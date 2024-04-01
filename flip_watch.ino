
// Libs used
#include "Arduino.h"
#include <GxEPD2_BW.h>
#include <GxEPD2_3C.h>
#include <GxEPD2_4C.h>
#include <GxEPD2_7C.h>
#include <Preferences.h>
#include <ESP32Time.h>

// Display and font
#include "GxEPD2_display_selection_new_style.h"
#include "GxEPD2_display_selection.h"
#include "GxEPD2_display_selection_added.h"
#include "Orbitron_Medium_30.h"

// Defines for pins
#define LTOUCH 14
#define RTOUCH 32
#define TOUCHTHRESHOLD 40
#define TOUCHWAIT 500
#define LEFT 0
#define RIGHT 1
// Define for LED
#define L1R 0
#define L1G 2
#define L1B 15

#define SLEEPTIME 300000

#define DEBUG true

RTC_DATA_ATTR int bootCount = 0;


// Operation Modes
enum opMode{
  BOOT, // This state is the intial one, the system is not yet ready and will not interact with the user
  IDLE, // This state is the default state after boot and when neither a countdown is finished nor running
  RUN, // This state is indicating that a timer countdown is running
  SLEEP // The system is in sleep mode 
};

// Storage instance
Preferences preferences;
const char keyTimeValue[] = "timeValue";

// Realtime clock instance and settings
ESP32Time rtc(0); // Running in UTC
const uint epochStart = 660394800; // 05.12.1990 12:00:00 <3


//--- Runtime state ---

// Timer value
unsigned short timeValue = 5;

// Current system state
opMode mode = BOOT;
bool handleTouchLeft = false;
bool handelTouchRight = false;
unsigned long touchWaits[2];
short touchCounter[2];
unsigned long ledTimer = 0;
bool rotationTop = false;
unsigned long counterStart = 0;
unsigned short runningTime = 0;
//---------------------

// Interrupts callbacks
void lTouchIntr(){
  // if the value is here not checked again you get false triggers
  if(touchRead(LTOUCH) < TOUCHTHRESHOLD)
    handleTouchLeft = true;
}

void rTouchIntr(){
  // if the value is here not checked again you get false triggers
  if(touchRead(RTOUCH) < TOUCHTHRESHOLD)
    handelTouchRight = true;
}


void setup() {
  ++bootCount;
  Serial.begin(115200);
  delay(1000);
  Serial.println("Boot number: " + String(bootCount));
  print_wakeup_reason();
  rtc.setTime(epochStart); 
  preferences.begin("flipwatch", false);
  setupState();
  setupDisplay();
  setupInOut();
  setupTouch();
  mode = IDLE;
  Serial.println("setup done");  
}

// Setup the state during boot
void setupState(){
  timeValue = preferences.getUShort(keyTimeValue,5);
  // Reset timer
  touchWaits[0] = millis();
  touchWaits[1] = millis();
  touchCounter[0] = 0;
  touchCounter[1] = 0;
}

void setupDisplay(){
  display.init(115200, true, 2, false); 
  display.setRotation(2); // TODO Read rotation sensor and decide whats correct
  showNumber(timeValue);
}

void setupTouch(){
  touchAttachInterrupt(RTOUCH, rTouchIntr, TOUCHTHRESHOLD);
  touchAttachInterrupt(LTOUCH, lTouchIntr, TOUCHTHRESHOLD);
}

void setupInOut(){
  // Touch sensors dont need a pinMode
  pinMode(L1R, OUTPUT);
  pinMode(L1G, OUTPUT);
  pinMode(L1B, OUTPUT);
  pinMode(LTOUCH,INPUT);
  pinMode(RTOUCH,INPUT);
  //TODO Real rotation sensor needs to be here
  pinMode(27,INPUT);
  ledOff();
  touchSleepWakeUpEnable(LTOUCH,TOUCHTHRESHOLD);
  touchSleepWakeUpEnable(RTOUCH,TOUCHTHRESHOLD);
}

void loop() {
  checkLedTimer();
  handleTouchIntr(); 
  resetButtonCounter();
  if(touchRead(27) < 40 && mode == IDLE){
    // TODO handle rotation here for both sensor
    // Touch simulates sensor going from 0 -> 1
    if(rotationTop == false){
        rotationTop = true;
        startTimer();
    }
  }

  if(mode == RUN){
    unsigned long minute = 60;
    unsigned short delta = (unsigned short)((rtc.getEpoch() - counterStart) / minute);
    if( timeValue - delta != runningTime){
      runningTime = timeValue - delta;
      showNumber(runningTime);
    }
    if(runningTime == 0){
      mode = IDLE;
      ledBlue();
      touchWaits[LEFT] = millis(); // Prevent sleeping directly
    }
  }

  if(mode == IDLE){
    if( touchWaits[LEFT] > touchWaits[RIGHT]){
       if(millis() -  touchWaits[LEFT] > SLEEPTIME){
          mode = SLEEP;
          sleep();
       }
    }
    else{
      if(millis() - touchWaits[RIGHT] > SLEEPTIME){
         mode = SLEEP;
         sleep();
       }
    }
  }
}

void sleep(){
  clearDisplay();
  ledOff();
  Serial.println("Going to sleep now");
  esp_deep_sleep_start();
}

void startTimer(){
   // Turning
   runningTime = timeValue;
   display.setRotation(0); //TODO make this depend on the rotation switch
   showNumber(runningTime);
   mode = RUN;
   counterStart = rtc.getEpoch();
   
}



//----      Display control     ------

void clearDisplay(){
  display.setFullWindow();
  display.firstPage();
  do
  {
    display.fillScreen(GxEPD_WHITE);
  }
  while (display.nextPage());
  display.hibernate();
}

void showNumber(int number){
  char buffer[4];
  itoa(number, buffer, 10);
  display.setFont(&Orbitron_Medium_60);
  display.setTextColor(GxEPD_BLACK);
  display.setTextSize(2);
  int16_t tbx, tby; uint16_t tbw, tbh;
  display.getTextBounds(buffer, 0, 0, &tbx, &tby, &tbw, &tbh);
  // center the bounding box by transposition of the origin:
  uint16_t x = ((display.width() - tbw) / 2) - tbx;
  uint16_t y = ((display.height() - tbh) / 2) - tby;
  display.setFullWindow();
  display.firstPage();
  do
  {
    display.fillScreen(GxEPD_WHITE);
    display.setCursor(x, y);
    display.print(buffer);
  }
  while (display.nextPage());
  display.hibernate();
}
//------------------------------------

//------- Handle inputs --------------

void handleTouchIntr(){
  if(mode == BOOT) return;
  unsigned long now = millis();
  if(handelTouchRight == false && handleTouchLeft == false) {
    return;
  };
  int index = handleTouchLeft ? LEFT : RIGHT;

  // If now - lastValue for index > 500 allow touch
  if(now - touchWaits[index] < TOUCHWAIT) return;
  touchWaits[index] = now;
  if(mode == IDLE){
    if(handleTouchLeft){
      decTimer(); // dec is index 0
    }
    else{
      incTimer(); // inc is index 1
    }
  }
  handleTouchLeft = false;
  handelTouchRight = false;
}

void resetButtonCounter(){
  unsigned long now = millis();
  if(touchCounter[LEFT] != 0 && (now - touchWaits[LEFT]) > (TOUCHWAIT*3)){
    touchCounter[LEFT] = 0;
    debugPrint("reset LEFT counter");
  }
  if(touchCounter[RIGHT] != 0 && (now - touchWaits[RIGHT]) > (TOUCHWAIT*3)){
    touchCounter[RIGHT] = 0;
    debugPrint("reset RIGHT counter");
  }
}

void incTimer(){
  if(timeValue < 99){
    touchCounter[LEFT] = 0;
    touchCounter[RIGHT] += 1;
    timeValue = min(99, timeValue + getTimerMod(touchCounter[RIGHT])); 
    preferences.putUShort(keyTimeValue, timeValue);
    turnOnFor(0,255,0,400);
    showNumber(timeValue);
  }
}

unsigned short getTimerMod(short counter){
    if(counter > 5){
      return 5;
    }
    if(counter > 3){
      return 2;
    }
    else{
      return 1;
    }
}

void decTimer(){
  if(timeValue > 1){
    touchCounter[LEFT] += 1;
    touchCounter[RIGHT] = 0;
    ushort change = getTimerMod(touchCounter[LEFT]);
    // Otherwise it would overflow cant use max here
    if(timeValue < change) change = 1;

    timeValue = timeValue - change; 
    preferences.putUShort(keyTimeValue, timeValue);
    turnOnFor(255,0,0,400);
    showNumber(timeValue);
  }
}

//------------------------------------

//---- Notification LED control ------

// Turn of the notification LED
void ledOff(){
  setColor(0,0,0);
}

// Turn the notification LED to red
void ledRed(){
  setColor(255,0,0);
}

// Turn the notification LED to green
void ledGreen(){
  setColor(0,255,0);
}

// Turn the notification LED to blue
void ledBlue(){
  setColor(0,0,255);
}

void turnOnFor(int red, int green,  int blue, unsigned long durationMills){
  ledTimer = millis() + durationMills;
  setColor(red,green,blue);
}

void checkLedTimer(){
  if(ledTimer != 0){
    if(millis() > ledTimer){
      ledOff();
      ledTimer = 0;
    }
  }
}

// Turn the notifiaction LED to the given color code
void setColor(int redValue, int greenValue,  int blueValue) {
  analogWrite(L1R, redValue);
  analogWrite(L1G,  greenValue);
  analogWrite(L1B, blueValue);
  // TODO add the other LEDS here too
}

//------------------------------------


void debugPrint(const char str[]){
  if(DEBUG){
    Serial.println(str);    
  }
}

void print_wakeup_reason(){
  esp_sleep_wakeup_cause_t wakeup_reason;

  wakeup_reason = esp_sleep_get_wakeup_cause();

  switch(wakeup_reason)
  {
    case ESP_SLEEP_WAKEUP_EXT0 : Serial.println("Wakeup caused by external signal using RTC_IO"); break;
    case ESP_SLEEP_WAKEUP_EXT1 : Serial.println("Wakeup caused by external signal using RTC_CNTL"); break;
    case ESP_SLEEP_WAKEUP_TIMER : Serial.println("Wakeup caused by timer"); break;
    case ESP_SLEEP_WAKEUP_TOUCHPAD : Serial.println("Wakeup caused by touchpad"); break;
    case ESP_SLEEP_WAKEUP_ULP : Serial.println("Wakeup caused by ULP program"); break;
    default : Serial.printf("Wakeup was not caused by deep sleep: %d\n",wakeup_reason); break;
  }
}
