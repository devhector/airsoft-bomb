/*
 * CERBERUS - Airsoft Bomb Prop
 * Para adicionar um novo modo de jogo:
 *  1. Adicione uma entrada em GameMode
 *  2. Implemente a lógica necessária
 *  3. Atualize stateSelectMode() e runCurrentMode()
 */

#include <Keypad.h>
#include <LiquidCrystal.h>

// ─── Pinos ────────────────────────────────────────────────────────────────────
LiquidCrystal lcd(14, 15, 16, 17, 18, 19);

const int PIN_GREEN  = 11;
const int PIN_BUZZER = 10;
const int PIN_CARD   = 12;   // Chave/cartão: INPUT_PULLUP, LOW = encaixado

// ─── Teclado ──────────────────────────────────────────────────────────────────
const byte ROWS = 4, COLS = 4;
char keys[ROWS][COLS] = {
  {'1','2','3','A'},
  {'4','5','6','B'},
  {'7','8','9','C'},
  {'*','0','#','D'}
};
byte rowPins[ROWS] = {9, 8, 7, 6};
byte colPins[COLS] = {5, 4, 3, 2};
Keypad keypad = Keypad(makeKeymap(keys), rowPins, colPins, ROWS, COLS);

// ─── Modos de jogo ────────────────────────────────────────────────────────────
enum GameMode {
  MODE_CLASSIC = 0,
  MODE_CARD,
  MODE_COUNT // Sempre deve ser o último
};

const char* modeNames[MODE_COUNT] = {
  "Classico",
  "Cartao"
};

// ─── Política de defuse (modo Cartão) ─────────────────────────────────────────
enum DefusePolicy {
  POLICY_CARD_ONLY = 0,
  POLICY_CARD_OR_PASS,
  POLICY_COUNT // Sempre deve ser o último
};

const char* policyNames[POLICY_COUNT] = {
  "Cartao",
  "Cartao/Senha"
};

// ─── Estado global da máquina ─────────────────────────────────────────────────
enum State {
  ST_SPLASH,
  ST_SELECT_MODE,
  ST_SET_PASSWORD,
  ST_SET_TIMER,
  ST_SET_DEFUSE_TIME,
  ST_CONFIRM_ARM,
  ST_COUNTDOWN,
  ST_DISARMING,
  ST_CARD_DEFUSING,
  ST_DISARMED,
  ST_EXPLODED,
  ST_RESET,
  ST_DEBUG_PINS
};

State        currentState  = ST_SPLASH;
GameMode     currentMode   = MODE_CLASSIC;
DefusePolicy currentPolicy = POLICY_CARD_ONLY;

// ─── Dados da rodada ──────────────────────────────────────────────────────────
char  password[5]  = {0};
char  entered[5]   = {0};
int   Hours        = 0;
int   Minutes      = 5;
int   Seconds      = 0;
int   tryCount     = 0;
long  secMillis    = 0;
long  tickInterval = 1000;
bool  lastRoundDisarmed = false;

// Modo cartão
int   defuseSeconds   = 10;
int   defuseRemaining = 0;
long  defuseMillis    = 0;
long  blinkMillis     = 0;
bool  ledGreenState   = false;

// ─── Helpers de hardware ──────────────────────────────────────────────────────
void beep(int freq = 5000, int dur = 100) {
  tone(PIN_BUZZER, freq, dur);
}

void beepError() {
  for (int i = 0; i < 3; i++) {
    tone(PIN_BUZZER, 300, 150);
    delay(200);
  }
}

void beepArm() {
  for (int i = 0; i < 3; i++) {
    beep(5000, 100);
    delay(150);
  }
}

void beepWin() {
  int melody[] = {1047, 1319, 1568};
  for (int n : melody) {
    tone(PIN_BUZZER, n, 200);
    delay(250);
  }
}

void resetLeds() {
  digitalWrite(PIN_GREEN, HIGH);  // HIGH = apagado (lógica invertida)
}

// Atualiza o blink do LED verde baseado no progresso do defuse.
// blinkInterval vai de 500ms (início) até 80ms (fim).
void updateDefuseBlink() {
  float progress     = 1.0f - ((float)defuseRemaining / (float)defuseSeconds);
  long blinkInterval = (long)(500 - progress * 420);
  if (blinkInterval < 80) blinkInterval = 80;

  unsigned long now = millis();
  if (now - blinkMillis >= (unsigned long)blinkInterval) {
    blinkMillis   = now;
    ledGreenState = !ledGreenState;
    digitalWrite(PIN_GREEN, ledGreenState ? LOW : HIGH);
  }
}

// ─── Helpers de LCD ───────────────────────────────────────────────────────────
void lcdMsg(const char* line0, const char* line1 = "") {
  lcd.clear();
  lcd.setCursor(0, 0); lcd.print(line0);
  lcd.setCursor(0, 1); lcd.print(line1);
}

char waitKey() {
  char k = NO_KEY;
  while (k == NO_KEY) k = keypad.getKey();
  return k;
}

bool readDigits(char* buf, int n, int row, int startCol) {
  int pos = 0;
  lcd.blink();
  lcd.setCursor(startCol, row);

  while (pos < n) {
    char k = keypad.getKey();
    if (k == NO_KEY) continue;
    if (k == '*') { lcd.noBlink(); return false; }
    if (k >= '0' && k <= '9') {
      beep();
      buf[pos] = k;
      lcd.setCursor(startCol + pos, row);
      lcd.print(k);
      pos++;
    }
  }
  lcd.noBlink();
  return true;
}

void printTime(int h, int m, int s, int row, int startCol) {
  auto printPad = [&](int val) {
    if (val < 10) lcd.print('0');
    lcd.print(val);
  };
  lcd.setCursor(startCol, row);
  printPad(h); lcd.print(':');
  printPad(m); lcd.print(':');
  printPad(s);
}

// ─── Desenha barra defuse cartao ───────────────────────────────────────────────────────────

void drawDefuseBar(int remaining, int total) {
  int filled = (int)(((float)(total - remaining) / (float)total) * 16.0f);
  if (filled > 16) filled = 16;
  lcd.setCursor(0, 1);
  for (int i = 0; i < 16; i++) {
    lcd.write(i < filled ? 254 : '-');
  }
}

// ─── Tela de splash ───────────────────────────────────────────────────────────
void stateSplash() {
  while (true) {
    lcdMsg("    CERBERUS   ", "Airsoft Division");
    unsigned long t = millis();
    while (millis() - t < 3000) {
      char k = keypad.getKey();
      if (k == '#') { currentState = ST_SELECT_MODE; return; }
      if (k == 'D') { currentState = ST_DEBUG_PINS;  return; }
    }

    lcdMsg("SEGURE # P/", "CONTINUAR");
    t = millis();
    while (millis() - t < 2000) {
      char k = keypad.getKey();
      if (k == '#') { currentState = ST_SELECT_MODE; return; }
      if (k == 'D') { currentState = ST_DEBUG_PINS;  return; }
    }
  }
}

// ─── Seleção de modo ──────────────────────────────────────────────────────────
void stateSelectMode() {
  int selected = 0;
  while (true) {
    lcd.clear();
    lcd.setCursor(0, 0); lcd.print("Modo de jogo:");
    lcd.setCursor(0, 1); lcd.print("> "); lcd.print(modeNames[selected]);

    char k = waitKey();
    if      (k == 'A' || k == '2') selected = (selected - 1 + MODE_COUNT) % MODE_COUNT;
    else if (k == 'B' || k == '8') selected = (selected + 1) % MODE_COUNT;
    else if (k == '#') {
      beep();
      currentMode  = (GameMode)selected;
      if (currentMode == MODE_CLASSIC)  currentState = ST_SET_PASSWORD;
      else                              currentState = ST_SET_TIMER;
      return;
    }
  }
}

// ─── Definir senha ───────────────────────────────────────────────────────────
void stateSetPassword() {
  if (currentMode == MODE_CARD) {
    // ── Passo 1: política ────────────────────────────────────────────────────
    int sel = 0;
    while (true) {
      lcd.clear();
      lcd.setCursor(0, 0); lcd.print("Desarmar com:");
      lcd.setCursor(0, 1); lcd.print("> "); lcd.print(policyNames[sel]);

      char k = waitKey();
      if      (k == 'A' || k == '2') sel = (sel - 1 + POLICY_COUNT) % POLICY_COUNT;
      else if (k == 'B' || k == '8') sel = (sel + 1) % POLICY_COUNT;
      else if (k == '*') { currentState = ST_SET_TIMER; return; }
      else if (k == '#') { beep(); break; }
    }
    currentPolicy = (DefusePolicy)sel;

    // ── Passo 2: senha (só se necessário) ───────────────────────────────────
    if (currentPolicy == POLICY_CARD_ONLY) {
      memset(password, 0, sizeof(password));  // sem senha
      currentState = ST_SET_DEFUSE_TIME;
      return;
    }
  }

  // Modo Clássico ou Cartão com política que requer senha
  lcdMsg("ESCOLHA A SENHA!", "Enter Code:    ");
  lcd.setCursor(11, 1);

  if (!readDigits(password, 4, 1, 11)) {
    currentState = (currentMode == MODE_CARD) ? ST_SET_TIMER : ST_SELECT_MODE;
    return;
  }
  password[4] = '\0';

  lcd.clear();
  lcd.setCursor(0, 0); lcd.print("SENHA: ");
  lcd.setCursor(6, 1);
  for (int i = 0; i < 4; i++) lcd.print(password[i]);
  delay(2500);

  currentState = (currentMode == MODE_CARD) ? ST_SET_DEFUSE_TIME : ST_SET_TIMER;
}

// ─── Definir temporizador da bomba ────────────────────────────────────────────
void stateSetTimer() {
  char buf[7] = {0};

  lcd.clear();
  lcd.setCursor(0, 0); lcd.print("Tempo: HH:MM:SS");
  lcd.setCursor(0, 1); lcd.print("SET: __:__:__  ");

  const int digitCols[6] = {5, 6, 8, 9, 11, 12};
  int pos = 0;
  lcd.blink();
  lcd.setCursor(digitCols[0], 1);

  while (pos < 6) {
    char k = keypad.getKey();
    if (k == NO_KEY) continue;

    if (k == '*') {
      lcd.noBlink();
      currentState = (currentMode == MODE_CLASSIC) ? ST_SET_PASSWORD : ST_SELECT_MODE;
      return;
    }
    if (k == '#' && pos > 0) {
      pos--;
      buf[pos] = 0;
      lcd.setCursor(digitCols[pos], 1); lcd.print('_');
      lcd.setCursor(digitCols[pos], 1);
      continue;
    }
    if (k >= '0' && k <= '9') {
      beep();
      buf[pos] = k;
      lcd.setCursor(digitCols[pos], 1); lcd.print(k);
      pos++;
      if (pos < 6) lcd.setCursor(digitCols[pos], 1);
    }
  }

  lcd.noBlink();

  Hours   = (buf[0]-'0')*10 + (buf[1]-'0');
  Minutes = (buf[2]-'0')*10 + (buf[3]-'0');
  Seconds = (buf[4]-'0')*10 + (buf[5]-'0');

  lcd.clear();
  lcd.setCursor(0, 0); lcd.print("Tempo escolhido:");
  printTime(Hours, Minutes, Seconds, 1, 4);
  delay(2500);

  tickInterval = 1000;
  tryCount     = 0;

  // Modo Cartão: vai configurar política + senha antes do tempo de defuse
  if (currentMode == MODE_CARD) currentState = ST_SET_PASSWORD;
  else                          currentState = ST_CONFIRM_ARM;
}

// ─── Definir tempo de defuse (modo cartão) ────────────────────────────────────
void stateSetDefuseTime() {

  // ── Passo 1: escolher unidade ──────────────────────────────────────────────
  const char* units[]  = { "Segundos", "Minutos", "Horas" };
  const int   nUnits   = 3;
  int         unitSel  = 0;

  while (true) {
    lcd.clear();
    lcd.setCursor(0, 0); lcd.print("Unidade de tempo:");
    lcd.setCursor(0, 1); lcd.print("> "); lcd.print(units[unitSel]);

    char k = waitKey();
    if      (k == 'A' || k == '2') unitSel = (unitSel - 1 + nUnits) % nUnits;
    else if (k == 'B' || k == '8') unitSel = (unitSel + 1) % nUnits;
    else if (k == '*') { currentState = ST_SET_PASSWORD; return; }
    else if (k == '#') { beep(); break; }
  }

  // ── Passo 2: digitar o valor (2 dígitos) ──────────────────────────────────
  char buf[3] = {0};

  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Desarmar p/ ");
  lcd.print(units[unitSel]);
  lcd.setCursor(0, 1); lcd.print("SET: __ ");

  switch (unitSel) {
    case 0: lcd.print("seg"); break;
    case 1: lcd.print("min"); break;
    case 2: lcd.print("hr "); break;
  }

  const int digitCols[2] = {5, 6};
  int pos = 0;
  lcd.blink();
  lcd.setCursor(digitCols[0], 1);

  while (pos < 2) {
    char k = keypad.getKey();
    if (k == NO_KEY) continue;

    if (k == '*') {
      lcd.noBlink();
      currentState = ST_SET_PASSWORD;
      return;
    }
    if (k == '#' && pos > 0) {
      pos--;
      buf[pos] = 0;
      lcd.setCursor(digitCols[pos], 1); lcd.print('_');
      lcd.setCursor(digitCols[pos], 1);
      continue;
    }
    if (k >= '0' && k <= '9') {
      beep();
      buf[pos] = k;
      lcd.setCursor(digitCols[pos], 1); lcd.print(k);
      pos++;
      if (pos < 2) lcd.setCursor(digitCols[pos], 1);
    }
  }

  lcd.noBlink();

  int val = (buf[0]-'0')*10 + (buf[1]-'0');
  if (val < 1) val = 1;

  switch (unitSel) {
    case 0: defuseSeconds = val;           break;
    case 1: defuseSeconds = val * 60;      break;
    case 2: defuseSeconds = val * 3600;    break;
  }

  lcd.clear();
  lcd.setCursor(0, 0); lcd.print("Desarma em:");
  lcd.setCursor(0, 1); lcd.print(val); lcd.print(" "); lcd.print(units[unitSel]);
  delay(2500);

  currentState = ST_CONFIRM_ARM;
}

// ─── Confirmar armar ──────────────────────────────────────────────────────────
void stateConfirmArm() {
  lcdMsg("#=ARMAR", "*=CORRIGIR");

  while (true) {
    char k = keypad.getKey();
    if (k == '#') {
      beepArm();
      lcdMsg("Bomba Armada!", "Iniciando...");
      delay(1500);
      lcd.clear();
      secMillis    = millis();
      currentState = ST_COUNTDOWN;
      return;
    }
    if (k == '*') {
      currentState = (currentMode == MODE_CARD) ? ST_SET_DEFUSE_TIME : ST_SET_TIMER;
      return;
    }
  }
}

// ─── Lógica do timer da bomba ─────────────────────────────────────────────────
bool tickTimer() {
  unsigned long now = millis();
  if (now - secMillis < (unsigned long)tickInterval) return false;

  secMillis = now;
  Seconds--;
  if (Seconds < 0) { Seconds = 59; Minutes--; }
  if (Minutes < 0) { Minutes = 59; Hours--;   }

  tone(PIN_BUZZER, 7000, 50);

  if (Hours < 0 || (Hours == 0 && Minutes < 0) ||
      (Hours == 0 && Minutes == 0 && Seconds <= 0)) {
    return true;
  }
  return false;
}

void displayTimer() {
  lcd.setCursor(0, 1); lcd.print("Timer:");
  printTime(Hours, Minutes, Seconds, 1, 7);
}

// ─── Estado: contagem regressiva ──────────────────────────────────────────────
void stateCountdown() {
  displayTimer();

  if (tickTimer()) {
    currentState = ST_EXPLODED;
    return;
  }

  if (currentMode == MODE_CARD) {
    char k = keypad.getKey();

    // Cartão encaixado → inicia defuse
    if (digitalRead(PIN_CARD) == LOW) {
      beep();
      defuseRemaining = defuseSeconds;
      defuseMillis    = millis();
      blinkMillis     = millis();
      ledGreenState   = false;
      lcd.clear();
      currentState = ST_CARD_DEFUSING;
      return;
    }

    // Tecla * → tenta desarmar por senha (se política permite)
    if (currentPolicy != POLICY_CARD_ONLY && k == '*') {
      beep();
      currentState = ST_DISARMING;
      memset(entered, 0, sizeof(entered));
    }

  } else {
    char k = keypad.getKey();
    if (k == '*') {
      beep();
      currentState = ST_DISARMING;
      memset(entered, 0, sizeof(entered));
    }
  }
}

// ─── Estado: defuse por cartão ────────────────────────────────────────────────
void stateCardDefusing() {
  if (digitalRead(PIN_CARD) == HIGH) {
    digitalWrite(PIN_GREEN, HIGH);
    ledGreenState = false;
    beepError();
    lcdMsg("Cartao removido!", "Desarmar cancelado");
    delay(1500);
    lcd.clear();
    currentState = ST_COUNTDOWN;
    return;
  }

  lcd.setCursor(0, 0);
  lcd.print("DEFUSE: ");
  if (defuseRemaining < 10) lcd.print(' ');
  lcd.print(defuseRemaining);
  lcd.print("s  ");

  drawDefuseBar(defuseRemaining, defuseSeconds);
  updateDefuseBlink();

  // Permite abortar o defuse por cartão e tentar por senha (CARD_OR_PASS)
  if (currentPolicy == POLICY_CARD_OR_PASS) {
    char k = keypad.getKey();
    if (k == '*') {
      digitalWrite(PIN_GREEN, HIGH);
      ledGreenState = false;
      lcd.clear();
      currentState = ST_DISARMING;
      memset(entered, 0, sizeof(entered));
      return;
    }
  }

  unsigned long now = millis();
  if (now - defuseMillis >= 1000) {
    defuseMillis = now;
    defuseRemaining--;
    tone(PIN_BUZZER, 3000, 60);
  }

  if (defuseRemaining <= 0) {
    digitalWrite(PIN_GREEN, HIGH);
    currentState = ST_DISARMED;
    return;
  }

  if (tickTimer()) {
    digitalWrite(PIN_GREEN, HIGH);
    currentState = ST_EXPLODED;
  }
}

// ─── Estado: digitando código para desarmar ─────────────────────────────────
void stateDisarming() {
  lcd.clear();
  lcd.setCursor(0, 0); lcd.print("Code: ");
  int pos = 0;

  while (pos < 4) {
    displayTimer();
    if (tickTimer()) { currentState = ST_EXPLODED; return; }

    char k = keypad.getKey();
    if (k == NO_KEY) continue;

    if (k == '#') {
      beep();
      pos = 0;
      lcd.setCursor(0, 0); lcd.print("Code:       ");
      lcd.setCursor(6, 0); lcd.print("    ");
      continue;
    }
    if (k == '*') {
      currentState = ST_COUNTDOWN;
      lcd.clear();
      return;
    }

    lcd.setCursor(6 + pos, 0); lcd.print(k);
    entered[pos] = k;
    pos++;
    beep();
    delay(80);
    lcd.setCursor(6 + pos - 1, 0); lcd.print('*');
  }

  entered[4] = '\0';
  bool correct = (memcmp(entered, password, 4) == 0);

  if (correct) {
    currentState = ST_DISARMED;
  } else {
    tryCount++;
    beepError();
    lcdMsg("SENHA ERRADA!", "");
    lcd.setCursor(0, 1); lcd.print("Tentativa: "); lcd.print(tryCount);
    delay(1200);

    long totalSec = (long)Hours * 3600 + Minutes * 60 + Seconds;
    totalSec /= 2;
    if (totalSec < 1) totalSec = 1;
    Hours   = totalSec / 3600;
    Minutes = (totalSec % 3600) / 60;
    Seconds = totalSec % 60;

    if (tryCount >= 2) tickInterval = max(100L, tickInterval / 2);

    currentState = ST_COUNTDOWN;
    lcd.clear();
  }
}

// ─── Estado: desarmada com sucesso ────────────────────────────────────────────
void stateDisarmed() {
  beepWin();
  lastRoundDisarmed = true;
  digitalWrite(PIN_GREEN, LOW);
  lcdMsg("Bomba desarmada!", "Bom Trabalho!");
  delay(8000);
  digitalWrite(PIN_GREEN, HIGH);
  currentState = ST_RESET;
}

// ─── Estado: explodiu ─────────────────────────────────────────────────────────
void stateExploded() {
  lastRoundDisarmed = false;
  lcdMsg(" A BOMBA ", "  EXPLODIU!!");
  for (int i = 0; i < 8; i++) {
    tone(PIN_BUZZER, 7000, 80);
    delay(200);
  }
  currentState = ST_RESET;
}

// ─── Estado: aguarda reset ────────────────────────────────────────────────────
void stateReset() {
  bool showResult = true;
  unsigned long lastSwitch = millis();

  while (true) {
    char k = keypad.getKey();

    if (k == 'D') {
      break;
    }

    unsigned long now = millis();

    if (now - lastSwitch >= 2000) {
      showResult = !showResult;
      lastSwitch = now;
      lcd.clear();
    }

    if (showResult) {
      if (lastRoundDisarmed) {
        lcd.setCursor(0, 0);
        lcd.print("Bomba desarmada");
        lcd.setCursor(0, 1);
        lcd.print("com sucesso!");
      } else {
        lcd.setCursor(0, 0);
        lcd.print("A BOMBA");
        lcd.setCursor(0, 1);
        lcd.print("EXPLODIU!");
      }
    }

    delay(50);
  }

  Hours           = 0;
  Minutes         = 5;
  Seconds         = 0;
  tryCount        = 0;
  tickInterval    = 1000;
  secMillis       = millis();
  defuseRemaining = 0;
  ledGreenState   = false;
  currentPolicy   = POLICY_CARD_ONLY;
  memset(password, 0, sizeof(password));
  memset(entered,  0, sizeof(entered));
  resetLeds();
  lcd.noBlink();
  lcd.clear();

  currentState = ST_SPLASH;
}

// ─── Estado: debug de pinos digitais ──────────────────────────────────────────
void stateDebugPins() {
  const int debugPins[4] = {10, 11, 12, 13};
  int lastState[4];

  // Coloca todos os pinos como entrada com pull-up interno para o teste
  for (int i = 0; i < 4; i++) {
    pinMode(debugPins[i], INPUT_PULLUP);
    lastState[i] = digitalRead(debugPins[i]);
  }

  Serial.println("=== MODO DEBUG DE PINOS ===");
  Serial.println("Pressione * para sair");
  for (int i = 0; i < 4; i++) {
    Serial.print("P"); Serial.print(debugPins[i]);
    Serial.print(" inicial: "); Serial.println(lastState[i]);
  }

  lcd.clear();

  while (true) {
    char k = keypad.getKey();
    if (k == '*') {
      // Restaura os pinos para seus usos normais
      pinMode(PIN_GREEN,  OUTPUT);
      pinMode(PIN_CARD,   INPUT_PULLUP);
      resetLeds();
      lcd.clear();
      currentState = ST_SPLASH;
      return;
    }

    bool changed = false;

    for (int i = 0; i < 4; i++) {
      int val = digitalRead(debugPins[i]);
      if (val != lastState[i]) {
        changed = true;
        Serial.print(">>> MUDANCA no pino ");
        Serial.print(debugPins[i]);
        Serial.print(": ");
        Serial.print(lastState[i]);
        Serial.print(" -> ");
        Serial.print(val);
        Serial.print("  (t=");
        Serial.print(millis());
        Serial.println("ms)");

        lastState[i] = val;
      }
    }

    // Atualiza o LCD sempre, mostrando o estado atual dos 4 pinos
    lcd.setCursor(0, 0);
    lcd.print("P10:"); lcd.print(lastState[0]);
    lcd.print(" P11:"); lcd.print(lastState[1]);
    lcd.print("    ");

    lcd.setCursor(0, 1);
    lcd.print("P12:"); lcd.print(lastState[2]);
    lcd.print(" P13:"); lcd.print(lastState[3]);
    lcd.print(changed ? " *" : "  ");

    delay(50);
  }
}

// ─── Despacho por modo de jogo ────────────────────────────────────────────────
void runCurrentMode() {
  
  if (currentState == ST_DEBUG_PINS) {
    stateDebugPins();
    return;
  }

  switch (currentMode) {

    case MODE_CARD:
      switch (currentState) {
        case ST_SPLASH:          stateSplash();        break;
        case ST_SELECT_MODE:     stateSelectMode();    break;
        case ST_SET_TIMER:       stateSetTimer();      break;
        case ST_SET_PASSWORD:    stateSetPassword();   break;
        case ST_SET_DEFUSE_TIME: stateSetDefuseTime(); break;
        case ST_CONFIRM_ARM:     stateConfirmArm();    break;
        case ST_COUNTDOWN:       stateCountdown();     break;
        case ST_CARD_DEFUSING:   stateCardDefusing();  break;
        case ST_DISARMING:       stateDisarming();     break;
        case ST_DISARMED:        stateDisarmed();      break;
        case ST_EXPLODED:        stateExploded();      break;
        case ST_RESET:           stateReset();         break;
        default: break;
      }
      break;

    case MODE_CLASSIC:
    default:
      switch (currentState) {
        case ST_SPLASH:       stateSplash();      break;
        case ST_SELECT_MODE:  stateSelectMode();  break;
        case ST_SET_PASSWORD: stateSetPassword(); break;
        case ST_SET_TIMER:    stateSetTimer();    break;
        case ST_CONFIRM_ARM:  stateConfirmArm();  break;
        case ST_COUNTDOWN:    stateCountdown();   break;
        case ST_DISARMING:    stateDisarming();   break;
        case ST_DISARMED:     stateDisarmed();    break;
        case ST_EXPLODED:     stateExploded();    break;
        case ST_RESET:        stateReset();       break;
        default: break;
      }
      break;
  }
}

// ─── Arduino entry points ─────────────────────────────────────────────────────
void setup() {
  Serial.begin(9600);
  
  pinMode(PIN_GREEN,  OUTPUT);
  pinMode(PIN_CARD,   INPUT_PULLUP);
  resetLeds();

  lcd.begin(16, 2);

  currentState = ST_SPLASH;
}

void loop() {
  runCurrentMode();
}
