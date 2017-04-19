/**
 * @file
 * @brief Little device to turn off the light (and AV) when no movment is detected. 
 * Turn on like normal (light switch) 
 */
 
/****************************************************************
* Libraries used
****************************************************************/
#include <Bounce2.h>

/****************************************************************
* Debug library + enable/disable
****************************************************************/
#define DEBUG_SKETCH
//#define DEBUG_TIME
//#define DEBUG_KICK
#include <DebugLib.h>

/****************************************************************
* Pin declarations
****************************************************************/
const byte OutputPins[2] = {3, 2}; //!<Output pins
const byte LightSwitchPin = 10; //!<Light switch pin
const byte ModeButtonPin = 12; //!<Mode button pin
const byte PirPin = A2; //!<PIR pin
const byte LedPin = A3; //!< Led pin
const byte ModeSelectorPin = 5; //!< Mode selector pin. Currently **not** used

//!Enum for easy access of OutputPins
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
//! How long wait in revive mode? (in ms)
const unsigned long ReviveTime   = 1  * 60 * 1000UL;
//!Led flash rate (if in flash) (in ms)
const unsigned int  LedFlashTime  = 200;
//!Time befor time ends to indicate time is running out (in ms)
const unsigned long WarningTime    = 5  * 60 * 1000UL;
//!Time befor time ends to indicate last change to kick the timer (should be < LedWarningTime (in ms)
const unsigned long PanicTime     = 60       * 1000UL;

#else //shorter for debuging
//!Time to keep the light on after last movement (in ms), [0] = normal, [1]= extend
const unsigned long LightTime[]   ={20 *      1000UL,
                                    60 *      1000UL};
//!Time to keep the aux on after last movement (in ms), [0] = normal, [1]= extend
const unsigned long AuxTime[]     ={20 *      1000UL,
                                    60 *      1000UL};
//! How long wait in revive mode? (in ms)
const unsigned long ReviveTime    = 5  *      1000UL;
//!Led flash rate (if in flash) (in ms)
const unsigned int  LedFlashTime  = 100;
//!Time befor time ends to indicate time is running out (in ms)
const unsigned long WarningTime    = 10 *      1000UL;
//!Time befor time ends to indicate last change to kick the timer (should be < LedWarningTime (in ms)
const unsigned long PanicTime     = 5  *      1000UL;
#endif

const unsigned int LongPressTime = 5000; //!< Time to keep the button pressed to count as a long press

/****************************************************************
* Global variables
****************************************************************/
//@{
//! Bounce objects for the inputs
Bounce lightSwitch, modeButton, modeSelector, pir;
//@}

//! Enum to make the states easy to read
enum state_t{ALL_OFF, ALL_ON, REVIVE, AUX_ONLY};
byte state = ALL_OFF; //!< State of the llight, state_t for easy access

unsigned long lastMovementMillis; //!< Last kick to the timer
bool extend = false; //!<Extend mode enabled?

//!Enum to make the led states easy to read
enum ledState_t{LED_OFF, 
                PURPLE, LIGHT_PURPLE, PURPLE_FLASH, LIGHT_PURPLE_BLUE_FLASH,
                BLUE, LIGHT_BLUE};
//!Keeps track of the led state
volatile byte ledState = LED_OFF;
//!Flash state of the led
bool ledFlash = true;

//!To keep track of the led flash time
unsigned long ledMillis;

//! Enum to easy set the floating state of digital3State()
enum digital3State_t{FLOAT = 2};

/****************************************************************
* ISR functions
****************************************************************/
/**
 * @brief Timer 2 overflow interrupt.
 * 
 * Used to do software PWM
 */
ISR(TIMER2_OVF_vect){
  static byte pwmState = true; //To remember last state set

  //flip state
  pwmState = !pwmState;

  //act accordingly
  if(pwmState){
    //Float for blue, high for purple on times
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

/****************************************************************
* Setup and loop
****************************************************************/

/**
 * @brief Default Arduino setup()
 */
void setup() {
  //Start Serial debuging
  DBEGIN(115200);
  DPRINTLN(F("Program start"));
  
  //Setup Timer 2. Normal mode and Clk/256 for a 244Hz interrupt
  TCCR2A = (TCCR2A & 0b11111100) | 0b00;
  TCCR2B = (TCCR2B & 0b11111000) | 0b110;
  
  //print Timer 2 settings to check
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
  
  //Check timer to see if state need changing
  checkTimer();
  
  // Acts on the light switch (and modeButton on it)
  checkLightSwitch();
  
  //Check if the ledState needs to change
  checkLed();

  //Update led to reflect ledState
  updateLed();
  
  //Sets the ouputs according to the state
  updateOutputs();
}

/****************************************************************
* Input functions
****************************************************************/

/**
 * @Brief Updates all inputs
 */
void updateInput(){
  lightSwitch.update();
  modeButton.update();
  modeSelector.update();
  pir.update();
}

/**
 * @brief if the given Bounce object changed state
 * 
 * @param [in] in Bounce object to check
 * 
 * @return **True** if it fell **or** rose, **false** otherwise
 * 
 */
bool bounceChanged(Bounce &in){
  return in.fell() || in.rose();
}

/**
 * @brief checks the whole light, including mode button
 * 
 * Changes the state accordingly to the actions of the light switch and mode button
 */
void checkLightSwitch(){
  static unsigned long modeButtonMillis = 0;
  
  //Act on swithcing the light switch on or off
  if(bounceChanged(lightSwitch)){
    //Debug printing
    DPRINT(F("Light switch: "));
    DPRINTLN(lightSwitch.read());
    DPRINT(F("Old state: "));
    DPRINTLN(state);

    //If the light is on, turn it off and go to AUX_ONLY
    if(state == ALL_ON){
      state = AUX_ONLY;
      //And extend the time if the mode button was pressed at that time
      if(!modeButton.read()){
        extend = true;
        DPRINTLN(F("Extend on!"));
      }

      //save modeButton time to not make it timeout directly.
      modeButtonMillis = millis();
    }
    //In every other state, turn the light on
    else{
      state = ALL_ON;

      //kick the timer because there was action
      //PIR may only see movement after switching (because of placement)
      //Prevents the unit from turning off directly
      kickTimer();

      //And reset extend if mode button was pressed at that time
      if(!modeButton.read()){
        extend = false;
        DPRINTLN(F("Extend off!"));
      }
    }
    DPRINT(F("New state: "));
    DPRINTLN(state);
  }
  
  if(modeButton.fell()){
    DPRINTF("ModeButton pressed!");
    if(state == AUX_ONLY){
      modeButtonMillis = millis();
      //If pressed exactly at 0, pretend it was 1 :D
      if(modeButtonMillis == 0){
        modeButtonMillis = 1;
      }
      DPRINTF("Set millis: ");
      DPRINTLN(modeButtonMillis);
    }
    //Set to 0 to make it do nothing
    else{
      modeButtonMillis = 0;
      DPRINTFLN("Reset");
  }
  //In AUX_ONLY a long press will turn off the AUX
  //modeButtonMillis
  else if(state == AUX_ONLY &&
    !modeButton.read() &&
    modeButtonMillis &&
    millis() - modeButtonMillis >= LongPressTime)
  {
    DPRINTFLN("Long modeButton press, all off!");
    state = ALL_OFF;
  }
}

/****************************************************************
* Output functions
****************************************************************/

/**
 * @brief set outputs according to state
 */
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

/**
 * @brief 3-state write functions
 * 
 * Makes it possible to switch between the 3-state options (LOW, HIGH, FLoAT) of a pin
 * 
 * @param [in] pin Pin to set
 * @param [in] mode Mode to set the pin at
 */
void digital3State(byte pin, byte mode){
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

/**
 * @Brief Toggles the pin
 * 
 * @Note Like digitalWrite(), be sure the mode is set correct.
 */
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

/****************************************************************
* Timer functions
****************************************************************/

void kickTimer(){
  #if defined(DEBUG_KICK)
  if(millis() - lastMovementMillis >= 1000){
  #endif
  lastMovementMillis = millis();

  if(state == REVIVE){
    state = ALL_ON;
  }
  DPRINT(F("Timer kicked! "));
  DPRINTLN(lastMovementMillis);
  #if defined(DEBUG_KICK)
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
    DPRINT(F("Aux time, new state: "));
    DPRINTLN(state);
  }
  
  //ALL_OFF will always reset extend mode
  if(state == ALL_OFF){
    extend = false;
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
