//-------------------
//  CUSTOM SETTINGS
//-------------------

//#define debugMode           //Uncomment this to turn on debugging mode
#define logoOnBoot          //Uncomment this to turn on logo on boot
#define kalmanFilter        //Uncomment this to turn on kalman filtration of sensor data.
//#define averageReadings     //Uncomment this to turn on sample collection between frames and to show an averaged value 
#define useSteinhart3       //Uncomment this to calculate temps using the Steinhart-Hart 3-coefficient equation rather than the simpler beta equation
#define calibratedADC       //Uncomment this to read the sensor with analogReadMilliVolts() (applies the chip's factory ADC calibration) and average several reads per sample.
                            //Usually recommended to keep enabled unless it causes problems.
                            //Comment it out for plain analogRead(). NOTE: the two give slightly different resistances, so re-check readings against a known temperature after switching
#define OTAUpdates          //Uncomment this to enable OTA firmware updating over wifi.
                            //This is really convenient, but really REALLY not secure...
                            //Consider changing your chosen wifi name and password
                            //Please remember that connecting to an updating network will block ADC2,
                            //thus making it impossible to read temperatre and dimming


#define MAX_IMAGE_WIDTH 240       //Used for the PNG library. Adjust according to screen used
#define screenRefreshPeriod 3000  //How often the screen shows new values
#define adcSamples 16             //Only matters if calibratedADC is enabled. How many ADC reads are averaged into one sample
#define sampleTableSize 100       //Only matters if averageReadings is enabled. Sets how many samples to collect between frames. Safe value is 100, but don't go above screenRefreshPeriod / 10
#define needleFramePeriod 33      //Minimum ms between needle redraws (~30 fps). The screen is only redrawn while the needle is actually moving
#define needleSmoothing 250.0f    //Needle easing time constant in ms. Higher = lazier/heavier needle, lower = snappier

//IN/OUT Pins
#define sensePin 32   //Temp sensor pin
#define dimmingPin 34 //Dimming sense pin

//-------------------
//     LIBRARIES
//-------------------
#ifdef kalmanFilter
  #include "SimpleKalmanFilter.h" //Kalman filter for noise filtration
#endif

#ifdef logoOnBoot
  #include <PNGdec.h>   //Decoding PNGs
  #include "logo.h"     //PNG in question
#endif

#ifdef OTAUpdates
  #include <WiFi.h>
  #include <WiFiClient.h>
  #include <WebServer.h>
  #include <ESPmDNS.h>
  #include <Update.h>
  #include "esp_wifi.h"
  #include "WebPages.h"
  #include "wifi_passes.h"
#endif

#include <Arduino.h>            //Needed for PlatformIO
#include <cmath>                //isnan, lround
#include <SPI.h>                //SPI comms
#include <TFT_eSPI.h>           //TFT LCD library

//-------------------
//  OBJECTS CRETION
//-------------------
TFT_eSPI tft = TFT_eSPI(240, 240);                  // Create screen object of size 240x240
TFT_eSprite screenBufferSprite = TFT_eSprite(&tft); // Sprite as a screen buffer

#ifdef kalmanFilter
  //This config seems good enough, never bothered to tune it properly tho
  SimpleKalmanFilter simpleKalmanFilter(3, 3, 0.01);  // Kalman filter object
#endif

#ifdef OTAUpdates
  WebServer server(80);
#endif

//-------------------
// CUSTOMIZABLE VARS
//-------------------

//Temp Calc variables
const float VCC = 3.33;             // VCC voltage
const int ADC_RESOLUTION = 4095;   // ADC resolution, 4095 for ESP32 since it uses a 12bit ADC
const float R_FIXED = 2100.0;      // Value of pull-up resistor 

//Minimum and maximum temp for the needle range.


//Operating temperature range
const float minOperatingTemp = 80.0;
const float maxOperatingTemp = 125.0; //Above this temp, the gauge will enter the alarm state
const float alarmHysteresis = 2.0;    //The alarm clears once temp drops this many degrees below maxOperatingTemp

//Temperatures that close the clock
const float minGaugeTemp = 69;
const float maxGaugeTemp = 135;

//Sensor plausibility limits
const float R_SHORT_LIMIT = 150.0;      //Sensor resistance below this = sensor or wire shorted to ground
const float R_COLD_LIMIT = 20000.0;     //Above this the sensor is well below its calibrated range (~50C or less). Either cold, or the wire is open
const float COLD_TEMP_C = 20.0;         //Temp reported while "cold". Anything under 50 is displayed as "<50" anyway
const float FAULT_WARM_TEMP = 65.0;     //If the oil was above this, a sudden "cold" reading is impossible, so it's treated as a fault
const unsigned long faultDelay = 2000;  //ms of continuously unusable samples before the fault is shown

//Screen brightness settings (0-100% range)
const uint8_t dayScreenBrightness = 100;
const uint8_t nightScreenBrightness = 20;


#ifdef useSteinhart3
  // Calibrated from: 
  //7.15k @ 80C
  //4.05k @ 100C
  //2.655 @ 120C
  // This may differ on your sensor, do your own measurments!
  #define SH_A  -1.762956315e-3
  #define SH_B  6.494655652e-4
  #define SH_C  -16.60934013e-7
#else
  const float R_NOMINAL = 50571.92;   // Delphi sensor resistance at 25C (approx)
  const float T_NOMINAL = 25.0;      // Reference temperature in Celsius
  const float BETA = 3745.10;         // Beta coefficient for sensor
#endif



//-------------------
// PROGRAM VARIABLES
//-------------------

//Variables only needed to provide data on debug interface. You can safely ignore these
//Not a perfect solution but eh it'll do for now
#ifdef debugMode
  #ifdef logoOnBoot
    const bool islogoOnBootDefined = true;
  #else
    const bool islogoOnBootDefined = false;
  #endif

  #ifdef kalmanFilter
    const bool iskalmanFilterDefined = true;
  #else
    const bool iskalmanFilterDefined = false;
  #endif

  #ifdef averageReadings
    const bool isaverageReadingsDefined = true;
  #else
    const bool isaverageReadingsDefined = false;
  #endif

  #ifdef useSteinhart3
    const bool isuseSteinhart3Defined = true;
  #else
    const bool isuseSteinhart3Defined = false;
  #endif

  #ifdef calibratedADC
    const bool iscalibratedADCDefined = true;
  #else
    const bool iscalibratedADCDefined = false;
  #endif

  #ifdef OTAUpdates
    const bool isOTAUpdatesDefined = true;
  #else
    const bool isOTAUpdatesDefined = false;
  #endif
#endif

#ifdef logoOnBoot
  // PNG decoder instance
  PNG png; 
  
  //Logo PNG image position
  int16_t xpos = 0;
  int16_t ypos = 0;
#endif

//millis() val on last screen refresh
unsigned long lastRefresh = 0;

// Center coordinates
const int centerX = 120;
const int centerY = 120;
//Screen radius from center
const int radius = 110;
//Gauge needle length
const int needleLen = 90;

//Calculated red and blue zones start and endings
const int redZoneStartAng = 315-(270*(1-((maxOperatingTemp-minGaugeTemp/maxGaugeTemp-minGaugeTemp)/(maxGaugeTemp-minGaugeTemp))));
const int blueZoneEndAng = 45+(270*((minOperatingTemp-minGaugeTemp/maxGaugeTemp-minGaugeTemp))/(maxGaugeTemp-minGaugeTemp));

#ifdef averageReadings
  //We shouldnt show values every 30ms. 
  //Instead, let's check it every 30ms and show the average value after 100 readings
  //These variables store info used to average those out
  double readingsTable[sampleTableSize];
  uint8_t readingIteration = 0;
  uint8_t validReadings = 0;    //How many of those samples were usable (bad ones are skipped)
#endif

double tempC = 0;

//Sensor health tracking
float lastSensorMv = 0;             //Last measured sensor pin voltage (debug info)
float lastSensorOhms = 0;           //Last calculated sensor resistance (debug info)
unsigned long lastGoodSample = 0;   //millis() of the last usable sensor sample
bool sensorFault = false;           //True when there have been no usable samples for faultDelay ms

//Alarm state. Kept as a flag (rather than recomputed every frame) so it can have hysteresis
bool alarmActive = false;

//Needle animation state
float needlePos = 0;                //Where the needle is drawn right now, 0-100%
unsigned long lastNeedleFrame = 0;  //millis() of the last needle step
int shownTemp = 0;                  //Temp shown as text and used for colors. Latched every screenRefreshPeriod
bool shownFault = false;            //Fault state currently on screen



//-------------------
// PROGRAM FUNCTIONS
//-------------------
//Gradient circle drawing function. Works by pointer only
void drawColorCircle(int16_t x, int16_t y, int r, uint16_t color_start, uint16_t color_end, TFT_eSprite* Bck_Spr) {
  for (int16_t i = 0; i <= r; i++) {
    uint8_t interpolation = map(i, 0, r, 255, 0);
    uint8_t red = map(interpolation, 0, 255, (color_start >> 11) & 0x1F, (color_end >> 11) & 0x1F);
    uint8_t green = map(interpolation, 0, 255, (color_start >> 5) & 0x3F, (color_end >> 5) & 0x3F);
    uint8_t blue = map(interpolation, 0, 255, (color_start) & 0x1F, (color_end) & 0x1F);
    uint16_t color = (red << 11) | (green << 5) | blue;
    Bck_Spr->fillSmoothCircle(x, y, r - i, color);
  }
}

//Set TFT backlight brightnes. 0%-100% range
void TFT_SET_BL(uint8_t Value) {
  if (Value > 100) {
    printf("TFT_SET_BL Error \r\n");
  } else {
    analogWrite(TFT_BL, Value * 2.55);
  }
}

//Check if screen needs to be dimmed
void updateDimming() {
  if (digitalRead(dimmingPin)) {TFT_SET_BL(nightScreenBrightness);}
  else {TFT_SET_BL(dayScreenBrightness);}
}

//map that supports doubles
double mapDouble(double value, double fromLow, double fromHigh, double toLow, double toHigh) {
  return (value - fromLow) * (toHigh - toLow) / (fromHigh - fromLow) + toLow; 
}

#ifdef logoOnBoot
  //PNG decode and draw
  int pngDraw(PNGDRAW *pDraw) {
    uint16_t lineBuffer[MAX_IMAGE_WIDTH];
    png.getLineAsRGB565(pDraw, lineBuffer, PNG_RGB565_BIG_ENDIAN, 0xffffffff);
    tft.pushImage(xpos, ypos + pDraw->y, pDraw->iWidth, 1, lineBuffer);
    return 1;
  }
#endif
//Gauge drawing and pushing to buffer, then screen
void drawGauge(double val, int actualTemp, bool alarm, bool fault) {
  int bgCol;
  int needleCol;

  //A sensor fault replaces the reading entirely, so it takes priority over the alarm
  bool showAlarm = alarm && !fault;

  // 0. Set coloring and fill with black
  if (showAlarm){bgCol = TFT_RED; needleCol = TFT_BLACK;}
  else {bgCol = TFT_BLACK; needleCol = TFT_WHITE;}
  
  uint16_t textColor;
  if (fault) {textColor = TFT_RED;}
  else if (actualTemp >= minOperatingTemp) {textColor = TFT_WHITE;}
  else {textColor = TFT_CYAN;}

  screenBufferSprite.fillScreen(bgCol);

  // 0a. draw gradients
  if (!showAlarm) { 
    drawColorCircle(centerX, centerY, 40, TFT_BLUE, TFT_BLACK, &screenBufferSprite);   
  }

  // 1. Draw the Scale (Static Ticks)
  screenBufferSprite.drawSmoothArc(centerX, centerY, radius, radius-3, 45, 315, TFT_BLUE, TFT_TRANSPARENT, false);
  screenBufferSprite.drawSmoothArc(centerX, centerY, radius, radius-10, 45, blueZoneEndAng, TFT_CYAN, TFT_TRANSPARENT, false);
  screenBufferSprite.drawSmoothArc(centerX, centerY, radius, radius-10, redZoneStartAng, 315, TFT_RED, TFT_TRANSPARENT, false);
  for (int i = 0; i <= 100; i += 10) {
    int length;
    if (i % 20 == 0) {length = 20;} else {length = 10;}
    double angle = mapDouble(i, 0.0, 100.0, 135.0, 405.0) * 0.01745329; // Degrees to Radians
    int x1 = centerX + (109 * cos(angle));
    int y1 = centerY + (109 * sin(angle));
    int x2 = centerX + ((109-length) * cos(angle));
    int y2 = centerY + ((109-length) * sin(angle));
    screenBufferSprite.drawWideLine(x1, y1, x2, y2, 2, TFT_WHITE, TFT_TRANSPARENT);
  }
  

  // 2. Calculate Needle End Point
  // Map 0-100 to 135 to 405 degrees (standard gauge sweep)
  double needleAngle = mapDouble(val, 0.0, 100.0, 135.0, 405.0) * 0.01745329; 
  int needleX = centerX + (needleLen * cos(needleAngle));
  int needleY = centerY + (needleLen * sin(needleAngle));


  // 3. Draw the Needle
  // We draw a "wide" line by drawing two lines close together or using drawWideLine
  screenBufferSprite.fillSmoothCircle(centerX, centerY, 20, TFT_BLACK);     // Needle Pivot Hub
  if (!fault) {   //No needle on a fault: don't show a position we can't trust
    screenBufferSprite.drawWideLine(centerX, centerY, needleX, needleY, 8, needleCol, bgCol);
  }


  // 4. Draw the Value Text
  screenBufferSprite.setFreeFont(&FreeSansBold12pt7b);
  screenBufferSprite.setTextColor(textColor);

  //Use a char array rather than Strings, this saves on heap space and prevents glitches
  //during long operation times.
  char displayTemp[8];
  int textW;

  if (fault) {
    screenBufferSprite.drawString("ERR", centerX, centerY + 40);
  } else {
    if (actualTemp < 50) {
      snprintf(displayTemp, sizeof(displayTemp), "<50");
    } else {
      snprintf(displayTemp, sizeof(displayTemp), "%d", actualTemp);
    }

    //Calculate text width for dynamic positioning of "°C"
    textW = screenBufferSprite.drawString(displayTemp, centerX, centerY + 40);

    // Draw "°C"
    int degX = centerX + (textW / 2) + 6;
    int degY = centerY + 40 - 10;
    screenBufferSprite.drawCircle(degX, degY, 4, textColor);
    screenBufferSprite.drawString("C", degX + 10, centerY + 40); //C
  }

  // 5. Push to screen
  screenBufferSprite.pushSprite(0, 0);
}

//Read the sensor pin. Returns millivolts.
float readSensorMv() {
  #ifdef calibratedADC
    //analogReadMilliVolts() applies the chip's factory ADC calibration (gain/offset).
    //Averaging several reads knocks down the ADC's noise before it ever reaches the filter.
    uint32_t sum = 0;
    for (int i = 0; i < adcSamples; i++) {
      sum += analogReadMilliVolts(sensePin);
    }
    return (float)sum / adcSamples;
  #else
    //Plain analogRead(), assuming ADC full scale == VCC
    return analogRead(sensePin) * (VCC * 1000.0f / ADC_RESOLUTION);
  #endif
}

//Take one sample of the sensor and convert it to deg C.
//Returns NAN when the sample can't be real (shorted sensor, or an open wire on warm oil),
//so the caller can keep it out of the filter instead of letting it drag the reading around.
double sampleOilTemp() {
  const float vccMv = VCC * 1000.0f;
  float mv = readSensorMv();
  lastSensorMv = mv;

  //Sensor resistance from the voltage divider. A pin at (or above) VCC means "infinite" resistance
  double resistance = (mv >= vccMv - 1.0f) ? 1e9 : R_FIXED * (double)mv / (vccMv - mv);
  lastSensorOhms = (float)resistance;

  if (resistance < R_SHORT_LIMIT) return NAN;   //Shorted sensor or wire

  if (resistance > R_COLD_LIMIT) {
    //Either the oil is genuinely cold (normal after a cold start), or the sensor wire is open.
    //Electrically these look identical, but oil that was warm can't go cold instantly,
    //so if we've seen it warm this can only be a fault.
    if (tempC > FAULT_WARM_TEMP) return NAN;
    return COLD_TEMP_C;
  }

  #ifdef useSteinhart3
    //Steinhart-Hart 3-coefficient equation, highly accurate for NTC
    double lnR = log(resistance);
    double invT = SH_A + lnR * (SH_B + SH_C * lnR * lnR);
    return (1.0 / invT - 273.15);
  #else
    //Standard beta based equation. Less accurate, but only needs 2 temperature measurments.
    //It's here as a failsafe
    double steinhart = log(resistance / R_NOMINAL) / BETA;   // 1/B * ln(R/Ro)
    steinhart += 1.0 / (T_NOMINAL + 273.15);                 // + (1/To)
    return (1.0 / steinhart) - 273.15;                       // Invert, Kelvin to Celsius
  #endif
}

void setup() {
  //First things first, kill backlight immediately at the hardware level before tft.init() runs
  pinMode(TFT_BL, OUTPUT);
  digitalWrite(TFT_BL, LOW);   // hard off - doesn't rely on analogWrite/PWM being ready
  TFT_SET_BL(0);                // set PWM state too for consistency

  #ifdef debugMode 
    Serial.begin(115200);
    Serial.println("GAUGE DEBUGGING ACTIVE, REMEMBER TO DISABLE AFTER FINISHING");
  #endif

  pinMode(dimmingPin, INPUT);
  pinMode(sensePin, INPUT);

  //SET UP SCREEN
  tft.init();
  tft.setRotation(0);
  tft.fillScreen(TFT_BLACK);

  // Set the text properties for the buffer sprite
  screenBufferSprite.setTextDatum(MC_DATUM);
  screenBufferSprite.setFreeFont(&FreeSansBold18pt7b);
  screenBufferSprite.setTextColor(TFT_WHITE);

  // Allocate memory for the sprite (240x240 pixels)
  if (screenBufferSprite.createSprite(240, 200) == nullptr) {
    //If it (for some reason) fails: notify and loop indefinitely
    #ifdef debugMode 
      Serial.println("Buffer sprite allocation failed! Not enough RAM.");
    #endif

    tft.setTextColor(TFT_WHITE,TFT_BLACK);
    tft.setTextSize(1);
    tft.setCursor(100, 0);
    
    tft.println("SPRITE ALLC. FAIL");
    if (digitalRead(dimmingPin)) { TFT_SET_BL(50);}
    else {TFT_SET_BL(100);}
    
    while (true) {delay (10000000);}
  }

  #ifdef logoOnBoot
    //Show boot logo
    int16_t rc = png.openFLASH((uint8_t *)bootLogo, sizeof(bootLogo), pngDraw);
    if (rc == PNG_SUCCESS) {
      tft.startWrite();
      rc = png.decode(NULL, 0);
      tft.endWrite();
    } 
    #ifdef debugMode 
      Serial.println("Showed logo OK");
    #endif
  #endif
  
  //Enable backlight now that everything is shown and ready
  if (digitalRead(dimmingPin)) { TFT_SET_BL(50);}
  else if (!digitalRead(dimmingPin)) {TFT_SET_BL(100);}
  
  //Setup OTA update web server
  #ifdef OTAUpdates
    WiFi.setHostname(host);
    WiFi.begin(ssid, password);
    //Repurpose the screen refresh timer this one time to see if wifi connects
    lastRefresh = millis();
    while(WiFi.status() != WL_CONNECTED && millis() - lastRefresh < 3000) {
      delay(10);
    }

    if (WiFi.status() == WL_CONNECTED)
    {
      isConnectedToWifi = true;
      #ifdef debugMode 
        Serial.println("IP address: ");
        Serial.println(WiFi.localIP());
      #endif        
        
      /*use mdns for host name resolution*/
      if (!MDNS.begin(host)) { //http://esp32.local
        #ifdef debugMode 
            Serial.println("Error setting up MDNS responder!");
        #endif
      }

      server.on("/", HTTP_GET, []() {
        server.sendHeader("Connection", "close");
        server.send(200, "text/html", serverIndex);
      });
      /*handling uploading firmware file */
      server.on("/update", HTTP_POST, []() {
        server.sendHeader("Connection", "close");
        server.send(200, "text/plain", (Update.hasError()) ? "FAIL" : "OK");
        ESP.restart();
      }, []() {
        HTTPUpload& upload = server.upload();
        if (upload.status == UPLOAD_FILE_START) {
          #ifdef debugMode 
            Serial.printf("Update: %s\n", upload.filename.c_str());
          #endif
          if (!Update.begin(UPDATE_SIZE_UNKNOWN)) { //start with max available size
            Update.printError(Serial);
          }
        } else if (upload.status == UPLOAD_FILE_WRITE) {
          /* flashing firmware to ESP*/
          if (Update.write(upload.buf, upload.currentSize) != upload.currentSize) {
            Update.printError(Serial);
          }
        } else if (upload.status == UPLOAD_FILE_END) {
          if (Update.end(true)) { //true to set the size to the current progress
            #ifdef debugMode 
              Serial.printf("Update Success: %u\nRebooting...\n", upload.totalSize);
            #endif
          } else {
            Update.printError(Serial);
          }
        }
      });

      server.begin();
    }
    else
    {
      //If the user does not wish to update, turn off the WiFi module.
      //This is very important, because WiFi capabilities block access to ADC2,
      //which our device relies heavily upon
      isConnectedToWifi = false;
      WiFi.disconnect(true);
      WiFi.mode(WIFI_OFF);
      esp_wifi_deinit();
    }
  #endif

  TFT_SET_BL(0); //Turn off backlight to avoid showing half-rendered frames
  tft.fillScreen(TFT_BLACK);
  //Force first draw to prevent a black screen for 3s
  lastRefresh =  millis() - screenRefreshPeriod;
  lastGoodSample = millis();
}

void loop() {
  #ifdef OTAUpdates
    if (isConnectedToWifi) {
      tft.drawString("UPDATE MODE", 48, 32);
    }
    else{
  #endif

    //---- Sampling ----
    double sample = sampleOilTemp();    //NAN if this sample isn't usable
    unsigned long now = millis();
    if (!std::isnan(sample)) { lastGoodSample = now; }
    bool newValue = false;              //True on passes where tempC was just updated

    #ifdef averageReadings
      if (readingIteration < sampleTableSize)
      {
        //Store usable samples in the table and continue on
        if (!std::isnan(sample)) { readingsTable[validReadings++] = sample; }
        readingIteration++;
      }
      else
      {
        if (validReadings > 0)
        {
          double sum = 0.0;
          for (int i = 0; i < validReadings; i++)
          {
            sum += readingsTable[i];
          }
          tempC = sum / validReadings;
          #ifdef kalmanFilter
            tempC = simpleKalmanFilter.updateEstimate(tempC);
          #endif
          newValue = true;
        }
        readingIteration = 0;               //Reset the counters
        validReadings = 0;
      }
    #else
      //Unusable samples are kept out of the filter entirely
      if (!std::isnan(sample)) {
        #ifdef kalmanFilter
          tempC = simpleKalmanFilter.updateEstimate(sample);
        #else
          tempC = sample;
        #endif
        newValue = true;
      }
    #endif

    (void)newValue;                     //Only used by the kalman filter. Keeps the compiler quiet when that's disabled

    //No usable sample for faultDelay ms means a sensor fault
    sensorFault = (now - lastGoodSample >= faultDelay);

    //Redraw the text only every [screenRefreshPeriod] seconds while keeping the needle movement smooth
    bool textDue = (now - lastRefresh >= screenRefreshPeriod);
    if (textDue) {
      lastRefresh = now;
      shownTemp = lround(tempC);          //Round, don't truncate

      //Alarm with hysteresis, so hovering around the threshold doesn't flip the screen back and forth
      if (!alarmActive && tempC > maxOperatingTemp) { alarmActive = true; }
      else if (alarmActive && tempC < maxOperatingTemp - alarmHysteresis) { alarmActive = false; }

      #ifdef debugMode
        Serial.println("--------DEBUG INFO-----------");
        Serial.println("LOGO ON BOOT: " + String(islogoOnBootDefined));
        Serial.println("KALMAN FILTERING ACTIVE: " + String(iskalmanFilterDefined));
        Serial.println("READINGS AVERAGING ACTIVE: " + String(isaverageReadingsDefined));
        Serial.println("CALIBRATED ADC: " + String(iscalibratedADCDefined));
        Serial.println("TEMP CALCULATION METHOD: " + String(isuseSteinhart3Defined ? "Steinhart-Hart 3 variables formula" : "Standard Beta formula"));
        Serial.println("OTA UPDATES:" + String(isOTAUpdatesDefined));
        Serial.println("--------READINGS-------------");
        Serial.println("MILLIS:" + String(millis()));
        Serial.println("SENSOR V: " + String(lastSensorMv / 1000.0f));
        Serial.println("EST. SENSOR RESISTANCE: " + String(lastSensorOhms));
        Serial.println("SENSOR FAULT: " + String(sensorFault));
        Serial.println("ALARM ACTIVE: " + String(alarmActive));
        Serial.println("DIMMING PIN VOLTAGE: " + String(VCC * (analogRead(dimmingPin) / (float)ADC_RESOLUTION)));
        Serial.println("HIST. LOWEST FREE MEMORY ON HEAP: " + String(esp_get_minimum_free_heap_size()));
        Serial.println("-----------------------------");
      #endif
    }
    //If kalman filter is on, make sure to update filter between frames.
    //Only right after a fresh value, otherwise it would run on every pass (or during a sensor fault) and the filter would stall
    #ifdef kalmanFilter
      else if (newValue) {simpleKalmanFilter.updateEstimate(tempC);}
    #endif

    //---- Needle animation: runs every needleFramePeriod, independent of the slow tick ----
    bool needleMoved = false;
    if (now - lastNeedleFrame >= needleFramePeriod) {
      float dt = (float)(now - lastNeedleFrame);
      if (dt > 100.0f) dt = 100.0f;         //After a stall (or on the very first frame) don't take one giant step
      lastNeedleFrame = now;

      //Where the needle wants to be. Float on purpose: no 1% quantisation, and the
      //needle follows the live (filtered) temp rather than the 3s-old latched one.
      float target = (tempC - minGaugeTemp) / (maxGaugeTemp - minGaugeTemp) * 100.0;
      target = constrain(target, 0.0f, 100.0f);

      //Time-based exponential easing, so the speed is the same no matter how long a frame takes to draw
      float diff = target - needlePos;
      if (fabsf(diff) >= 0.05f) {           //Deadband (~0.13 deg): when settled, stop redrawing
        needlePos += diff * (1.0f - expf(-dt / needleSmoothing));
        needleMoved = true;
      }
    }

    //A fault appearing or clearing is shown right away rather than waiting for the next slow tick
    bool faultChanged = (sensorFault != shownFault);
    if (faultChanged) {
      shownFault = sensorFault;
      shownTemp = lround(tempC);
    }

    //Draw gauge on screen, only if something visible changed
    if (textDue || needleMoved || faultChanged) {
      drawGauge(needlePos, shownTemp, alarmActive, shownFault);
    }
  
  #ifdef OTAUpdates
  }
  #endif

  //Save on CPU power 
  delay(10);

  //Check if screen needs to be dimmed
  //This is on the end to allow the screen to render everything before turning on the backlight on first boot
  updateDimming();

  #ifdef OTAUpdates
    if (isConnectedToWifi) {
      server.handleClient();
    }
  #endif
}