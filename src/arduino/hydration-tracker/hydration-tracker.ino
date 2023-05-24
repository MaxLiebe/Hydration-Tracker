#include <FastLED.h>
#include "HX711.h"
#include <TM1637Display.h>
#include <Wire.h>
#include <SSD1306.h>
#include "fonts.h"
#include "logo.h"

#define SCREEN_ADDRESS 0x3C
#define OLED_SDA 13
#define OLED_SCK 14
SSD1306 display(SCREEN_ADDRESS, OLED_SDA, OLED_SCK);

#define RING_DATA_PIN 18
#define LED_TYPE    WS2812B
#define COLOR_ORDER GRB
#define NUM_LEDS    24
#define BRIGHTNESS 200
CRGB ringLeds[NUM_LEDS];

#define HX711_SCK 36
#define HX711_DT 38
#define LOAD_CELL_THRESHOLD 80000
HX711 loadCell;

#define TIMER_CLK 40
#define TIMER_DIO 39
TM1637Display timer(TIMER_CLK, TIMER_DIO);
uint8_t TIMER_BLANK[4];

#define READY_BUTTON_PIN 3

bool glassPresent = false;
unsigned long glassPutOn = 0;
bool glassAnimationDone = false;

void setup() {
  Serial.begin(9600);

  //initialize the LED ring with FastLED, and turn off all the LEDs
  FastLED.addLeds<LED_TYPE, RING_DATA_PIN, COLOR_ORDER>(ringLeds, NUM_LEDS).setCorrection(TypicalLEDStrip);
  FastLED.setBrightness(BRIGHTNESS);
  FastLED.clear();
  FastLED.show();

  //initialize the load cell
  loadCell.begin(HX711_DT, HX711_SCK);
  loadCell.tare();

  //initialize the 7 segment timer
  timer.setBrightness(0x0f);
  //timer.setSegments(TIMER_BLANK);
  timer.showNumberDecEx(0, 0b11111111, true, 4, 0);

  //set the button to use the inbuilt pullup resistor
  pinMode(READY_BUTTON_PIN, INPUT_PULLUP);
  
  //init and clear the display
  Wire.begin(OLED_SDA, OLED_SCK);
  display.init();
  display.clear();

  //show the splash screen
  display.drawXbm(2, 2, LOGO_WIDTH, LOGO_HEIGHT, LOGO_BITS);
  display.setFont(Roboto_Black_22);
  display.setTextAlignment(TEXT_ALIGN_RIGHT);
  display.setColor(WHITE);
  display.drawString(128, 14, "KLOK-");
  display.setFont(Roboto_Black_14);
  display.drawString(128, 42, "KLOKKER");
  display.flipScreenVertically();
  display.display();

  //wait a bit to show the splash screen
  delay(1000);

  //fade in and out the green LED ring
  for(int i = 2 * 255; i > 0; i--) {
    fill_solid(ringLeds, NUM_LEDS, CRGB::Green);
    fadeToBlackBy(ringLeds, NUM_LEDS, i < 255 ? 255 - i : i - 255);
    FastLED.show();
    delay(6);
  }
  
  display.clear();
  display.setTextAlignment(TEXT_ALIGN_LEFT);
  display.drawString(0, 6, "1 Zet je glas neer");
  display.drawString(0, 24, "2 Druk op \"klaar\"");
  display.drawString(0, 42, "3 Trek een bak!");
  display.display();
}

void loop() {
  if(loadCell.is_ready()) {
    display.clear();
    display.setTextAlignment(TEXT_ALIGN_LEFT);
    int value = loadCell.get_value(1);
    display.drawString(0, 6, String(value));
    display.display();
    bool glassStatus = abs(value) > LOAD_CELL_THRESHOLD;
    if(glassStatus && !glassPresent) {
      glassPutOn = millis();
    }else if(!glassStatus && glassAnimationDone) {
      glassAnimationDone = false;
    }
    glassPresent = glassStatus;
  }
  if(glassPresent && !glassAnimationDone) {
    glassDetectedRingAnimation();
  }else{
    idleRingAnimation();
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
  //show 2 opposite snakes of x LEDs slowly filling up a circle
  FastLED.clear();
  int snakeSpeed = 40; //LEDs per second
  unsigned long currentTime = millis() - glassPutOn;
  int currentPosition = (currentTime / (1000 / snakeSpeed));
  if(currentPosition >= NUM_LEDS / 2) {
    glassAnimationDone = true;
  }
  for(int i = 0; i < NUM_LEDS / 2; i++) {
    if(i <= currentPosition) {
      ringLeds[i] = CRGB::Green;
      ringLeds[NUM_LEDS - 1 - i] = CRGB::Green;
    }
  }
  FastLED.show();
}
