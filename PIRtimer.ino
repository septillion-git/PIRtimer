
/****************************************************************
* Libraries used
****************************************************************/
#include <Bounce2.h>

/****************************************************************
* Debug library + enable/disable
****************************************************************/
#define DEBUG_SKETCH
#define DEBUG_TIME
#include <DebugLib.h>

/****************************************************************
* Pin declarations
****************************************************************/
const byte OutputPins[2] = {3, 2};
const byte LightSwitchPin = 5; //10
const byte ModeButtonPin = 12;
const byte PirPin = A2;
const byte LedPin = A3;
const byte ModeSelectorPin = 5;

enum output_t {LIGHT, AUX};

/****************************************************************
* Timing settings
****************************************************************/
#ifndef DEBUG_TIME //normal operation
//!Time to keep the light on after last movement (in ms), [0] = normal, [1]= extend
const unsigned long LightTime[]  ={15 * 60 * 1000UL,
                                   2  * 60 * 60 * 1000UL};
//!Time to keep the aux on after last movement (in ms), [0] = normal, [1]= extend
const unsigned long AuxTime[]    ={15 * 60 * 1000UL,
                                   2  * 60 * 60 * 1000UL};
const unsigned long ReviveTime   = 1  * 60 * 1000UL; //!< How long wait in revive mode? (in ms)
//!Led flash rate (if in flash)
const unsigned int  LedFlashTime  = 200;
//!Time befor time ends to indicate time is running out
const unsigned long WarningTime    = 5  * 60 * 1000UL;
//!Time befor time ends to indicate last change to kick the timer (should be < LedWarningTime
const unsigned long PanicTime     = 30      * 1000UL;

#else //shorter for debuging
const unsigned long LightTime[]   ={20 *      1000UL,
                                    60 *      1000UL};
const unsigned long AuxTime[]     ={20 *      1000UL,
                                    60 *      1000UL};
const unsigned long ReviveTime    = 5  *      1000UL;
//!Led flash rate (if in flash) (in ms)
const unsigned int  LedFlashTime  = 100;
//!Time befor time ends to indicate time is running out
const unsigned long WarningTime    = 10 *      1000UL;
//!Time befor time ends to indicate last change to kick the timer (should be < LedWarningTime
const unsigned long PanicTime     = 5  *      1000UL;
#endif

/****************************************************************
* Global variables
****************************************************************/
Bounce lightSwitch, modeButton, modeSelector, pir;

enum state_t{ALL_OFF, ALL_ON, REVIVE, AUX_ONLY};
byte state = ALL_OFF; //!< State of the llight, state_t for easy access

unsigned long lastMovementMillis; //!< Last kick to the timer
bool extend = false; //!<Extend mode enabled?

enum ledState_t{LED_OFF, 
                PURPLE, LIGHT_PURPLE, PURPLE_FLASH, LIGHT_PURPLE_BLUE_FLASH,
                BLUE, LIGHT_BLUE};
volatile byte ledState = LED_OFF;
volatile bool ledFlash = true;
unsigned long ledMillis;

unsigned long tempMillis;

enum digitalFloat_t{FLOAT = 2};

ISR(TIMER2_OVF_vect){
  static byte pwmState = true;

  pwmState = !pwmState;
  
  if(pwmState){
    if(ledState == LIGHT_BLUE){
      digital3State(LedPin, FLOAT);
    }
    else{
      digital3State(LedPin, HIGH);
    }
  }
  else{
    digital3State(LedPin, LOW);
  }
}

void setup() {
  //Start Serial debuging
  DBEGIN(115200);
  DPRINTLN(F("Program start"));

  TCCR2A = (TCCR2A & 0b11111100) | 0b00;
  TCCR2B = (TCCR2B & 0b11111000) | 0b110;

  DPRINT(F("TCCR2A: 0b"));
  DPRINTLN(TCCR2A, BIN);
  DPRINT(F("TCCR2B: 0b"));
  DPRINTLN(TCCR2B, BIN);
  DPRINT(F("TIMSK2: 0b"));
  DPRINTLN(TIMSK2, BIN);
  
  //Setup the output
  for(byte i = 0; i < sizeof(OutputPins); i++){
    //active LOW so first make them HIGH to prevent becoming a LOW output
    digitalWrite(OutputPins[i], HIGH);
    pinMode(OutputPins[i], OUTPUT);
  }

  //setup inputs
  lightSwitch.attach(LightSwitchPin, INPUT_PULLUP);
  modeButton.attach(ModeButtonPin, INPUT_PULLUP);
  modeSelector.attach(ModeSelectorPin, INPUT_PULLUP);
  pir.attach(PirPin, INPUT_PULLUP);
}

void loop() {
  // Update all the inputs to use in this loop
  updateInput();
  
  //If there is movement we kick the timer
  if(pir.read()){
    kickTimer();
  }

  checkTimer();
  
  // Acts on the light switch (and modeButton on it)
  checkLightSwitch();

  checkLed();
  
  updateLed();
  
  //Sets the ouputs according to the state
  updateOutputs();
}

void updateInput(){
  lightSwitch.update();
  modeButton.update();
  modeSelector.update();
  pir.update();
}

bool bounceChanged(Bounce &in){
  return in.fell() || in.rose();
}

void checkLightSwitch(){
  if(bounceChanged(lightSwitch)){
    DPRINT(F("Light switch: "));
    DPRINTLN(lightSwitch.read());
    
    DPRINT(F("Old state: "));
    DPRINTLN(state);
    if(state == ALL_ON){
      state = AUX_ONLY;
      if(!modeButton.read()){
        extend = true;
        DPRINTLN(F("Extend on!"));
      }
    }
    else{
      state = ALL_ON;
      kickTimer();
      if(!modeButton.read()){
        extend = false;
        DPRINTLN(F("Extend off!"));
      }
    }
    DPRINT(F("New state: "));
    DPRINTLN(state);
  }
}

void updateOutputs(){
  if(state == ALL_ON){
    digitalWrite(OutputPins[LIGHT], LOW);
    digitalWrite(OutputPins[AUX], LOW);
  }
  else if(state == ALL_OFF){
    digitalWrite(OutputPins[LIGHT], HIGH);
    digitalWrite(OutputPins[AUX], HIGH);
  }
  else{
    digitalWrite(OutputPins[LIGHT], HIGH);
    digitalWrite(OutputPins[AUX], LOW);
  }
}

  switch(mode){
    case 0:
      digitalWrite(pin, LOW);
      pinMode(pin, OUTPUT);
      break;
    case 1:
      pinMode(pin, OUTPUT);
      digitalWrite(pin, HIGH);
      break;
    case 2:
      pinMode(pin, INPUT);
      break;
  }
}

void kickTimer(){
  #if defined(DEBUG_SKETCH)
  if(millis() - lastMovementMillis >= 1000){
  #endif
  lastMovementMillis = millis();

  if(state == REVIVE){
    state = ALL_ON;
  }
  DPRINT(F("Timer kicked! "));
  DPRINTLN(lastMovementMillis);
  #if defined(DEBUG_SKETCH)
  }
  #endif
}

void checkTimer(){
  const unsigned long MillisNow = millis();
  
  if(state == ALL_ON && MillisNow - lastMovementMillis >= LightTime[extend]){
    state = REVIVE;
    DPRINT(F("Light time, new state: "));
    DPRINTLN(state);
  }
  else if(state == REVIVE && MillisNow - lastMovementMillis >= (LightTime[extend] + ReviveTime)){
    state = AUX_ONLY;
    DPRINT(F("Revive time, new state: "));
    DPRINTLN(state);
  }
  else if(state == AUX_ONLY && MillisNow - lastMovementMillis >= AuxTime[extend]){
    state = ALL_OFF;
    extend = false;
    DPRINT(F("Aux time, new state: "));
    DPRINTLN(state);
  }
}

inline void digitalToggle(byte p){
  digitalWrite(p, !digitalRead(p));
}

void updateLed(){
  static byte ledStateSet = -1;
  //Set flash flag if necessary
  if(millis() - ledMillis >= LedFlashTime){
    ledMillis = millis();
    ledFlash = !ledFlash;

    if(ledState == LIGHT_PURPLE_BLUE_FLASH){
      if(ledFlash){
        digital3State(LedPin, FLOAT);
        setOverflowInterruptTimer2(false);
      }
      else{
        setOverflowInterruptTimer2(true);
      }
    }
    else if(ledState == PURPLE_FLASH){
      digital3State(LedPin, ledFlash);
    }
  }

  if(ledStateSet != ledState){
    ledStateSet = ledState;

    //select base color
    if(ledState == LED_OFF){
      digital3State(LedPin, LOW);
    }
    else if(ledState >= PURPLE &&
            ledState <= LIGHT_PURPLE_BLUE_FLASH)
    {
      digital3State(LedPin, HIGH);
    }
    else{
      digital3State(LedPin, FLOAT);
    }

    //light state?
    if( ledState == LIGHT_PURPLE ||
        ledState == LIGHT_PURPLE_BLUE_FLASH ||
        ledState == LIGHT_BLUE)
    {
      ///enable interrupts for software PWM
      setOverflowInterruptTimer2(true);
    }
    else{
      setOverflowInterruptTimer2(false);
    }
    
    //if it's a flashing mode, turn the flash on directly
    if( ledState == PURPLE_FLASH ||
        ledState == LIGHT_PURPLE_BLUE_FLASH)
    {
      ledFlash = true;
      ledMillis = millis();
    }
  }
}

void checkLed(){
  const unsigned long MillisNow = millis();
  byte newLedState;

  if(state == ALL_OFF){
    if(extend){
      newLedState = LIGHT_PURPLE;
    }
    else{
      newLedState = LED_OFF;
    }
  }
  else if( MillisNow - lastMovementMillis >= (LightTime[extend] - PanicTime) ||
      MillisNow - lastMovementMillis >= (AuxTime[extend] - PanicTime) )
  {
    newLedState = PURPLE_FLASH;
  }
  else if( MillisNow - lastMovementMillis >= (LightTime[extend] - WarningTime) ||
      MillisNow - lastMovementMillis >= (AuxTime[extend] - WarningTime) )
  {
    newLedState = PURPLE;
  }
  else if(extend){
    newLedState = LIGHT_PURPLE;
  }
  else if(state == AUX_ONLY){
    newLedState = LIGHT_BLUE;
  }
  else{
    newLedState = LED_OFF;
  }

  if(ledState != newLedState){
    ledState = newLedState;
    DPRINT(F("Led state changed! New: "));
    DPRINTLN(ledState);
  }
}

void setOverflowInterruptTimer2(bool s){
  if(s){
    TIMSK2 |= _BV(TOIE2);
  }
  else{
    TIMSK2 &= !_BV(TOIE2);
  }
}
