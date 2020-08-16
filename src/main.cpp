#include <Arduino.h>

#include <ESP8266WiFi.h>
#include <DNSServer.h>  
#include <ESP8266WebServer.h>
#include <ArduinoOTA.h>

#include <ArduCastControl.h>

#include <WiFiManager.h>

#include <U8g2lib.h>
#ifdef U8X8_HAVE_HW_I2C
#include <Wire.h>
#endif

#include <OneButton.h>

//uncommenting U8G2_16BIT in u8g2.h IS REQUIRED

//Small screen
U8G2_SSD1306_128X64_NONAME_F_SW_I2C u8g2(U8G2_R0, D1, D2, U8X8_PIN_NONE);
//big screen
//U8G2_SH1106_128X64_NONAME_F_SW_I2C u8g2(U8G2_R0, D1, D2, U8X8_PIN_NONE);
#define DEFAULTFONT u8g2_font_t0_13_tf
#define SCROLLWIDTH 3*6
#define LINE1 10
#define LINE2 LINE1+16
#define LINE3 LINE2+16
#define LINE4 LINE3+16
//#define DRAWVOLUME //enable it for a single line on the right side of the screen representing volume


WiFiManager wifiManager;
WiFiManagerParameter ccIpManager("ccip", "IP of chromecast", "192.168.1.12", 20);

#define LONGPRESS 500
#define B_SELECT D5
#define B_LEFT D6
#define B_RIGHT D7

OneButton bSelect(B_SELECT, true);
OneButton bLeft(B_LEFT, true);
OneButton bRight(B_RIGHT, true);
void pauseFunction();
void prevFunction();
void nextFunction();
void rewFunction();
void ffwFunction();
void seekFunction();
void rewOneFunction();
void ffwOneFunction();


ArduCastControl cc = ArduCastControl();


void setup() {
  wifiManager.addParameter(&ccIpManager);
  wifiManager.autoConnect("AutoConnectAP");

  ArduinoOTA.setHostname ("chromecastremote");
  ArduinoOTA.begin();

  pinMode(D8, OUTPUT); //common pin for keys, used for pulldown - should have a pulldown anyway

  bSelect.attachClick(pauseFunction);

  bLeft.attachDoubleClick(prevFunction);
  bRight.attachDoubleClick(nextFunction);

  bLeft.attachClick(rewOneFunction);
  bRight.attachClick(ffwOneFunction);
  
  bLeft.attachDuringLongPress(rewFunction);
  bRight.attachDuringLongPress(ffwFunction);
  
  bLeft.attachLongPressStop(seekFunction);
  bRight.attachLongPressStop(seekFunction);
  bLeft.setPressTicks(LONGPRESS);
  bRight.setPressTicks(LONGPRESS);

  u8g2.begin();
  u8g2.setFont(DEFAULTFONT);
}

// ============== screen related code =============
bool blinkOn = true;
int32_t line1Offset = 0;
int32_t line2Offset = 0;
int32_t line3Offset = 0;
uint32_t line1Len = 0;
uint32_t line2Len = 0;
uint32_t line3Len = 0;
bool line1Back = false;
bool line2Back = false;
bool line3Back = false;

int32_t updateScroll(int32_t offset, uint32_t len, bool *direction){
#ifdef DRAWVOLUME
  #define MAXX 127
#else
  #define MAXX 128
#endif
  if ( len > MAXX ){
    if ( *direction && (len + offset) <= MAXX - SCROLLWIDTH/2 ){
      *direction = false;
    }
    if ( !*direction && offset >= SCROLLWIDTH/2 ){
      *direction = true;
    }

    if (*direction){
      return offset - SCROLLWIDTH;
    } else {
      return offset + SCROLLWIDTH;
    }
  } else {
    return 0;
  }
}

void updateScreen(bool bumpPhase, float seek){
  if ( bumpPhase ){
    blinkOn = !blinkOn;
    line1Offset = updateScroll(line1Offset, line1Len, &line1Back);
    line2Offset = updateScroll(line2Offset, line2Len, &line2Back);
    line3Offset = updateScroll(line3Offset, line3Len, &line3Back);
  }

#ifdef DRAWVOLUME
  bool volumeDraw = false;
#endif
  bool seekDraw = false;
  bool stringsDraw = false;

  connection_t cs = cc.getConnection();
  if ( cs == WAIT_FOR_RESPONSE || cs == CONNECT_TO_APPLICATION ) //we shouldn't run, let's return
    return;
  u8g2.clearBuffer();
  if ( cs == DISCONNECTED ){
    u8g2.drawStr(18, 40, "Connecting");
  } else if (cs != APPLICATION_RUNNING){
    u8g2.drawStr(15, 40, "Not playing");
#ifdef DRAWVOLUME
    volumeDraw = true;
  } else {
    volumeDraw = true;
#else
  } else {
#endif
    if ( cc.duration != 0 ){
      seekDraw = true;
    }
    if ( cc.statusText[0] != '\0'){
      stringsDraw = true;
    }
  }
  if ( stringsDraw ){
    line1Len = u8g2.drawUTF8(line1Offset, LINE1, cc.displayName);
    if ( cc.title[0] != '\0') { //two line mode
      line2Len = u8g2.drawUTF8(line2Offset, LINE2, cc.statusText);
    } else { //four line mode
      line2Len = u8g2.drawUTF8(line2Offset, LINE2, cc.artist);
      line3Len = u8g2.drawUTF8(line3Offset, LINE3, cc.title);
    }
  }
  #ifdef DRAWVOLUME
  if ( volumeDraw && (!cc.isMuted || blinkOn)){
    float len = cc.volume * 63;
    u8g2.drawVLine(127, 63-(int)len, len);
  }
  #endif
  if ( seekDraw ){
    float len;
    if (seek < 0 ){
      seek = cc.currentTime;
    }

    char buffer[30];
    int second = (int)cc.duration;
    int ssecond = (int)seek;
    int minute = second/60;
    int sminute = ssecond/60;
    second %= 60;
    ssecond %= 60;

    if (minute >= 60){
      int hour = minute/60;
      int shour = sminute/60;
      minute %= 60;
      sminute %= 60;
      snprintf(buffer, sizeof(buffer), "%d:%02d:%02d/%d:%02d:%02d", hour, minute, second, shour, sminute, ssecond);
    } else {
      snprintf(buffer, sizeof(buffer), "%d:%02d / %d:%02d", minute, second, sminute, ssecond);
    }
    u8g2.drawStr(0, LINE4, buffer);
    len = seek * 128 / cc.duration;
    u8g2.drawHLine(0, 63, (int)len);
  }
  u8g2.sendBuffer();
}


// ================= button related code =====================
float seekTo = -1;
uint16_t seekList[] = {5, 15, 30, 60, 120, 180, 300};
#define MAXSEEKINDEX 5

void pauseFunction(){
  cc.pause(true);
}

void prevFunction(){
  cc.prev();
}

void nextFunction(){
  cc.next();
}

uint32_t seekCalc(OneButton &button, bool single){
  int16_t seekIndex, maxSeekMultiplier = 0;
  uint32_t seekAmount;
  if ( single ){
    seekIndex = 1;
  } else {
    seekIndex = button.getPressedTicks()/LONGPRESS;
  }

  maxSeekMultiplier = seekIndex - MAXSEEKINDEX - 1;
  if ( maxSeekMultiplier > 0 ){
    seekAmount = maxSeekMultiplier * seekList[MAXSEEKINDEX]; 
  } else {
    seekAmount = seekList[seekIndex-1]; 
  }

  return seekAmount;
}

void rewGlobal(bool single){
  seekTo = cc.currentTime - seekCalc(bLeft, single);
  if ( seekTo < 0 )
    seekTo = 0;
}

void ffwGlobal(bool single){
  seekTo = cc.currentTime + seekCalc(bRight, single);
  if ( seekTo > cc.duration )
    seekTo = cc.duration;
}

void rewFunction(){
  rewGlobal(false);
}

void ffwFunction(){
  ffwGlobal(false);
}

void rewOneFunction(){
  rewGlobal(true);
  seekFunction();
}

void ffwOneFunction(){
  ffwGlobal(true);
  seekFunction();
}

void seekFunction(){
  cc.seek(false, seekTo);
  seekTo = -1;
}

//============== main loop ===================

unsigned long lastUpdated = 0;
unsigned long updatePeriod = 5000;

unsigned long screenPhaseChanged = 0;
unsigned long screenPhasePeriod = 500;


void loop() {
  bool refreshScreen = false;
  ArduinoOTA.handle();
  //wait for 5s to boot - this is useful in case of a bootloop to keep OTA running
  if ( millis() < 5000 )
    return;
  
  //chromecast loop. Connect, update and change period on status
  if ( millis() - lastUpdated > updatePeriod ) {
    if ( cc.getConnection() == DISCONNECTED ){
      cc.connect(ccIpManager.getValue());
    } else {
      connection_t c = cc.loop();
      if ( c == WAIT_FOR_RESPONSE || c == CONNECT_TO_APPLICATION ){
        updatePeriod = 50; 
      } else if ( c == APPLICATION_RUNNING ){
        refreshScreen = true;
        updatePeriod = 500;
      } else {
        refreshScreen = true;
        updatePeriod = 5000;
      }
    }
    lastUpdated = millis();
  }

  //button loop. Only run it if an application is running
  if ( cc.getConnection() == APPLICATION_RUNNING ){
    bSelect.tick();
    bLeft.tick();
    bRight.tick();
    if ( bLeft.isLongPressed() || bRight.isLongPressed() ){ //update seek display
      refreshScreen = true;
    }
  }
  
  //screen loop. Update it when needed for blinking/scrolling
  if ( millis() - screenPhaseChanged > screenPhasePeriod ){
    screenPhaseChanged = millis();
    updateScreen(true, seekTo);
  } else if(refreshScreen) {
    updateScreen(false, seekTo);
    //cc.dumpStatus();
  }
  refreshScreen = false;
}
