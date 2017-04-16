
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
#ifndef DEBUG_TIME
const unsigned long LightTime    = 15  * 60 * 1000UL;
const unsigned long AuxTime      = 15  * 60 * 1000UL;
const unsigned long ReviveTime   = 1   * 60 * 1000UL;
const unsigned long WarningTime  = 3   * 60 * 1000UL;
const unsigned long PanicTime    = 1   * 60 * 1000UL;

const unsigned long LightExtendedTime  = 2 * 60 * 60 * 1000UL;
const unsigned long AuxExtendedTime    = 2 * 60 * 60 * 1000UL;
#else
const unsigned long LightTime    = 30 *      1000UL;
const unsigned long AuxTime      = 30 *      1000UL;
const unsigned long ReviveTime   = 10 *      1000UL;
const unsigned long WarningTime  = 15 *      1000UL;
const unsigned long PanicTime    = 5  *      1000UL;

const unsigned long LightExtendedTime  = 2 * 60 * 60 * 1000UL;
const unsigned long AuxExtendedTime    = 2 * 60 * 60 * 1000UL;
#endif

/****************************************************************
* Global variables
****************************************************************/
Bounce lightSwitch, modeButton, modeSelector, pir;

enum state_t{ALL_OFF, ALL_ON, REVIVE, AUX_ONLY};
byte state = ALL_OFF; //!< State of the llight, state_t for easy access

unsigned long lastMovementMillis; //!< Last kick to the timer
bool extend = false;

ISR(TIMER2_OVF_vect){
  digitalToggle(13);
}

void setup() {
  Serial.begin(115200);
  DPRINTLN(F("Program start"));

  TCCR2A = (TCCR2A & 0b11111100) | 0b00;
  TCCR2B = (TCCR2B & 0b11111000) | 0b110;
  TIMSK2 |= _BV(TOIE2);

  DPRINT(F("TCCR2A: 0b"));
  DPRINTLN(TCCR2A, BIN);
  DPRINT(F("TCCR2B: 0b"));
  DPRINTLN(TCCR2B, BIN);
  DPRINT(F("TIMSK2: 0b"));
  DPRINTLN(TIMSK2, BIN);
  pinMode(13, OUTPUT);
  
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
      }
    }
    else{
      state = ALL_ON;
      kickTimer();
      if(!modeButton.read()){
        extend = false;
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

void digitalFloat(byte pin, byte mode){
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
  lastMovementMillis = millis();

  if(state == REVIVE){
    state = ALL_ON;
  }
  DPRINT(F("Timer kicked! "));
  DPRINTLN(lastMovementMillis);
}

void checkTimer(){
  const unsigned long MillisNow = millis();

  if(state == ALL_ON && MillisNow - lastMovementMillis >= LightTime){
    state = REVIVE;
    DPRINT(F("Light time, new state: "));
    DPRINTLN(state);
  }
  else if(state == REVIVE && MillisNow - lastMovementMillis >= (LightTime + ReviveTime)){
    state = AUX_ONLY;
    DPRINT(F("Revive time, new state: "));
    DPRINTLN(state);
  }
  else if(state == AUX_ONLY && MillisNow - lastMovementMillis >= AuxTime){
    state = ALL_OFF;
    extend = false;
    DPRINT(F("Aux time, new state: "));
    DPRINTLN(state);
  }
}

inline void digitalToggle(byte p){
  digitalWrite(p, !digitalRead(p));
}
