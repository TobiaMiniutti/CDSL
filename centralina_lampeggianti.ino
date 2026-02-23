#include <LiquidCrystal_I2C.h> //variaile LCD I2C
#include <Wire.h> //variaile LCD I2C
#include <Keypad.h> //variabile Membrane Keypad
#include <EEPROM.h> //variaible persistence
#if defined(ARDUINO_ARCH_AVR)
#include <avr/wdt.h>
#endif
#include <stdio.h>

const byte EEPROM_ADDR = 0;
const char KEY_STEP_FORWARD = '#';
const char KEY_STEP_BACK = '*';
const char KEY_RESET = '0';
const char KEY_TOGGLE_BACKLIGHT = 'A';
const char KEY_SHOW_UPTIME = 'B';
const char KEY_SHOW_EEPROM = 'C';
const char KEY_TOGGLE_DEBUG = 'D';
const unsigned long INFO_WINDOW_MS = 1500;
const unsigned long MENU_ITEM_MS = 2000;
const unsigned long MENU_SCROLL_MS = 250;
const unsigned long RESET_CONFIRM_MS = 5000;

int pinIntIO(10);
int pinButCM(11);
int pinRelIO(12);
int pinRelCM(13);
//int pinSDA(A4);
//int pinSCL(A5);

int valIntIO;
int valButCM;
int stato = 1; //(value, add, min, max)
const int STATO_MIN = 1;
const int STATO_MAX = 7;
int prevIntIO = HIGH;

bool backlightOn = true;
bool debugEnabled = true;
unsigned long infoUntilMs = 0;
char infoLine0[17];
char infoLine1[17];

bool menuActive = false;
unsigned long menuLastItemMs = 0;
unsigned long menuLastScrollMs = 0;
int menuIndex = 0;
int menuScrollOffset = 0;

int resetConfirmStage = 0;
unsigned long resetConfirmAtMs = 0;

LiquidCrystal_I2C lcd(0x27, 16, 2); //first parameter is I2C addres; second parameter is how many columns are on the screen; third parameter is how many rows are on the screen 
//LCD-I2Clcd(0x27, 16, 2); //first parameter is I2C addres; second parameter is how many columns are on the screen; third parameter is how many rows are on the screen 

const byte ROWS(4); //number of rows on the screen
const byte COLS(4); //number of columns on the screen
//define the symbols on the butons of the keypad
char hexaKeys[ROWS][COLS] = {
  {'1','2','3','A'},
  {'4','5','6','B'},
  {'7','8','9','C'},
  {'*','0','#','D'}
};
byte rowPins[ROWS] = {9,8,7,6};
byte colPins[COLS] = {5,4,3,2};
Keypad customKeypad = Keypad( makeKeymap(hexaKeys), rowPins, colPins, ROWS, COLS);

void persistState(){
  EEPROM.update(EEPROM_ADDR, stato);
}

void debugPrint(const char* msg){
  if (debugEnabled){
    Serial.println(msg);
  }
}

void debugPrintKey(char k){
  if (debugEnabled){
    Serial.print("Pressed Key: ");
    Serial.println(k);
  }
}

void printPadded(const char* s){
  int i = 0;
  while (i < 16 && s[i] != '\0'){
    lcd.print(s[i]);
    i++;
  }
  while (i < 16){
    lcd.print(' ');
    i++;
  }
}

void pulseRelCM(int steps){
  for (int i = 0; i < steps; i++){
    digitalWrite(pinRelCM, HIGH);
    delay(180);
    digitalWrite(pinRelCM, LOW);
    delay(180);
  }
}

void goToState(int nstato){
  if (nstato < STATO_MIN || nstato > STATO_MAX){
    return;
  }
  if (nstato == stato){
    return;
  }
  int steps = (nstato - stato + STATO_MAX) % STATO_MAX;
  if (steps == 0){
    return;
  }
  pulseRelCM(steps);
  stato = nstato;
  persistState();
}

void stepForward(){
  int nstato = (stato >= STATO_MAX) ? STATO_MIN : (stato + 1);
  goToState(nstato);
}

void stepBackward(){
  // Non cambia il rele: la modalita e solo in avanti con on/off 12V
  snprintf(infoLine0, sizeof(infoLine0), "INDIETRO N/A ");
  snprintf(infoLine1, sizeof(infoLine1), "solo avanti   ");
  infoUntilMs = millis() + INFO_WINDOW_MS;
}

void showUptime(){
  unsigned long up = millis() / 1000;
  snprintf(infoLine0, sizeof(infoLine0), "UPTIME %lus    ", up);
  snprintf(infoLine1, sizeof(infoLine1), "               ");
  infoUntilMs = millis() + INFO_WINDOW_MS;
}

void showEepromState(){
  snprintf(infoLine0, sizeof(infoLine0), "EEPROM STATO   ");
  snprintf(infoLine1, sizeof(infoLine1), "SALVATO %d     ", stato);
  infoUntilMs = millis() + INFO_WINDOW_MS;
}

void toggleBacklight(){
  backlightOn = !backlightOn;
  if (backlightOn){
    lcd.backlight();
    snprintf(infoLine0, sizeof(infoLine0), "LCD LIGHT ON   ");
  } else {
    lcd.noBacklight();
    snprintf(infoLine0, sizeof(infoLine0), "LCD LIGHT OFF  ");
  }
  snprintf(infoLine1, sizeof(infoLine1), "               ");
  infoUntilMs = millis() + INFO_WINDOW_MS;
}

void toggleDebug(){
  debugEnabled = !debugEnabled;
  if (debugEnabled){
    Serial.println("Debug ON");
    snprintf(infoLine0, sizeof(infoLine0), "DEBUG ON       ");
  } else {
    snprintf(infoLine0, sizeof(infoLine0), "DEBUG OFF      ");
  }
  snprintf(infoLine1, sizeof(infoLine1), "               ");
  infoUntilMs = millis() + INFO_WINDOW_MS;
}

void softReset(){
#if defined(ARDUINO_ARCH_AVR)
  wdt_enable(WDTO_15MS);
  while (true) {}
#else
  while (true) {}
#endif
}

const char* menuLabels[] = {
  "1 BACKLIGHT ON/OFF",
  "2 MOSTRA UPTIME",
  "3 STATO EEPROM",
  "4 DEBUG ON/OFF",
  "5 RESET ARDUINO"
};
const int MENU_COUNT = sizeof(menuLabels) / sizeof(menuLabels[0]);

void menuNext(){
  menuIndex = (menuIndex + 1) % MENU_COUNT;
  menuScrollOffset = 0;
  menuLastItemMs = millis();
  menuLastScrollMs = millis();
}

void menuPrev(){
  menuIndex = (menuIndex - 1 + MENU_COUNT) % MENU_COUNT;
  menuScrollOffset = 0;
  menuLastItemMs = millis();
  menuLastScrollMs = millis();
}

void renderMenu(){
  const char* label = menuLabels[menuIndex];
  int len = strlen(label);
  if (millis() - menuLastItemMs > MENU_ITEM_MS){
    menuNext();
    label = menuLabels[menuIndex];
    len = strlen(label);
  }
  if (len > 16 && (millis() - menuLastScrollMs > MENU_SCROLL_MS)){
    menuScrollOffset++;
    if (menuScrollOffset > len){
      menuScrollOffset = 0;
    }
    menuLastScrollMs = millis();
  }

  lcd.setCursor(0,0);
  printPadded("MENU: 0 ESC");
  lcd.setCursor(0,1);
  if (len <= 16){
    printPadded(label);
  } else {
    int start = menuScrollOffset;
    for (int i = 0; i < 16; i++){
      int idx = start + i;
      char c = (idx < len) ? label[idx] : ' ';
      lcd.print(c);
    }
  }
}

void resetConfirmStart(){
  resetConfirmStage = 1;
  resetConfirmAtMs = millis();
}

void resetConfirmAdvance(){
  if (resetConfirmStage == 1){
    resetConfirmStage = 2;
    resetConfirmAtMs = millis();
  } else if (resetConfirmStage == 2){
    lcd.setCursor(0,0);
    printPadded("RESET ARDUINO");
    lcd.setCursor(0,1);
    printPadded("ATTENDI...");
    delay(200);
    softReset();
  }
}

void resetConfirmCancel(){
  resetConfirmStage = 0;
}

void openMenu(){
  menuActive = true;
  menuLastItemMs = millis();
  menuLastScrollMs = millis();
  menuScrollOffset = 0;
}

void renderResetConfirm(){
  if (resetConfirmStage == 1){
    lcd.setCursor(0,0);
    printPadded("RESET? premi 5");
    lcd.setCursor(0,1);
    printPadded("per conferma");
  } else if (resetConfirmStage == 2){
    lcd.setCursor(0,0);
    printPadded("CONFERMA FINALE");
    lcd.setCursor(0,1);
    printPadded("premi 5");
  }
}

bool handleMenuSelection(char key){
  if (key == '1'){
    toggleBacklight();
    return true;
  } else if (key == '2'){
    showUptime();
    return true;
  } else if (key == '3'){
    showEepromState();
    return true;
  } else if (key == '4'){
    toggleDebug();
    return true;
  } else if (key == '5'){
    resetConfirmStart();
    return false;
  }
  return false;
}

void setup() {

  Serial.begin(9600);
  pinMode(pinIntIO, INPUT_PULLUP);
  pinMode(pinButCM, INPUT_PULLUP);
  pinMode(pinRelIO, OUTPUT);
  pinMode(pinRelCM, OUTPUT);
//  pinMode(pinSDA, OUTPUT);
//  pinMode(pinSCL, OUTPUT);

  lcd.init(); //initialize lcd
  lcd.backlight(); //turn on the backlight

  int saved = EEPROM.read(EEPROM_ADDR);
  if (saved >= 1 && saved <= 7){
    stato = saved;
  } else {
    persistState();
  }

}

void loop(){
  // Evita di sovrascrivere stato quando non viene premuto alcun tasto
  /*
  if (customKey >= 1){
    Serial.println(customKey);
  */

/*  while(1){
    lcd.setCursor(0,0); //tell the screen to set cursor on 0;0
    lcd.print("Hello World!"); //tell the screen to write "Hello World"
    lcd.setCursor(0,1);
    lcd.print("DSL v1.0.3");

    delay(1000);
  }
*/

  valIntIO = digitalRead(pinIntIO); //associare variabile valore interruttore on/off all'effettivo stato
  valButCM = digitalRead(pinButCM); //associare variabile valore pulsante cambio modalita all'effettivo stato
  if (prevIntIO == LOW && valIntIO == HIGH){
    // Persistenza anche quando spengo con intIO
    persistState();
  }
  prevIntIO = valIntIO;

  if (valIntIO == 0){ //variante se lampeggianti accesi

    debugPrint("IntIO ON");
    
    lcd.setCursor(0,0);
    printPadded("STATO DSL: ON");
    lcd.setCursor(0,1);
    printPadded("STATO MDL:");
    lcd.setCursor(12,1);
    lcd.print(stato);

    digitalWrite(pinRelIO, HIGH);

    while(valIntIO == 0){

      valIntIO = digitalRead(pinIntIO);
      char customKey = customKeypad.getKey();
      if(customKey){
        debugPrintKey(customKey);

        if (menuActive){
          if (customKey == '0'){
            menuActive = false;
            resetConfirmCancel();
          } else if (customKey == KEY_STEP_FORWARD){
            menuNext();
          } else if (customKey == KEY_STEP_BACK){
            menuPrev();
          } else if (customKey >= '1' && customKey <= '9'){
            if (resetConfirmStage > 0){
              if (customKey == '5'){
                resetConfirmAdvance();
              } else {
                resetConfirmCancel();
              }
            } else {
              bool closeMenu = handleMenuSelection(customKey);
              if (closeMenu){
                menuActive = false;
              }
            }
          }
        }
        else if(customKey >= '1' && customKey <= '7'){
          int nstato = customKey - '0';
          goToState(nstato);
        }
        else if (customKey == KEY_STEP_FORWARD){ // skip di 1 passo
          stepForward();
        }
        else if (customKey == KEY_STEP_BACK){ // niente rele, solo info
          stepBackward();
        }
        else if (customKey == KEY_RESET){ // menu funzioni
          openMenu();
        }
        else if (customKey == KEY_TOGGLE_BACKLIGHT){
          toggleBacklight();
        }
        else if (customKey == KEY_SHOW_UPTIME){
          showUptime();
        }
        else if (customKey == KEY_SHOW_EEPROM){
          showEepromState();
        }
        else if (customKey == KEY_TOGGLE_DEBUG){
          toggleDebug();
        }
      }

      if (menuActive){
        if (resetConfirmStage > 0 && (millis() - resetConfirmAtMs > RESET_CONFIRM_MS)){
          resetConfirmCancel();
        }
        if (resetConfirmStage > 0){
          renderResetConfirm();
        } else {
          renderMenu();
        }
      }
      else if (infoUntilMs != 0 && millis() < infoUntilMs){
        lcd.setCursor(0,0);
        printPadded(infoLine0);
        lcd.setCursor(0,1);
        printPadded(infoLine1);
      } else {
        infoUntilMs = 0;
        lcd.setCursor(0,0);
        printPadded("STATO DSL: ON");
        lcd.setCursor(0,1);
        printPadded("STATO MDL:");
        lcd.setCursor(12,1);
        lcd.print(stato);
      }

        /*while(customKey){

        if(customKey == stato){ //variante se modalita selezionata corrisponde all'attuale

          Serial.print("Pressed Key ");
          Serial.println(customKey);

          lcd.setCursor(12,1);
          lcd.print(stato);
        }

          else{

            if(customKey >= '10'){ //segnatura doppio numero su lcd

              Serial.print("Pressed Key ");
              Serial.println(customKey);

              lcd.setCursor(12,1);
              lcd.print(stato);
            }
              else{ //segnatura singolo numero + spazio per evitare sovrapposizioni su lcd

                Serial.print("Pressed Key ");
                Serial.println(customKey);

                lcd.setCursor(12,1);
                lcd.print(stato);
                lcd.setCursor(13,1);
                lcd.print(" ");
              }
          }

        while (stato == customKey){ //loop fino a che stato attuale non combacia con pressed key

          if(stato+1 > 7){ //se lo stato supera il valore massimo (7) allora
            return 7;
            stato = stato+customKey-7;
          }
            else{ //senno calcolo base
              stato = stato+1;
            }
          
          digitalWrite(pinRelCM, HIGH);
          delay(180);
          digitalWrite(pinRelCM, LOW);
          delay(180);
        }
        }
      }*/ //while (customKey)
  }
}


  else{ //variante se lampeggianti spenti

    debugPrint("IntIO OFF");

    digitalWrite(pinRelIO, LOW);

    char customKey = customKeypad.getKey();
    if(customKey){
      debugPrintKey(customKey);

      if (menuActive){
        if (customKey == '0'){
          menuActive = false;
          resetConfirmCancel();
        } else if (customKey == KEY_STEP_FORWARD){
          menuNext();
        } else if (customKey == KEY_STEP_BACK){
          menuPrev();
        } else if (customKey >= '1' && customKey <= '9'){
          if (resetConfirmStage > 0){
            if (customKey == '5'){
              resetConfirmAdvance();
            } else {
              resetConfirmCancel();
            }
          } else {
            bool closeMenu = handleMenuSelection(customKey);
            if (closeMenu){
              menuActive = false;
            }
          }
        }
      }
      else if (customKey == KEY_RESET){
        openMenu();
      }
    }

    if (menuActive){
      if (resetConfirmStage > 0 && (millis() - resetConfirmAtMs > RESET_CONFIRM_MS)){
        resetConfirmCancel();
      }
      if (resetConfirmStage > 0){
        renderResetConfirm();
      } else {
        renderMenu();
      }
    }
    else if (infoUntilMs != 0 && millis() < infoUntilMs){
      lcd.setCursor(0,0);
      printPadded(infoLine0);
      lcd.setCursor(0,1);
      printPadded(infoLine1);
    } else {
      infoUntilMs = 0;
      lcd.setCursor(0,0);
      printPadded("STATO DSL: OFF");
      lcd.setCursor(0,1);
      printPadded("DSL v1.0.3");
    }

  }

}


