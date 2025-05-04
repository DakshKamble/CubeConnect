#include <WiFi.h>
#include <WiFiManager.h>
#include <Firebase_ESP_Client.h>
#include "addons/TokenHelper.h"
#include "addons/RTDBHelper.h"
#include <FastLED.h>
#include <FastLED_NeoMatrix.h>
#include <Adafruit_GFX.h>
#include <math.h>

#define API_KEY       "AIzaSyBN891E7BYOBCvkCg2B1zcjO8lmQtZDuOg"
#define DATABASE_URL  "https://cube-esp-default-rtdb.asia-southeast1.firebasedatabase.app/"

#define LED_PIN       20
#define BUTTON_PIN    21
#define LED_COUNT     64

// -----------------------------------------------------------------------------
// DEFINE YOUR MENU ITEMS HERE: text + RGB color
// -----------------------------------------------------------------------------
struct MenuItem {
  const char *text;
  uint8_t r, g, b;
};
// Add as many entries as you like; the count auto‑updates.
MenuItem menuItems[] = {
  { "Good Morning",         255, 140,   0 },  // Bright orange
  { "Good Night",            25,  25, 112 },  // Keep as midnight blue
  { "Jevlis Ka?",           255,  20, 147 },  // Hot pink
  { "Check WhatsApp",        37, 211, 102 },  // WhatsApp green
  { "Yes",                  173, 255,  47 },  // Neon green-yellow
  { "No",                   255,   0,   0 },  // Bright red
  { "Maybe",                255, 215,   0 },  // Gold
  { "Please?",              218, 112, 214 },  // Orchid (kept for softness, but bright)
  { "Baap Ko Maat Sikha!",  255,  69,   0 },  // Bright red-orange
  { "Muskat Foden!!",       255,  20, 147 },  // Hot pink
  { "Chal be",              255,  99,  71 },  // Bright tomato
  { "You Ok?",               30, 144, 255 },  // Bright sky blue
  { "Come Here",            255,   0, 255 },  // Bright magenta
  { "Hold me",              255, 105, 180 },  // Hot pink (slightly softer)
  { "Don't Go",             255,   0, 127 },  // Bright rose
  { "You're Beautiful",     255,  20, 147 },  // Hot pink
  { "You're Mine",          255,   0,   0 },  // Bright red (intense claim)
  { "Dafa Hoja Gandu!",     255,   0,   0 },  // Pure bright red (aggression)
  { "Love You",             225,   0,   0 },  // Deep violet (passionate & vibrant)
  { "Miss You",             138,  43, 226 },  // Blue-violet (bright enough now)
  { "Thinking of You",      148,   0, 211 }   // Bright purple (deep emotion)
};
const int MENU_ITEM_COUNT = sizeof(menuItems) / sizeof(menuItems[0]);
// -----------------------------------------------------------------------------

const unsigned long MESSAGE_DISPLAY_DURATION = 15000;

FirebaseData fbdo;
FirebaseAuth auth;
FirebaseConfig config;
bool signupOK = false;

CRGB leds[LED_COUNT];
FastLED_NeoMatrix *matrix;

unsigned long buttonPressTime = 0;
bool buttonPressed = false;
bool longPressTriggered = false;
const unsigned long LONG_PRESS_TIME    = 3000;
const unsigned long localDebounceDelay = 50;
const unsigned long debounceDelay      = 2000;

enum AnimationMode {
  ANIM_BLANK,
  ANIM_CYLON,
  ANIM_BOUNCING_DOTS,
  ANIM_CALM,
  ANIM_SCROLL_TEXT,
  ANIM_COMET_TAIL,
  ANIM_RAINFALL,
  ANIM_SPARKLING_STARS,
  ANIM_COUNT
};
AnimationMode currentAnim = ANIM_BLANK;

enum MenuStage { MENU_HEADER, MENU_ITEMS };
bool menuActive = false;
MenuStage menuStage = MENU_HEADER;
int menuIndex = 0;

bool messageActive = false;
int messageIndex = 0;
unsigned long messageStartTime = 0;

String currentStatus = "idle";
bool statusChangeInProgress = false;

const unsigned long SCROLL_INTERVAL = 120;

unsigned long messageSentTime = 0;
bool messageSentPendingIdle = false;

uint16_t XY(uint8_t x, uint8_t y) {
  return y * 8 + x;
}

void initMatrix() {
  matrix = new FastLED_NeoMatrix(leds, 8, 8,
    NEO_MATRIX_TOP + NEO_MATRIX_LEFT +
    NEO_MATRIX_ROWS + NEO_MATRIX_PROGRESSIVE);
  matrix->setTextWrap(false);
  matrix->setBrightness(40);
  matrix->setTextColor(matrix->Color(255, 20, 147)); // default Blue
  matrix->setTextSize(1);
}

void clearMatrix() {
  FastLED.clear();
  FastLED.show();
  matrix->fillScreen(0);
  matrix->show();
}

void flashRedSmooth() {
  unsigned long start = millis();
  while (millis() - start < 3000) {
    float t = float(millis() - start) / 3000.0f;
    uint8_t b = uint8_t(sin(t * PI) * 255.0f);
    fill_solid(leds, LED_COUNT, CRGB(b, 0, 0));
    FastLED.show();
    delay(20);
  }
  clearMatrix();
}

void scrollMenuHeader() {
  static int16_t x = 8;
  static unsigned long lastTime = 0;
  static bool init = false;
  if (!init) {
    x = 8;
    lastTime = millis();
    init = true;
  }
  if (millis() - lastTime < SCROLL_INTERVAL) return;
  matrix->fillScreen(0);
  matrix->setCursor(x, 0);
  matrix->print("MENU");
  matrix->show();
  x--;
  lastTime = millis();
  int textWidth = strlen("MENU") * 6;
  if (x < -textWidth) {
    init = false;
    menuStage = MENU_ITEMS;
    x = 8;
    lastTime = millis();
  }
}

void scrollMenuItem() {
  static int16_t x = 8;
  static unsigned long lastTime = 0;
  static int lastIndex = -1;

  if (lastIndex != menuIndex) {
    x = 8;
    lastTime = millis();
    lastIndex = menuIndex;
  }
  if (millis() - lastTime < SCROLL_INTERVAL) return;

  // Fetch text and color from the struct
  const char* txt = menuItems[menuIndex].text;
  uint8_t r = menuItems[menuIndex].r;
  uint8_t g = menuItems[menuIndex].g;
  uint8_t b = menuItems[menuIndex].b;

  matrix->fillScreen(0);
  matrix->setCursor(x, 0);
  matrix->setTextColor(matrix->Color(r, g, b));
  matrix->print(txt);
  matrix->show();

  // restore default red
  matrix->setTextColor(matrix->Color(255, 20, 147));

  x--;
  lastTime = millis();
  int textWidth = strlen(txt) * 6;
  if (x < -textWidth) x = 8;
}

void scrollActiveMessage() {
  static int16_t x = 8;
  static unsigned long lastTime = 0;
  static int lastIndex = -1;

  // Reset position when messageIndex changes
  if (lastIndex != messageIndex) {
    x = 8;
    lastTime = millis();
    lastIndex = messageIndex;
  }
  if (millis() - lastTime < SCROLL_INTERVAL) return;

  // Fetch text and its custom color
  const char* txt = menuItems[messageIndex].text;
  uint8_t r = menuItems[messageIndex].r;
  uint8_t g = menuItems[messageIndex].g;
  uint8_t b = menuItems[messageIndex].b;

  matrix->fillScreen(0);
  matrix->setCursor(x, 0);
  matrix->setTextColor(matrix->Color(r, g, b));  // apply the custom color
  matrix->print(txt);
  matrix->show();

  // (optional) restore default red if used elsewhere
  matrix->setTextColor(matrix->Color(255, 20, 147));

  x--;
  lastTime = millis();
  int textWidth = strlen(txt) * 6;
  if (x < -textWidth) x = 8;
}

void drawScrollText() {
  static const char* scrollText = "Gargi";
  static int16_t scrollX = 8;
  static unsigned long lastScrollTime = 0;
  if (millis() - lastScrollTime < SCROLL_INTERVAL) return;
  matrix->fillScreen(0);
  matrix->setCursor(scrollX, 0);
  matrix->print(scrollText);
  matrix->show();
  scrollX--;
  if (scrollX < -((int)strlen(scrollText) * 6)) scrollX = 8;
  lastScrollTime = millis();
}

void drawBlank() {
  FastLED.clear();
  FastLED.show();
}

void drawCylon() {
  static int pos = 0, dir = 1;
  static uint8_t hue = 0;
  leds[pos] = CHSV(hue++, 255, 255);
  FastLED.show();
  fadeToBlackBy(leds, LED_COUNT, 20);
  pos += dir;
  if (pos == LED_COUNT - 1 || pos == 0) dir = -dir;
}

void drawBouncingDots() {
  static int x = 0, dx = 1;
  FastLED.clear();
  for (int y = 0; y < 8; y++) leds[XY(x, y)] = CRGB::Blue;
  x += dx;
  if (x == 0 || x == 7) dx = -dx;
  FastLED.show();
}

void drawCalmEffect() {
  static uint16_t x = 0, y = 0;
  for (int i = 0; i < LED_COUNT; i++) {
    uint8_t noise = inoise8(x + i * 10, y + i * 10);
    leds[i] = CHSV(noise, 255, 255);
  }
  x++; y++;
  FastLED.show();
}

void cometTail() {
  static int x = 0;
  fadeToBlackBy(leds, LED_COUNT, 80);
  for (int y = 0; y < 8; y++) leds[XY(x, y)] = CHSV(160, 255, 255);
  x = (x + 1) % 8;
  FastLED.show();
}

void rainfall() {
  for (int i = 0; i < 8; i++)
    if (random8() < 30) leds[XY(random8(8), 0)] = CRGB::White;
  for (int y = 7; y >= 1; y--)
    for (int x = 0; x < 8; x++)
      leds[XY(x, y)] = leds[XY(x, y - 1)];
  for (int x = 0; x < 8; x++) leds[XY(x, 0)] = CRGB::Black;
  FastLED.show();
}

void sparklingStars() {
  fadeToBlackBy(leds, LED_COUNT, 20);
  if (random8() < 70) {
    int x = random8(8), y = random8(8);
    leds[XY(x, y)] = (random8() < 150) ? CRGB(255,192,203) : CRGB::White;
  }
  FastLED.show();
}

void setStatus(const String &status) {
  if (Firebase.ready() && signupOK && !statusChangeInProgress) {
    Firebase.RTDB.setString(&fbdo, "cube1/status", status);
    currentStatus = status;
    statusChangeInProgress = true;
    delay(debounceDelay);
    statusChangeInProgress = false;
  }
}

void checkButton() {
  int state = digitalRead(BUTTON_PIN);
  static int lastState = HIGH;
  static unsigned long lastDebounce = 0;

  if (state != lastState) lastDebounce = millis();

  if (millis() - lastDebounce > localDebounceDelay) {
    if (state == LOW && !buttonPressed) {
      buttonPressed = true;
      longPressTriggered = false;
      buttonPressTime = millis();
    }
    else if (state == LOW && buttonPressed && !longPressTriggered) {
      if (millis() - buttonPressTime > LONG_PRESS_TIME) {
        if (!menuActive && !messageActive) {
          menuActive = true;
          menuStage = MENU_HEADER;
          menuIndex = 0;
        }
        else if (menuActive && menuStage == MENU_ITEMS) {
          flashRedSmooth();
          String statusMsg = "msg:" + String(menuIndex);
          setStatus(statusMsg);
          messageSentTime = millis();
          messageSentPendingIdle = true;
          menuActive = false;
        }
        longPressTriggered = true;
      }
    }
    else if (state == HIGH && buttonPressed) {
      buttonPressed = false;
      if (!longPressTriggered) {
        if (menuActive && menuStage == MENU_ITEMS) {
          menuIndex = (menuIndex + 1) % MENU_ITEM_COUNT;
        } else if (!messageActive) {
          currentAnim = AnimationMode((currentAnim + 1) % ANIM_COUNT);
        }
      }
    }
  }
  lastState = state;
}

void checkFirebaseStatus() {
  if (Firebase.RTDB.getString(&fbdo, "cube2/status")) {
    String status = fbdo.stringData();
    if (status != currentStatus) {
      if (status.startsWith("msg:")) {
        int idx = status.substring(4).toInt();
        if (idx >= 0 && idx < MENU_ITEM_COUNT) {
          messageActive = true;
          messageIndex = idx;
          messageStartTime = millis();
          currentStatus = status;
        }
      }
      else if (status == "idle") {
        messageActive = false;
        clearMatrix();
        currentStatus = "idle";
      }
    }
  }
}

void setup() {
  Serial.begin(115200);
  FastLED.addLeds<WS2812B, LED_PIN, GRB>(leds, LED_COUNT);
  FastLED.setBrightness(200);
  FastLED.clear();
  FastLED.show();

  initMatrix();
  pinMode(BUTTON_PIN, INPUT_PULLUP);

  WiFiManager wm;
  if (!wm.autoConnect("Gargi's Cube")) {
    ESP.restart();
  }

  config.api_key = API_KEY;
  config.database_url = DATABASE_URL;
  Firebase.reconnectWiFi(true);
  if (Firebase.signUp(&config, &auth, "", "")) signupOK = true;
  Firebase.begin(&config, &auth);
  Firebase.setDoubleDigits(5);

  setStatus("idle");
}

void loop() {
  if (messageActive) {
    scrollActiveMessage();
    if (millis() - messageStartTime >= MESSAGE_DISPLAY_DURATION) {
      messageActive = false;
      clearMatrix();
      setStatus("idle");
    }
    return;
  }

  if (menuActive) {
    checkButton();
    if (menuStage == MENU_HEADER) {
      scrollMenuHeader();
    } else {
      scrollMenuItem();
    }
    return;
  }

  checkButton();

  static unsigned long lastFirebaseCheck = 0;
  if (millis() - lastFirebaseCheck > 2000) {
    checkFirebaseStatus();
    lastFirebaseCheck = millis();
  }

  if (messageSentPendingIdle && millis() - messageSentTime >= 10000) {
    setStatus("idle");
    messageSentPendingIdle = false;
  }

  switch (currentAnim) {
    case ANIM_BLANK:           drawBlank();           break;
    case ANIM_CYLON:           drawCylon();           break;
    case ANIM_BOUNCING_DOTS:   drawBouncingDots();    break;
    case ANIM_CALM:            drawCalmEffect();      break;
    case ANIM_SCROLL_TEXT:     drawScrollText();      break;
    case ANIM_COMET_TAIL:      cometTail();           break;
    case ANIM_RAINFALL:        rainfall();            break;
    case ANIM_SPARKLING_STARS: sparklingStars();      break;
  }

  delay(50);
}