//IMPORT LIBRARIES
#include <FastLED.h>
#include "HX711.h"
#include <TM1637Display.h>
#include <Wire.h>
#include <SSD1306.h>
#include <qrcodeoled.h>
#include <WiFi.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include "FS.h"
#include "SD.h"
#include "SPI.h"

#include "fonts.h"
#include "logo.h"

//SCREEN
#define SCREEN_ADDRESS 0x3C
#define OLED_SDA 16
#define OLED_SCK 17
SSD1306 display(SCREEN_ADDRESS, OLED_SDA, OLED_SCK, GEOMETRY_128_64);
QRcodeOled qrcode(&display);

//LED RING
#define RING_DATA_PIN 18  //M14 A18
#define LED_TYPE WS2812B
#define COLOR_ORDER GRB
#define NUM_LEDS 24
#define BRIGHTNESS 200
#define MIN_BRIGHTNESS 0
CRGB ringLeds[NUM_LEDS];

//LOAD CELL
#define HX711_SCK 5
#define HX711_DT 3
//#define LOAD_CELL_CALIBRATED_VALUE -397700 //to get this value, read the value of the load cell when nothing is on it using the debug code below (lid needs to be screwed in)
#define LOAD_CELL_CALIBRATED_VALUE -374700 //lower value for testing when the lid is not on
//#define LOAD_CELL_THRESHOLD 17000 //can be calibrated yourself. we'd recommend leaving it at this value
#define LOAD_CELL_THRESHOLD 50000 //different value for testing when the lid is not on
HX711 loadCell;

//TIMER
#define TIMER_CLK 40  //M2 A40
#define TIMER_DIO 39  //M1 A39
TM1637Display timer(TIMER_CLK, TIMER_DIO);
uint8_t TIMER_BLANK[4];

//SD Card
#define SD_CS_PIN        12
#define SD_CLK_PIN       11
#define SD_MOSI_PIN      9
#define SD_MISO_PIN      7
#define SD_FREQ          80000000
SPIClass spi = SPIClass(HSPI);

//MODE BUTTON
#define MODE_BUTTON_PIN 1  //M34 A1

//TIMER MODES
#define SECONDS_HUNDREDTHS 0
#define MINUTES_SECONDS 1

//TIMER STATES
#define BOOTING -1
#define WAITING_FOR_THRESHOLD 0
#define THRESHOLD_REACHED 1
#define TIMER_RUNNING 2
#define TIMER_STOPPED 3

#define BOOT_TIME 4000
#define ONE_MINUTE 60000

//ANIMATION STATES
#define ANIMATION_BOOT 0
#define ANIMATION_IDLE 1
#define ANIMATION_TIMER_PRIMED 2
#define ANIMATION_TIMER_STARTED 3
#define ANIMATION_TIMER_RUNNING 4
#define ANIMATION_TIMER_STOPPED 5
#define ANIMATION_MODUS_STATE 6

#define ANIMATION_SPEED_DRINKING 25
#define BRIGHTNESS_STEP_DRINKING 15

#define PULSE_DURATION 1000  // Duration of the pulse in milliseconds

//MODE STATES
#define NUMBER_OF_MODES 3
#define BEER 0
#define SHOT 1
#define WATER 2

//WIFI CREDENTIALS FOR LEADERBOARD
const char* ssid = "HydrationTracker";
const char* password = "drinkwater";

//WIFI SERVER
AsyncWebServer server(80);
String header;

//VARIABLES
unsigned long timerStartedMs = 0;
unsigned long timeElapsed = 0;
unsigned long animationStarted = 0;
int timerState = BOOTING;
int modeState = BEER;
int animationState = ANIMATION_BOOT;
bool leaderboardEnabled = true;

String secrets[10];

int numberOfModes = NUMBER_OF_MODES;
bool pulseActive = true;
String typeOfDrink = "beer";
String drinkStatus = "Waiting for drink";
bool pressButton = true;

long lastDebounceTime = 0;  // the last time the output pin was toggled
long debounceDelay = 200;   // the debounce time; increase if the output flickers

// Define the current brightness and pulsation direction
uint8_t minBrightnessDrinking = MIN_BRIGHTNESS;
int8_t pulsationDirectionDrinking = 1;

// Define variables for timing
unsigned long previousMillisDrinking = 0;
unsigned long intervalDrinking = ANIMATION_SPEED_DRINKING;

void setup() {
  Serial.begin(9600);
  //initialize the LED ring with FastLED, and turn off all the LEDs
  FastLED.addLeds<LED_TYPE, RING_DATA_PIN, COLOR_ORDER>(ringLeds, NUM_LEDS).setCorrection(TypicalLEDStrip);
  FastLED.setBrightness(BRIGHTNESS);
  FastLED.clear();
  FastLED.show();

  //initialize the load cell
  loadCell.begin(HX711_DT, HX711_SCK);

  //initialize the 7 segment timer
  timer.setBrightness(0xff);
  encodeMsToTimer(0);

  //set the button to use the inbuilt pullup resistor
  pinMode(MODE_BUTTON_PIN, INPUT_PULLUP);

  //init and clear the display
  Wire.begin(OLED_SDA, OLED_SCK);
  display.setColor(WHITE);
  display.setFont(Roboto_Black_14);
  display.init();
  qrcode.init();
  display.clear();

  //show the splash screen
  display.drawXbm(0, 0, LOGO_WIDTH, LOGO_HEIGHT, LOGO_BITS);
  display.flipScreenVertically();
  display.display();

  //setup sd card
  spi.begin(SD_CLK_PIN, SD_MISO_PIN, SD_MOSI_PIN, SD_CS_PIN); 
  if (!SD.begin(SD_CS_PIN, spi, SD_FREQ)) {
    //SD card initialisation failed, don't bother with setting up a wifi hotspot.
    leaderboardEnabled = false;
    return;
  }

  //setup wifi access point
  WiFi.mode(WIFI_STA);
  IPAddress Ip(4, 3, 2, 1);
  IPAddress NMask(255, 255, 255, 0);
  WiFi.softAPConfig(Ip, Ip, NMask);
  WiFi.softAP(ssid, password);
  IPAddress IP = WiFi.softAPIP();

  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){
    //request->send(200, "text/plain", "Hello, world");
    request->send(SD, "/index.html", "text/html");
  });

  server.serveStatic("/", SD, "/");
  
  server.begin();
}

void loop() {
  display.setTextAlignment(TEXT_ALIGN_CENTER);
  display.clear();
  display.drawString(64, 12, drinkStatus);
  display.drawString(64, 30, typeOfDrink);
  display.display();
  if (pressButton) {
    if ((millis() - lastDebounceTime) > debounceDelay) {
      if (digitalRead(MODE_BUTTON_PIN) == 0) {
        lastDebounceTime = millis();  //set the current time
        modeState += 1;
        animationStarted = millis();
        animationState = ANIMATION_MODUS_STATE;
      }
    }
  }
  if (modeState > numberOfModes - 1) {
    modeState = 0;
  }

  switch (modeState) {
    case BEER:
      typeOfDrink = "Drink: Beer";
      break;
    case SHOT:
      typeOfDrink = "Drink: Shot";
      break;
    case WATER:
      typeOfDrink = "Drink: Water";
      break;
  }

  switch (timerState) {
    case BOOTING:
      pressButton = true;
      if (millis() > BOOT_TIME) {
        timerState = WAITING_FOR_THRESHOLD;
        animationState = ANIMATION_IDLE;
        switch(SD.cardType()) {
          case CARD_NONE: {
            display.drawString(64, 50, "No SD card found");
            leaderboardEnabled = false;
            break;
          }
        }
        display.display();
      }
      break;
    case TIMER_RUNNING:
      timeElapsed = millis() - timerStartedMs;
      encodeMsToTimer(timeElapsed);
      drinkStatus = "Waiting for drink";
      pressButton = false;
      break;
  }

  if (loadCell.is_ready()) {
    int weightValue = loadCell.get_value(1);

    //becomes true if something is detected on the load cell (the weight is above the threshold)
    bool thresholdMet = abs(weightValue) > abs(LOAD_CELL_CALIBRATED_VALUE) + LOAD_CELL_THRESHOLD || abs(weightValue) < abs(LOAD_CELL_CALIBRATED_VALUE) - LOAD_CELL_THRESHOLD;

    switch (timerState) {
      case WAITING_FOR_THRESHOLD:
        if (thresholdMet) {
          pressButton = false;
          timerState = THRESHOLD_REACHED;
          animationState = ANIMATION_TIMER_PRIMED;
          encodeMsToTimer(0);
          drinkStatus = "Ready?";
        }
        break;
      case THRESHOLD_REACHED:
        if (!thresholdMet) {
          timerState = TIMER_RUNNING;
          timerStartedMs = millis();
          animationState = ANIMATION_TIMER_RUNNING;
          drinkStatus = "Go!";
        }
        break;
      case TIMER_RUNNING:
        if (thresholdMet) {
          timerState = TIMER_STOPPED;
          animationState = ANIMATION_IDLE;
          drinkStatus = "DONE!";
        }
        break;
      case TIMER_STOPPED:
        pressButton = true;
        if (!thresholdMet) {
          timerState = WAITING_FOR_THRESHOLD;
          char buf[20];
          itoa(timeElapsed, buf, 10);
          char secret[10];
          generateRandomDigits(secret, 10);
          for(int i = 1; i < 10; i++) {
            secrets[i] = secrets[i - 1];
          }
          secrets[0] = String(secret);
          qrcode.create("http://4.3.2.1?t=" + String(buf) + "&s=" + secrets[0]);
          drinkStatus = "Waiting for drink";
        }
        break;
    }
  }

  //switch statement to manage what animation is being displayed on the LED ring
  switch (animationState) {
    case ANIMATION_MODUS_STATE:
      {
        if (millis() - animationStarted > debounceDelay * 2) {
          animationState = ANIMATION_IDLE;
        }
        modusSwitch();
        break;
      }
    case ANIMATION_BOOT:
      {
        //fade in and out the green LED ring
        unsigned long currentTime = millis();
        int fadeValue = 255 - (float)(abs((int)currentTime % 1000 - 500) / 500.0 * 255);
        fill_solid(ringLeds, NUM_LEDS, CRGB::Green);
        fadeToBlackBy(ringLeds, NUM_LEDS, fadeValue);
        FastLED.show();
        break;
      }
    case ANIMATION_IDLE:
      {
        idleRingAnimation();
        break;
      }
    case ANIMATION_TIMER_PRIMED:
      break;
    case ANIMATION_TIMER_RUNNING:
      {
        glassDetectedRingAnimation();
        break;
      }
  }
}

void encodeMsToTimer(unsigned long ms) {
  //create an array to store the digits in
  uint8_t segments[4];

  //decide what mode to use for the timer based on how much time has elapsed
  int timerMode = ms < ONE_MINUTE ? SECONDS_HUNDREDTHS : MINUTES_SECONDS;

  //timerMode is an int, so we'll use a switch statement to accomodate for different modes
  //for expandability, we have opted not to just use a boolean here
  switch (timerMode) {
    case SECONDS_HUNDREDTHS:
      //extract the seconds and the hundredths from the milliseconds
      int seconds = ms / 1000;
      int hundredths = (ms - (seconds * 1000)) / 10;

      //update the segments with the correct digits
      segments[0] = timer.encodeDigit(seconds / 10);
      segments[1] = timer.encodeDigit(seconds % 10) | 0x80;  //OR-ing with 0x80 to turn on the :
      segments[2] = timer.encodeDigit(hundredths / 10);
      segments[3] = timer.encodeDigit(hundredths % 10);
      break;
  }
  timer.setSegments(segments);
}

void generateRandomDigits(char* buf, int l) {
  for(int i = 0; i < l; i++) {
    byte randomValue = random(0, 10);
    char letter = randomValue + '0';
    buf[i] = letter;
  }
}

void idleRingAnimation() {
  //show a snake of x LEDs going round with a certain speed
  FastLED.clear();
  int trailLeds = 20;
  int snakeSpeed = 24;  //LEDs per second
  unsigned long currentTime = millis();
  int currentPosition = (currentTime / (1000 / snakeSpeed)) % NUM_LEDS;
  for (int i = 0; i < trailLeds; i++) {
    int led = (currentPosition - i + NUM_LEDS) % NUM_LEDS;
    ringLeds[led] = CHSV(currentTime / 15 % 255, 255, 255);
    byte fade = map(i, 0, trailLeds, 0, 255);
    ringLeds[led].fadeToBlackBy(fade);
  }
  FastLED.show();
}

void glassDetectedRingAnimation() {
  // Check if it's time to update the animation
  unsigned long currentMillisDrinking = millis();
  if (currentMillisDrinking - previousMillisDrinking >= intervalDrinking) {
    previousMillisDrinking = currentMillisDrinking;

    // Update the brightness
    minBrightnessDrinking += pulsationDirectionDrinking * BRIGHTNESS_STEP_DRINKING;

    // Set the LED colors
    fill_rainbow(ringLeds, NUM_LEDS, minBrightnessDrinking);

    // Show the updated LED array
    FastLED.show();
  }
}

void modusSwitch() {
  unsigned long elapsedTime = millis() - animationStarted;
  // Calculate the brightness based on the elapsed time
  uint8_t brightness = map(elapsedTime, 0, PULSE_DURATION, 0, 255);
  // Apply the red color and brightness to all LEDs
  fill_solid(ringLeds, NUM_LEDS, CRGB(brightness, 0, 0));
  FastLED.show();
}