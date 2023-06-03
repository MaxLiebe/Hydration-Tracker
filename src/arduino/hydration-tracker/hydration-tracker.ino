//IMPORT LIBRARIES
#include <FastLED.h>
#include "HX711.h"
#include <TM1637Display.h>
#include <Wire.h>
#include <SSD1306.h>
#include <qrcodeoled.h>
#include <WiFi.h>
#include "soc/soc.h"
#include "soc/rtc_cntl_reg.h"
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
#define RING_DATA_PIN 14
#define LED_TYPE    WS2812B
#define COLOR_ORDER GRB
#define NUM_LEDS    24
#define BRIGHTNESS 200
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
#define TIMER_CLK 2
#define TIMER_DIO 1
TM1637Display timer(TIMER_CLK, TIMER_DIO);
uint8_t TIMER_BLANK[4];

//SD Card
#define SD_CS_PIN        12
#define SD_CLK_PIN       11
#define SD_MOSI_PIN      9
#define SD_MISO_PIN      7
#define MHz 1000000

//MODE BUTTON
#define MODE_BUTTON_PIN 3

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

//WIFI CREDENTIALS FOR LEADERBOARD
const char* ssid = "HydrationTracker";
const char* password = "drinkwater";

//WIFI SERVER
WiFiServer server(80);
String header;

//VARIABLES
unsigned long timerStartedMs = 0;
unsigned long timeElapsed = 0;
int timerState = BOOTING;
int animationState = ANIMATION_BOOT;

String secrets[10];

void setup() {
  WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0);
  
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

  WiFi.mode(WIFI_STA);
  IPAddress Ip(4, 3, 2, 1);
  IPAddress NMask(255, 255, 255, 0);
  WiFi.softAPConfig(Ip, Ip, NMask);
  Serial.print("Setting AP (Access Point)…");
  WiFi.softAP(ssid, password);
  IPAddress IP = WiFi.softAPIP();
  Serial.print("AP IP address: ");
  Serial.println(IP);
  server.begin();

  pinMode(SD_CS_PIN, OUTPUT); // SS

  SPIClass spiSD = SPIClass(VSPI); // Neither HSPI nor VSPI seem to work
  spiSD.begin(SD_CLK_PIN, SD_MISO_PIN, SD_MOSI_PIN, SD_CS_PIN); 

  if (!SD.begin(SD_CS_PIN, spiSD))//, 1 * MHz)) - no freq work: 1, 25, 40, 50, 80 MHz
    Serial.println("we gaming");
}

void loop() {
  switch(timerState) {
    case BOOTING:
      if(millis() > BOOT_TIME) {
        timerState = WAITING_FOR_THRESHOLD;
        animationState = ANIMATION_IDLE;
        display.clear();
        display.setTextAlignment(TEXT_ALIGN_CENTER);
        display.drawString(64, 25, "Waiting for drink");
        switch(SD.cardType()) {
          case CARD_NONE: {
            display.drawString(64, 50, "F");
            break;
          }
          case CARD_SD: {
            display.drawString(64, 50, "Let's go");
            break;
          }
          case CARD_SDHC: {
            display.drawString(64, 50, "Let's goo");
            break;
          }
        }
        display.display();
      }
      break;
    case TIMER_RUNNING:
      timeElapsed = millis() - timerStartedMs;
      encodeMsToTimer(timeElapsed);
      break;
  }
  
  if(loadCell.is_ready()) {
    int weightValue = loadCell.get_value(1);
    //DEBUG show the load cell value on the display
//    display.clear();
//    display.setTextAlignment(TEXT_ALIGN_LEFT);
//    display.drawString(0, 6, String(weightValue));
//    display.display();
    
    //becomes true if something is detected on the load cell (the weight is above the threshold)
    bool thresholdMet = abs(weightValue) > abs(LOAD_CELL_CALIBRATED_VALUE) + LOAD_CELL_THRESHOLD || abs(weightValue) < abs(LOAD_CELL_CALIBRATED_VALUE) - LOAD_CELL_THRESHOLD;

    switch(timerState) {
      case WAITING_FOR_THRESHOLD:
        if(thresholdMet) {
          timerState = THRESHOLD_REACHED;
          animationState = ANIMATION_TIMER_PRIMED;
          encodeMsToTimer(0);
          display.clear();
          display.setTextAlignment(TEXT_ALIGN_CENTER);
          display.drawString(64, 25, "Ready?");
          display.display();
        }
        break;
      case THRESHOLD_REACHED:
        if(!thresholdMet) {
          timerState = TIMER_RUNNING;
          timerStartedMs = millis();
          animationState = ANIMATION_TIMER_RUNNING;
          display.clear();
          display.setTextAlignment(TEXT_ALIGN_CENTER);
          display.drawString(64, 25, "Go!");
          display.display();
        }
        break;
      case TIMER_RUNNING:
        if(thresholdMet) {
          timerState = TIMER_STOPPED;
          animationState = ANIMATION_IDLE;
          display.clear();
          display.setTextAlignment(TEXT_ALIGN_CENTER);
          display.drawString(64, 25, "DONE!");
          display.display();
        }
        break;
      case TIMER_STOPPED:
        if(!thresholdMet) {
          timerState = WAITING_FOR_THRESHOLD;
          char buf[20];
          itoa(timeElapsed, buf, 10);
          char secret[10];
          generateRandomSecret(secret, 10);
          for(int i = 1; i < 10; i++) {
            secrets[i] = secrets[i - 1];
          }
          secrets[0] = String(secret);
          qrcode.create("http://4.3.2.1?t=" + String(buf) + "&s=" + secrets[0]);
        }
        break;
    }
  }

  //switch statement to manage what animation is being displayed on the LED ring
  switch(animationState) {
    case ANIMATION_BOOT: {
      //fade in and out the green LED ring
      unsigned long currentTime = millis();
      int fadeValue = 255 - (float)(abs((int)currentTime % 1000 - 500) / 500.0 * 255);
      fill_solid(ringLeds, NUM_LEDS, CRGB::Green);
      fadeToBlackBy(ringLeds, NUM_LEDS, fadeValue);
      FastLED.show();
      break;
    }
    case ANIMATION_IDLE: {
      idleRingAnimation();
      break;
    }
    case ANIMATION_TIMER_PRIMED:
      break;
    case ANIMATION_TIMER_RUNNING: {
      glassDetectedRingAnimation();
      break; 
    }
  }

  WiFiClient client = server.available();   // Listen for incoming clients

  if (client) {                             // If a new client connects,
    Serial.println("New Client.");          // print a message out in the serial port
    String currentLine = "";                // make a String to hold incoming data from the client
    while (client.connected()) {            // loop while the client's connected
      if (client.available()) {             // if there's bytes to read from the client,
        char c = client.read();             // read a byte, then
        Serial.write(c);                    // print it out the serial monitor
        header += c;
        if (c == '\n') {                    // if the byte is a newline character
          // if the current line is blank, you got two newline characters in a row.
          // that's the end of the client HTTP request, so send a response:
          if (currentLine.length() == 0) {
            // HTTP headers always start with a response code (e.g. HTTP/1.1 200 OK)
            // and a content-type so the client knows what's coming, then a blank line:
            client.println("HTTP/1.1 200 OK");
            client.println("Content-type:text/html");
            client.println("Connection: close");
            client.println();

            // Display the HTML web page
            client.println("<!DOCTYPE html><html>");
            client.println("<head><meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">");
            client.println("<link rel=\"icon\" href=\"data:,\">");
            // CSS to style the on/off buttons 
            // Feel free to change the background-color and font-size attributes to fit your preferences
            client.println("<style>html { font-family: Helvetica; display: inline-block; margin: 0px auto; text-align: center;}");
            client.println(".button { background-color: #4CAF50; border: none; color: white; padding: 16px 40px;");
            client.println("text-decoration: none; font-size: 30px; margin: 2px; cursor: pointer;}");
            client.println(".button2 {background-color: #555555;}</style></head>");
            
            // Web Page Heading
            client.println("<body><h1>ESP32 Web Server</h1>");

            client.println("</body></html>");
            
            // The HTTP response ends with another blank line
            client.println();
            // Break out of the while loop
            break;
          } else { // if you got a newline, then clear currentLine
            currentLine = "";
          }
        } else if (c != '\r') {  // if you got anything else but a carriage return character,
          currentLine += c;      // add it to the end of the currentLine
        }
      }
    }
    // Clear the header variable
    header = "";
    // Close the connection
    client.stop();
    Serial.println("Client disconnected.");
    Serial.println("");
  }
}

void encodeMsToTimer(unsigned long ms) {
  //create an array to store the digits in
  uint8_t segments[4];

  //decide what mode to use for the timer based on how much time has elapsed
  int timerMode = ms < ONE_MINUTE ? SECONDS_HUNDREDTHS : MINUTES_SECONDS;

  //timerMode is an int, so we'll use a switch statement to accomodate for different modes
  //for expandability, we have opted not to just use a boolean here
  switch(timerMode) {
    case SECONDS_HUNDREDTHS:
      //extract the seconds and the hundredths from the milliseconds
      int seconds = ms / 1000;
      int hundredths = (ms - (seconds * 1000)) / 10;

      //update the segments with the correct digits
      segments[0] = timer.encodeDigit(seconds / 10);
      segments[1] = timer.encodeDigit(seconds % 10) | 0x80; //OR-ing with 0x80 to turn on the :
      segments[2] = timer.encodeDigit(hundredths / 10);
      segments[3] = timer.encodeDigit(hundredths % 10);
      break; 
  }
  timer.setSegments(segments);
}

void generateRandomSecret(char* buf, int l) {
  for(int i = 0; i < l; i++) {
    byte randomValue = random(0, 37);
    char letter = randomValue + 'a';
    if(randomValue > 26) {
      letter = (randomValue - 26) + '0';
    }
    buf[i] = letter;
  }
}

void idleRingAnimation() {
  //show a snake of x LEDs going round with a certain speed
  FastLED.clear();
  int trailLeds = 20;
  int snakeSpeed = 24; //LEDs per second
  unsigned long currentTime = millis();
  int currentPosition = (currentTime / (1000 / snakeSpeed)) % NUM_LEDS;
  for(int i = 0; i < trailLeds; i++) {
    int led = (currentPosition - i + NUM_LEDS) % NUM_LEDS;
    ringLeds[led] = CHSV(currentTime / 15 % 255, 255, 255);
    byte fade = map(i, 0, trailLeds, 0, 255);
    ringLeds[led].fadeToBlackBy(fade);
  }
  FastLED.show();
}

void glassDetectedRingAnimation() {
    //show a snake of x LEDs going round with a certain speed
  FastLED.clear();
  int trailLeds = 20;
  int snakeSpeed = 70; //LEDs per second
  unsigned long currentTime = millis();
  int currentPosition = (currentTime / (1000 / snakeSpeed)) % NUM_LEDS;
  for(int i = 0; i < trailLeds; i++) {
    int led = (currentPosition - i + NUM_LEDS) % NUM_LEDS;
    ringLeds[led] = CHSV(currentTime / 15 % 255, 255, 255);
    byte fade = map(i, 0, trailLeds, 0, 255);
    ringLeds[led].fadeToBlackBy(fade);
  }
  FastLED.show();

  // //show 2 opposite snakes of x LEDs slowly filling up a circle
  // FastLED.clear();
  // int snakeSpeed = 40; //LEDs per second
  // unsigned long currentTime = millis() - glassPutOn;
  // int currentPosition = (currentTime / (1000 / snakeSpeed));
  // // if(currentPosition >= NUM_LEDS / 2) {
  // //   glassAnimationDone = true;
  // // }
  // for(int i = 0; i < NUM_LEDS / 2; i++) {
  //   if(i <= currentPosition) {
  //     ringLeds[i] = CRGB::Green;
  //     ringLeds[NUM_LEDS - 1 - i] = CRGB::Green;
  //   }
  // }
  // FastLED.show();
}
