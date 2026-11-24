#include <Wire.h>

uint8_t NUM_LEDS = 10;
uint8_t LEDS_POR_LADO = 4;
uint8_t LEDS[NUM_LEDS] = {6, 7, 8, 9,  4, 5,  13, 12, 11, 10};

uint8_t PIN_BOTAO_P1 = A3;
uint8_t PIN_BOTAO_P2 = A2;

bool BOTAO_ATIVO_ALTO = true;

uint8_t ENDERECO_I2C = 8;
int8_t POSICAO_INICIAL = (NUM_LEDS / 2) - 1;
unsigned long DEBOUNCE_MS = 30;
unsigned long PISCA_VITORIA_MS = 150;
unsigned long TESTE_PASSO_MS = 150;

class Jogador {
  public:
    uint8_t pinoBotao;
    int8_t direcao;

    bool estadoEstavel, ultimaLeituraBruta;
    unsigned long ultimoTempoDebounce;

    Jogador(uint8_t botao, int8_t dir)
      : pinoBotao(botao), direcao(dir),
        estadoEstavel(false), ultimaLeituraBruta(false), ultimoTempoDebounce(0) {}

    void iniciarHardware() {
      pinMode(pinoBotao, BOTAO_ATIVO_ALTO ? INPUT : INPUT_PULLUP);
    }

    bool botaoApertadoAgora() const {
      return digitalRead(pinoBotao) == (BOTAO_ATIVO_ALTO ? HIGH : LOW);
    }

    void reiniciar(unsigned long agora) {
      estadoEstavel = ultimaLeituraBruta = botaoApertadoAgora();
      ultimoTempoDebounce = agora;
    }

    bool cliqueDetectado(unsigned long agora) {
      bool leitura = botaoApertadoAgora();
      if (leitura != ultimaLeituraBruta) {
        ultimaLeituraBruta = leitura;
        ultimoTempoDebounce = agora;
      }
      if ((agora - ultimoTempoDebounce) > DEBOUNCE_MS && leitura != estadoEstavel) {
        estadoEstavel = leitura;
        if (estadoEstavel) return true;
      }
      return false;
    }
};

Jogador jogadores[2] = {
  Jogador(PIN_BOTAO_P1, -1),
  Jogador(PIN_BOTAO_P2, +1)
};

enum EstadoEscravo : uint8_t { TESTE_INICIAL, OCIOSO, JOGANDO, VITORIA };

EstadoEscravo estado = TESTE_INICIAL;
int8_t  posicao = POSICAO_INICIAL;
uint8_t vencedor = 0;
unsigned long inicioTeste = 0;

volatile char    comandoPendente = 0;
volatile uint8_t statusI2C = 0;

void apagarCorda() {
  for (uint8_t i = 0; i < NUM_LEDS; i++) digitalWrite(LEDS[i], LOW);
}

void mostrarMarcador() {
  apagarCorda();
  digitalWrite(LEDS[posicao], HIGH);
  digitalWrite(LEDS[posicao + 1], HIGH);
}

void mostrarVitoria(unsigned long agora) {
  bool ligado = (agora / PISCA_VITORIA_MS) % 2;
  uint8_t inicio = (vencedor == 1) ? 0 : (NUM_LEDS - LEDS_POR_LADO);
  apagarCorda();
  for (uint8_t i = 0; i < LEDS_POR_LADO; i++) digitalWrite(LEDS[inicio + i], ligado);
}

void prepararPartida(unsigned long agora) {
  posicao = POSICAO_INICIAL;
  vencedor = 0;
  for (uint8_t i = 0; i < 2; i++) jogadores[i].reiniciar(agora);
  mostrarMarcador();
}

void entrarOcioso() {
  prepararPartida(millis());
  estado = OCIOSO;
}

void iniciarRodada() {
  prepararPartida(millis());
  estado = JOGANDO;
}

void declararVitoria(uint8_t quem) {
  vencedor = quem;
  estado = VITORIA;
}

void executarTeste(unsigned long agora) {
  static unsigned long ultimoPasso = 0xFFFFFFFFUL;
  unsigned long passo = (agora - inicioTeste) / TESTE_PASSO_MS;

  if (passo == ultimoPasso) return;
  ultimoPasso = passo;

  apagarCorda();

  if (passo < NUM_LEDS) digitalWrite(LEDS[passo], HIGH);
  else                  entrarOcioso();
}

void processarPuxada(uint8_t idx) {
  posicao += jogadores[idx].direcao;

  if (posicao < 0)                           declararVitoria(1);
  else if (posicao > (int8_t)(NUM_LEDS - 2)) declararVitoria(2);
  else                                       mostrarMarcador();
}

void executarRodada(unsigned long agora) {
  for (uint8_t i = 0; i < 2; i++) {
    if (jogadores[i].cliqueDetectado(agora)) {
      processarPuxada(i);
      if (estado != JOGANDO) return;
    }
  }
}

void receberComando(int quantidade) {
  while (Wire.available()) comandoPendente = (char)Wire.read();
}

void enviarStatus() {
  Wire.write((uint8_t)statusI2C);
}

void tratarComandoPendente() {
  noInterrupts();
  char cmd = comandoPendente;
  comandoPendente = 0;
  interrupts();

  if (cmd == 'S')                                   iniciarRodada();
  else if (cmd == 'R' && estado != TESTE_INICIAL)   entrarOcioso();
}

void setup() {
  for (uint8_t i = 0; i < NUM_LEDS; i++) pinMode(LEDS[i], OUTPUT);
  for (uint8_t i = 0; i < 2; i++) jogadores[i].iniciarHardware();

  Wire.begin(ENDERECO_I2C);
  Wire.onReceive(receberComando);
  Wire.onRequest(enviarStatus);

  inicioTeste = millis();
  estado = TESTE_INICIAL;
}

void loop() {
  unsigned long agora = millis();

  tratarComandoPendente();

  switch (estado) {
    case TESTE_INICIAL: executarTeste(agora); break;
    case OCIOSO: break;
    case JOGANDO: executarRodada(agora); break;
    case VITORIA: mostrarVitoria(agora); break;
  }

  statusI2C = vencedor;
}