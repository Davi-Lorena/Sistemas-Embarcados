#include <Wire.h>
#include <LiquidCrystal.h>

LiquidCrystal lcd(2, 3, 4, 5, 6, 7);

uint8_t PIN_BOTAO_INICIO = A0;
uint8_t PIN_BUZZER = A1;
bool BOTAO_ATIVO_ALTO = true;

uint8_t ENDERECO_ESCRAVO = 8;

unsigned long DEBOUNCE_MS = 30;
unsigned long INICIO_MS = 3000;
unsigned long CONTAGEM_PASSO_MS = 1000;
unsigned long POLL_MS = 20;
unsigned long PLACAR_MS = 3000;
unsigned long ERRO_MS = 2500;
unsigned long JA_VISIVEL_MS = 1000;
uint8_t FALHAS_I2C_MAX = 50;

uint8_t PONTOS_PARA_VENCER = 3;
unsigned long CAMPEAO_MS = 4000;

class BotaoInicio {
  public:
    uint8_t pino;
    bool estavel, bruta;
    unsigned long ultimoTempo;

    BotaoInicio(uint8_t p) : pino(p), estavel(false), bruta(false), ultimoTempo(0) {}

    void iniciar() { pinMode(pino, BOTAO_ATIVO_ALTO ? INPUT : INPUT_PULLUP); }

    bool apertadoAgora() const {
      return digitalRead(pino) == (BOTAO_ATIVO_ALTO ? HIGH : LOW);
    }

    bool clique(unsigned long agora) {
      bool leitura = apertadoAgora();
      if (leitura != bruta) { bruta = leitura; ultimoTempo = agora; }
      if ((agora - ultimoTempo) > DEBOUNCE_MS && leitura != estavel) {
        estavel = leitura;
        if (estavel) return true;
      }
      return false;
    }

    void sincronizar(unsigned long agora) {
      estavel = bruta = apertadoAgora();
      ultimoTempo = agora;
    }
};

BotaoInicio botaoInicio(PIN_BOTAO_INICIO);

enum EstadoMestre : uint8_t { INICIANDO, AGUARDANDO, CONTAGEM, JOGANDO, PLACAR, ERRO, CAMPEAO };

EstadoMestre estado = INICIANDO;
unsigned long marcaTempo = 0;
unsigned long ultimoPoll = 0;
uint8_t passoContagem = 255;
uint8_t mensagemAtual = 0;
uint8_t falhasI2C = 0;
int pontosP1 = 0, pontosP2 = 0;
int rodada = 1;

void iniciarLcd() {
  lcd.begin(16, 2);
  byte aTil[8] = {B01101, B10110, B01110, B00001, B01111, B10001, B01111, B00000};
  lcd.createChar(1, aTil);
}

void escreverLinhaCentralizada(uint8_t linha, const char* texto) {
  uint8_t len = strlen(texto);
  if (len > 16) len = 16;
  uint8_t espacos = (16 - len) / 2;

  lcd.setCursor(0, linha);
  for (uint8_t i = 0; i < espacos; i++) lcd.print(' ');
  for (uint8_t i = 0; i < len; i++) lcd.print(texto[i]);
  for (uint8_t i = espacos + len; i < 16; i++) lcd.print(' ');
}

void escreverPlacar(uint8_t linha) {
  char buf[17];
  snprintf(buf, sizeof(buf), "P1: %-3d  P2: %-3d", pontosP1, pontosP2);
  escreverLinhaCentralizada(linha, buf);
}

void telaAguardando() {
  iniciarLcd();
  escreverPlacar(0);
  escreverLinhaCentralizada(1, "Aperte p/ jogar");
}

void telaContagem(uint8_t numero) {
  char buf[17];
  snprintf(buf, sizeof(buf), "%d", numero);
  escreverLinhaCentralizada(1, buf);
}

bool enviarComando(char comando) {
  Wire.beginTransmission(ENDERECO_ESCRAVO);
  Wire.write((uint8_t)comando);
  return Wire.endTransmission() == 0;
}

void entrarEmErro(const char* linha0, const char* linha1) {
  enviarComando('R');
  escreverLinhaCentralizada(0, linha0);
  escreverLinhaCentralizada(1, linha1);
  estado = ERRO;
  marcaTempo = millis();
}

void tocarFanfarra() {
  const int melodia[] = {523, 659, 784, 1047, 784, 1047};
  for (uint8_t i = 0; i < 6; i++) {
    tone(PIN_BUZZER, melodia[i], 180);
    delay(260);
  }
}

void iniciarContagem(unsigned long agora) {
  estado = CONTAGEM;
  marcaTempo = agora;
  passoContagem = 255;
  char buf[17];
  snprintf(buf, sizeof(buf), "Rodada %d", rodada);
  escreverLinhaCentralizada(0, buf);
}

void iniciarCombate(unsigned long agora) {
  if (!enviarComando('S')) {
    entrarEmErro("Erro: Escravo", "nao responde");
    return;
  }
  estado = JOGANDO;
  ultimoPoll = agora;
  marcaTempo = agora;
  falhasI2C = 0;
  escreverPlacar(0);
  escreverLinhaCentralizada(1, "JA!");
  mensagemAtual = 3;
}

void processarVitoria(uint8_t vencedor) {
  if (vencedor == 1) pontosP1++; else pontosP2++;
  rodada++;

  escreverLinhaCentralizada(0, vencedor == 1 ? "JOGADOR 1" : "JOGADOR 2");
  escreverLinhaCentralizada(1, "VENCEU!");
  tocarFanfarra();
  iniciarLcd();

  if (pontosP1 >= PONTOS_PARA_VENCER || pontosP2 >= PONTOS_PARA_VENCER) {
    escreverLinhaCentralizada(0, vencedor == 1 ? "P1 campe\x01o!!!" : "P2 campe\x01o!!!");
    escreverPlacar(1);
    estado = CAMPEAO;
  } else {
    escreverPlacar(0);
    escreverLinhaCentralizada(1, vencedor == 1 ? "Ponto: JOGADOR 1" : "Ponto: JOGADOR 2");
    estado = PLACAR;
  }
  marcaTempo = millis();
}

void atualizarContagem(unsigned long agora) {
  uint8_t passo = (agora - marcaTempo) / CONTAGEM_PASSO_MS;
  if (passo == passoContagem) return;
  passoContagem = passo;

  if (passo < 3) {
    telaContagem(3 - passo);
    tone(PIN_BUZZER, 1000, 200);
  } else {
    tone(PIN_BUZZER, 1500, 500);
    iniciarCombate(agora);
  }
}

void atualizarCombate(unsigned long agora) {
  if (agora - ultimoPoll < POLL_MS) return;
  ultimoPoll = agora;

  if (Wire.requestFrom(ENDERECO_ESCRAVO, (uint8_t)1) < 1 || !Wire.available()) {
    if (++falhasI2C >= FALHAS_I2C_MAX) entrarEmErro("Erro: Escravo", "sem resposta");
    return;
  }
  falhasI2C = 0;

  uint8_t vencedor = Wire.read() & 0x03;

  if (vencedor == 1 || vencedor == 2) { processarVitoria(vencedor); return; }

  if (mensagemAtual == 3 && (agora - marcaTempo) >= JA_VISIVEL_MS) {
    escreverLinhaCentralizada(1, "LUTEM!!!");
    mensagemAtual = 0;
  }
}

void setup() {
  iniciarLcd();

  botaoInicio.iniciar();
  pinMode(PIN_BUZZER, OUTPUT);
  Wire.begin();
#if defined(WIRE_HAS_TIMEOUT)
  Wire.setWireTimeout(3000, true);
#endif

  escreverLinhaCentralizada(0, "Cabo de Guerra");
  escreverLinhaCentralizada(1, "Iniciando...");
  estado = INICIANDO;
  marcaTempo = millis();
}

void loop() {
  unsigned long agora = millis();

  switch (estado) {
    case INICIANDO:
      if (agora - marcaTempo >= INICIO_MS) {
        botaoInicio.sincronizar(agora);
        telaAguardando();
        estado = AGUARDANDO;
      }
      break;

    case AGUARDANDO:
      if (botaoInicio.clique(agora)) iniciarContagem(agora);
      break;

    case CONTAGEM:
      atualizarContagem(agora);
      break;

    case JOGANDO:
      atualizarCombate(agora);
      break;

    case PLACAR:
      if (agora - marcaTempo >= PLACAR_MS) {
        enviarComando('R');
        botaoInicio.sincronizar(agora);
        telaAguardando();
        estado = AGUARDANDO;
      }
      break;

    case ERRO:
      if (agora - marcaTempo >= ERRO_MS) {
        botaoInicio.sincronizar(agora);
        telaAguardando();
        estado = AGUARDANDO;
      }
      break;

    case CAMPEAO:
      if (agora - marcaTempo >= CAMPEAO_MS) {
        pontosP1 = 0;
        pontosP2 = 0;
        rodada = 1;
        enviarComando('R');
        botaoInicio.sincronizar(agora);
        telaAguardando();
        estado = AGUARDANDO;
      }
      break;
  }
}