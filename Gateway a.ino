#include <WiFi.h>
#include <SPI.h>
#include <SD.h>
#include <time.h>
#include <sys/time.h>
#include <Preferences.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ILI9341.h>
#include <HardwareSerial.h>
#include <PubSubClient.h>

#include "wifiConfig.h"
#include "OTAUpdate.h"

#define TFT_CS        5
#define TFT_DC        27
#define TFT_RST       33
#define SD_CS         32

#define LORA_AUX      4
#define LORA_M0       25
#define LORA_M1       26
#define LORA_RX_PIN   16
#define LORA_TX_PIN   17

#define BTN_NEXT_PIN  21
#define BTN_OK_PIN    22

#define SCREEN_ROTATION 3
#define TFT_SPI_FREQ    16000000UL

#define WIFI_ENABLED    true
#define WIFI_SSID       "Bắt bắt Cmm"
#define WIFI_PASS       "huy06122004"

#define TB_ENABLED      true
#define TB_SERVER       "thingsboard.cloud"
#define TB_PORT         1883
#define TB_TOKEN        "token_device"
#define TB_TOPIC        "v1/devices/me/telemetry"

#define ZALO_ENABLED            true
#define ZALO_BOT_TOKEN          "BOT_TOKEN"
#define ZALO_CHAT_ID            "e0bc67060750ee0eb741"
#define ZALO_REPEAT_INTERVAL_MS 60000UL

#define QUEUE_FILE      "/queue.txt"
#define SYNC_INTERVAL_MS 300UL

#define GMT_OFFSET_SEC  (7 * 3600)
#define DST_OFFSET_SEC  0
#define NTP_RESYNC_MS   (6UL * 3600UL * 1000UL)
#define NVS_SAVE_MS     (10UL * 60UL * 1000UL)

#define NODE_COUNT      4
#define NODE_TIMEOUT_MS 60000UL

#define LORA_PSK         "WGW2026"

#define DEMO_FAKE_RX    false

#define SCREEN_W   320
#define SCREEN_H   240
#define HEADER_H   36

#define TILE_W     156
#define TILE_H     64
#define TILE_X0    3
#define TILE_X1    161
#define TILE_Y0    38
#define TILE_DY    66

#define CARD_W     156
#define CARD_H     88
#define CARD_X0    3
#define CARD_X1    161
#define CARD_Y0    38
#define CARD_Y1    129
#define FOOTER_Y   220

constexpr uint16_t RGB(uint8_t r, uint8_t g, uint8_t b) {
  return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
}
const uint16_t COL_PANEL      = RGB(10, 18, 28);
const uint16_t COL_CARD_BG    = RGB(12, 22, 34);
const uint16_t COL_CARD_SEL   = RGB(22, 42, 64);
const uint16_t COL_OFF        = RGB(60, 68, 76);
const uint16_t COL_BORDER_DIM = RGB(45, 65, 85);
const uint16_t COL_SELECT     = RGB(255, 220, 40);

Adafruit_ILI9341 tft(TFT_CS, TFT_DC, TFT_RST);
HardwareSerial LoRa(2);
Preferences prefs;
WiFiClient espClient;
PubSubClient mqttClient(espClient);

inline void spiSelectTFT() {
  digitalWrite(SD_CS, HIGH);
}

inline void spiSelectSD() {
  digitalWrite(TFT_CS, HIGH);
}

struct PalEntry { char c; uint8_t r, g, b; };
static const PalEntry PAL[] = {
  {'W', 255, 255, 255},
  {'L', 185, 210, 230},
  {'G', 130, 145, 160},
  {'D',  60,  70,  80},
  {'R', 255,  55,  55},
  {'O', 255, 145,  30},
  {'Y', 255, 235,  45},
  {'N',  50, 235, 105},
  {'E',  20, 165,  75},
  {'C',  80, 230, 255},
  {'B',  35, 135, 255},
  {'K',  15,  70, 185},
  {'M', 225,  85, 235},
  {'T', 180, 110,  45},
  {'U', 235, 185,  95}
};
static const int PAL_N = sizeof(PAL) / sizeof(PAL[0]);

uint16_t palColor(char ch, bool dim) {
  for (int i = 0; i < PAL_N; i++) {
    if (PAL[i].c == ch) {
      if (!dim) return RGB(PAL[i].r, PAL[i].g, PAL[i].b);
      uint8_t g = (PAL[i].r + PAL[i].g + PAL[i].b) / 9;
      return RGB(g, g, g);
    }
  }
  return 0;
}

static const char* const ICON_WATER[16] = {
  "..WWWW..........",
  "..WLLW..........",
  "..WLLW.....C....",
  "..WLLW....CCC...",
  "..WRRW....CCBC..",
  "..WRRW...CCBBBC.",
  "..WORW...CWCBBBC",
  "..WORW..CCWBBBBK",
  "..WRRW..CBBBBBBK",
  ".WWRRWW.CBBBBBBK",
  "WRRRRRRW.BBBBKK.",
  "WRROORRW..KKKK..",
  "WRROORRW........",
  "WRRRRRRW........",
  ".WRRRRW.........",
  "..WWWW.........."
};

static const char* const ICON_AIR[16] = {
  "..WWWW..........",
  "..WLLW...CCCCC..",
  "..WLLW..CWWWWWC.",
  "..WLLW.CWWWWWWWC",
  "..WLLW..CCCCCCC.",
  "..WRRW..........",
  "..WORW...CCCCCCC",
  "..WORW..........",
  "..WRRW....YYYYY.",
  ".WWRRWW........Y",
  "WRRRRRRW..CCCCC.",
  "WRROORRW.......C",
  "WRROORRW....CCC.",
  "WRRRRRRW........",
  ".WRRRRW.........",
  "..WWWW.........."
};

static const char* const ICON_PH[16] = {
  "....WWWWWWWW....",
  ".....WLLLLW.....",
  "......WLLW......",
  "......WLLW......",
  ".....W....W.....",
  "....W..WW..W....",
  "...W........W...",
  "...WCCCCCCCCW...",
  "..WNNNNWWNNNNW..",
  "..WNNNNNNNNNNW..",
  ".WEEEEWEEEEEEEW.",
  ".WEEEEEEEEEEEEW.",
  ".WYYYYYYYYYYYYW.",
  ".WOOOOOOOOOOOOW.",
  "..WWWWWWWWWWWW..",
  "................"
};

static const char* const ICON_TURB[16] = {
  "................",
  "..WWWWWWWWWWWW..",
  "...WLLLLLLLLW...",
  "...WCCCCCCCCW...",
  "...WCWCCCCWCW...",
  "...WUUUUUUUUW...",
  "...WUOUUUYUOW...",
  "...WUUUTUUUUW...",
  "...WTTTTTTTTW...",
  "...WTOUTTUOTW...",
  "...WTTTTUTTTW...",
  "...WTUTTTTTUW...",
  "...WTTTTTTTTW...",
  "...WOOOOOOOOW...",
  "...WWWWWWWWWW...",
  "................"
};

static const char* const ICON_DO[16] = {
  ".........CCCC...",
  "........CCWWCC..",
  "........CWBBBC..",
  "........CBBBBC..",
  ".........CCCC...",
  "..CCCCC.........",
  ".CCWWWCC...CCC..",
  "CCWBBBBCC.CWWBC.",
  "CWBBBBBBC.CBBBC.",
  "CBBBBBBBC..CCC..",
  "CBBBBBBKC.......",
  ".CCBBBKC........",
  "..CCCCC.........",
  "................",
  "CCCCCCCCCCCCCCCC",
  "BBBBBBBBBBBBBBBB"
};

static const char* const ICON_EC[16] = {
  "......OOOOO.....",
  ".....OYYYYO.....",
  "....OYYYYO......",
  "...OYYWYO.......",
  "..OYYWYYYYOO....",
  "..OOOOOOOOYO....",
  ".....OYYYYO.....",
  "....OYYWYO......",
  "...OYYYO........",
  "..OYYO..........",
  ".OYO............",
  "................",
  "..CC..CC..CC..CC",
  "CC..CC..CC..CC..",
  "BBBBBBBBBBBBBBBB",
  "KKKKKKKKKKKKKKKK"
};

static const char* const ICON_WIFI[16] = {
  "................",
  "................",
  "................",
  "......cccc......",
  "...cccccccccc...",
  ".ccc........ccc.",
  "cc............cc",
  "c...bbbbbbbb...c",
  "...bb......bb...",
  "...b........b...",
  "......aaaa......",
  ".....aa..aa.....",
  "................",
  ".......dd.......",
  ".......dd.......",
  "................"
};

void drawIcon(const char* const* rows, int x, int y, int scale, bool dim) {
  spiSelectTFT();
  for (int r = 0; r < 16; r++) {
    const char* s = rows[r];
    int c = 0;
    while (c < 16) {
      char ch = s[c];
      if (ch == '.') { c++; continue; }
      int run = 1;
      while (c + run < 16 && s[c + run] == ch) run++;
      tft.fillRect(x + c * scale, y + r * scale, run * scale, scale, palColor(ch, dim));
      c += run;
    }
  }
}

enum { S_WATER_TEMP, S_AIR_TEMP, S_PH, S_TURB, S_DO, S_EC, S_COUNT };

static const char UNIT_C[] = "\xF8" "C";

struct SensorMeta {
  const char* label;
  const char* shortLbl;
  const char* zaloName;
  const char* tbKey;
  const char* unit;
  const char* zaloUnit;
  uint8_t decimals;
  uint16_t accent;
  const char* const* icon;
};

const SensorMeta SENSOR_META[S_COUNT] = {
  {"To H2O", "H2O", "Nhiệt độ nước",      "wt",   UNIT_C,  "°C",   1, RGB(60, 165, 255),  ICON_WATER},
  {"To Air", "Air", "Nhiệt độ không khí", "at",   UNIT_C,  "°C",   1, RGB(120, 225, 255), ICON_AIR},
  {"pH",     "pH ", "Độ pH",              "ph",   "",      "",     2, RGB(60, 225, 110),  ICON_PH},
  {"Turb",   "Trb", "Độ đục",             "turb", "%",     "%",    1, RGB(245, 155, 55),  ICON_TURB},
  {"DO",     "DO ", "Oxy hòa tan (DO)",   "do",   "mg/L",  "mg/L", 2, RGB(80, 210, 255),  ICON_DO},
  {"Do Man", "Man", "Độ mặn (từ EC)",     "ec",   "o/oo",  "‰",    1, RGB(255, 225, 45),  ICON_EC}
};

float convertEcToSalinityPpt(float rawEc) {
  if (rawEc <= 0.0f) return 0.0f;
  double ec_uS = (rawEc <= 100.0f) ? (rawEc * 1000.0) : (double)rawEc;
  double T = 25.0;
  double R = ec_uS / 42914.0;
  double rt = 0.6766097 + (2.00564e-2 + (1.104259e-4 + (-6.9698e-7 + 1.0031e-9 * T) * T) * T) * T;
  double Rt = R / rt;
  if (Rt <= 0.0) return 0.0f;
  double x = sqrt(Rt);
  double f = (T - 15.0) / (1.0 + 0.0162 * (T - 15.0));
  double S = 0.0080 + (-0.1692 + (25.3851 + (14.0941 + (-7.0261 + 2.7081 * x) * x) * x) * x) * x
           + f * (0.0005 + (-0.0056 + (-0.0066 + (-0.0375 + (0.0636 - 0.0144 * x) * x) * x) * x) * x);
  double x4 = 400.0 * Rt, y = 100.0 * Rt;
  S -= 0.0080 / (1.0 + 1.5 * x4 + x4 * x4) + 0.0005 * f / (1.0 + sqrt(y) + y + pow(y, 1.5));
  return (S > 0.0) ? (float)S : 0.0f;
}

struct SensorThreshold {
  float minVal;
  float maxVal;
  bool hasMax;
};

SensorThreshold SENSOR_LIMITS[S_COUNT] = {
  {28.0f, 32.0f,  true},
  {20.0f, 35.0f,  true},
  {7.5f,  8.5f,   true},
  {30.0f, 45.0f,  true},
  {5.0f,  999.0f, false},
  {10.0f, 25.0f,  true}
};

struct RelayConfig {
  bool state;
  bool timerEn;
  uint8_t onH, onM;
  uint8_t offH, offM;
  unsigned long countdownEndMs;
  int lastTriggeredMinute;
};

RelayConfig relays[3] = {
  {false, false, 6, 0, 7, 0, 0, -1},
  {false, false, 14, 0, 15, 0, 0, -1},
  {false, false, 18, 0, 19, 0, 0, -1}
};

struct NodeData {
  const char* id;
  const char* title;
  float values[S_COUNT];
  float rawEc_uS;
  bool valid[S_COUNT];
  bool inAlert[S_COUNT];
  unsigned long lastAlertMs[S_COUNT];
  unsigned long seq;
  unsigned long lastRx;
  bool everRx;
  bool wasStale;
};

NodeData nodes[NODE_COUNT] = {
  {"ND1", "NODE 1", {0}, 0.0f, {false}, {false}, {0}, 0, 0, false, false},
  {"ND2", "NODE 2", {0}, 0.0f, {false}, {false}, {0}, 0, 0, false, false},
  {"ND3", "NODE 3", {0}, 0.0f, {false}, {false}, {0}, 0, 0, false, false},
  {"ND4", "NODE 4", {0}, 0.0f, {false}, {false}, {0}, 0, 0, false, false}
};

bool isNodeOnline(int nodeIdx) {
  return nodes[nodeIdx].everRx && (millis() - nodes[nodeIdx].lastRx <= NODE_TIMEOUT_MS);
}

enum ScreenMode { SCREEN_HOME, SCREEN_DETAIL };
ScreenMode currentScreen = SCREEN_HOME;
int selectedNode = 0;
bool showingWifiPortalScreen = false;

bool sdOk = false;
unsigned long queuedCount = 0;
uint32_t queueReadPos = 0;
unsigned long lastSdTry = 0;
unsigned long lastSyncStep = 0;

unsigned long wifiLastBegin = 0;
int prevWifiKey = -1;
bool ntpActive = false;
bool everSynced = false;
bool timeRestored = false;
unsigned long ntpStartMs = 0, lastNtpTry = 0, lastNtpSync = 0, lastNvsSave = 0;

unsigned long lastMqttTry = 0;
unsigned long tbTxCount = 0;

bool getValidTime(struct tm &t) {
  time_t now = time(nullptr);
  localtime_r(&now, &t);
  return (t.tm_year + 1900 >= 2024 && t.tm_year + 1900 <= 2099);
}

uint64_t getEpochMillis() {
  struct timeval tv;
  gettimeofday(&tv, nullptr);
  if (tv.tv_sec < 1704067200L) return 0;
  return (uint64_t)tv.tv_sec * 1000ULL + (uint64_t)(tv.tv_usec / 1000);
}

void saveTimeToNvs() {
  struct tm t;
  if (getValidTime(t)) {
    time_t now = time(nullptr);
    prefs.putULong("epoch", (unsigned long)now);
  }
}

void restoreTimeFromNvs() {
  unsigned long savedEpoch = prefs.getULong("epoch", 0);
  if (savedEpoch > 1704067200UL) {
    struct timeval tv = { (time_t)savedEpoch, 0 };
    settimeofday(&tv, nullptr);
    timeRestored = true;
  }
}

void printCenteredText(const char* text, int y, uint8_t textSize, uint16_t color) {
  int charW = 6 * textSize;
  int len = strlen(text);
  int x = (SCREEN_W - len * charW) / 2;
  if (x < 4) x = 4;
  tft.setTextSize(textSize);
  tft.setTextColor(color, ILI9341_WHITE);
  tft.setCursor(x, y);
  tft.print(text);
}

void drawWifiPortalScreen() {
  spiSelectTFT();
  tft.fillScreen(ILI9341_WHITE);

  printCenteredText("CHUA KET NOI WIFI", 42, 2, ILI9341_NAVY);

  String apLine = "WiFi: " + apSsid;
  printCenteredText(apLine.c_str(), 92, 2, ILI9341_BLACK);

  printCenteredText("Truy cap dia chi:", 138, 1, RGB(60, 70, 85));
  printCenteredText("http://192.168.4.1", 160, 2, RGB(0, 90, 210));
}

void drawClock() {
  if (showingWifiPortalScreen) return;
  spiSelectTFT();
  char tStr[12], dStr[16];
  struct tm ti;
  bool valid = getValidTime(ti);

  if (valid) {
    snprintf(tStr, sizeof(tStr), "%02d:%02d:%02d", ti.tm_hour, ti.tm_min, ti.tm_sec);
    snprintf(dStr, sizeof(dStr), "%02d/%02d/%04d", ti.tm_mday, ti.tm_mon + 1, ti.tm_year + 1900);
  } else {
    strcpy(tStr, "--:--:--");
    strcpy(dStr, "--/--/----");
  }

  uint16_t clockCol = everSynced ? ILI9341_WHITE : (valid ? ILI9341_YELLOW : ILI9341_RED);
  tft.setTextSize(2);
  tft.setTextColor(clockCol, ILI9341_NAVY);
  tft.setCursor(6, 4);
  tft.print(tStr);

  tft.setTextSize(1);
  tft.setTextColor(valid ? ILI9341_LIGHTGREY : ILI9341_RED, ILI9341_NAVY);
  tft.setCursor(6, 24);
  tft.print(dStr);
}

void drawStatus() {
  if (showingWifiPortalScreen) return;
  spiSelectTFT();
  char tmp[24], buf[24];
  tft.setTextSize(1);

  uint16_t sdCol = ILI9341_GREEN;
  if (!sdOk) {
    snprintf(tmp, sizeof(tmp), "SD:LOI");
    sdCol = ILI9341_RED;
  } else if (queuedCount > 0) {
    snprintf(tmp, sizeof(tmp), "SD:CHO %lu", queuedCount);
    sdCol = ILI9341_YELLOW;
  } else {
    snprintf(tmp, sizeof(tmp), "SD:OK Q:0");
    sdCol = ILI9341_GREEN;
  }
  snprintf(buf, sizeof(buf), "%-14.14s", tmp);
  tft.setTextColor(sdCol, ILI9341_NAVY);
  tft.setCursor(112, 6);
  tft.print(buf);

  if (currentScreen == SCREEN_HOME) {
    int activeNodes = 0;
    for (int i = 0; i < NODE_COUNT; i++) {
      if (isNodeOnline(i)) activeNodes++;
    }
    snprintf(tmp, sizeof(tmp), "LoRa:%d/4 NODE", activeNodes);
    snprintf(buf, sizeof(buf), "%-14.14s", tmp);
    tft.setTextColor(activeNodes > 0 ? ILI9341_GREEN : ILI9341_RED, ILI9341_NAVY);
    tft.setCursor(112, 21);
    tft.print(buf);
  } else {
    bool online = isNodeOnline(selectedNode);
    snprintf(tmp, sizeof(tmp), "[%s] %s", nodes[selectedNode].id, online ? "ONLINE" : "MAT KN");
    snprintf(buf, sizeof(buf), "%-14.14s", tmp);
    tft.setTextColor(online ? ILI9341_CYAN : ILI9341_ORANGE, ILI9341_NAVY);
    tft.setCursor(112, 21);
    tft.print(buf);
  }
}

void drawWifiIcon(int x, int y, int arcs, uint16_t litColor, uint16_t slash) {
  spiSelectTFT();
  for (int r = 0; r < 16; r++) {
    const char* s = ICON_WIFI[r];
    int c = 0;
    while (c < 16) {
      char ch = s[c];
      if (ch == '.') { c++; continue; }
      int run = 1;
      while (c + run < 16 && s[c + run] == ch) run++;
      bool lit = false;
      if (ch == 'a') lit = (arcs >= 1);
      else if (ch == 'b') lit = (arcs >= 2);
      else if (ch == 'c') lit = (arcs >= 3);
      else if (ch == 'd') lit = (slash == 0);
      tft.fillRect(x + c * 2, y + r * 2, run * 2, 2, lit ? litColor : COL_OFF);
      c += run;
    }
  }
  if (slash) {
    tft.drawLine(x + 2, y + 30, x + 30, y + 2, slash);
    tft.drawLine(x + 3, y + 30, x + 31, y + 2, slash);
    tft.drawLine(x + 2, y + 29, x + 30, y + 1, slash);
  }
}

void updateWifiIcon(bool forceRedraw = false) {
  if (showingWifiPortalScreen) return;
  spiSelectTFT();
  int state;
  int arcs = 0;
  bool tbConnected = mqttClient.connected();

  if (!WIFI_ENABLED) {
    state = 3;
  } else if (WiFi.status() == WL_CONNECTED) {
    state = 2;
    int rssi = WiFi.RSSI();
    arcs = (rssi > -60) ? 3 : (rssi > -72) ? 2 : 1;
  } else if (millis() - wifiLastBegin < 15000) {
    state = 1;
    arcs = (millis() / 350) % 4;
  } else {
    state = 0;
  }

  int key = state * 20 + arcs * 2 + (tbConnected ? 1 : 0);
  if (!forceRedraw && key == prevWifiKey) return;
  prevWifiKey = key;

  uint16_t color = (state == 2) ? ILI9341_GREEN : (state == 1) ? ILI9341_ORANGE : COL_OFF;
  uint16_t slash = (state == 0) ? ILI9341_RED : (state == 3) ? COL_OFF : 0;

  const char* txt = (state == 2) ? (tbConnected ? "TB: OK" : "WiFi OK") :
                    (state == 1) ? "Dang noi..." :
                    (state == 3) ? "WiFi tat" : "Mat WiFi";

  tft.fillRect(202, 0, SCREEN_W - 202, HEADER_H, ILI9341_NAVY);
  drawWifiIcon(284, 2, arcs, color, slash);
  tft.setTextSize(1);
  uint16_t txtCol = (state == 0) ? ILI9341_RED :
                    (state == 1) ? ILI9341_ORANGE :
                    (state == 2) ? (tbConnected ? ILI9341_GREEN : ILI9341_YELLOW) : ILI9341_DARKGREY;
  tft.setTextColor(txtCol, ILI9341_NAVY);
  tft.setCursor(278 - strlen(txt) * 6, 14);
  tft.print(txt);
}

int cardX(int idx) { return (idx % 2) ? CARD_X1 : CARD_X0; }
int cardY(int idx) { return (idx / 2) ? CARD_Y1 : CARD_Y0; }

void drawNodeCard(int idx) {
  if (showingWifiPortalScreen) return;
  spiSelectTFT();
  int x = cardX(idx);
  int y = cardY(idx);
  bool sel = (idx == selectedNode);
  bool online = isNodeOnline(idx);

  uint16_t borderCol = sel ? COL_SELECT : (online ? RGB(40, 160, 100) : COL_BORDER_DIM);
  uint16_t bgCol     = sel ? COL_CARD_SEL : COL_CARD_BG;

  tft.fillRect(x, y, CARD_W, CARD_H, borderCol);
  tft.fillRect(x + 2, y + 2, CARD_W - 4, CARD_H - 4, bgCol);

  uint16_t headBg = sel ? RGB(45, 75, 110) : RGB(18, 32, 48);
  tft.fillRect(x + 2, y + 2, CARD_W - 4, 20, headBg);

  tft.setTextSize(1);
  tft.setTextColor(sel ? COL_SELECT : ILI9341_WHITE, headBg);
  tft.setCursor(x + 6, y + 8);
  if (sel) tft.printf("> %s (%s)", nodes[idx].title, nodes[idx].id);
  else     tft.printf("  %s (%s)", nodes[idx].title, nodes[idx].id);

  uint16_t stCol = online ? ILI9341_GREEN : ILI9341_RED;
  tft.fillCircle(x + CARD_W - 42, y + 11, 3, stCol);
  tft.setTextColor(stCol, headBg);
  tft.setCursor(x + CARD_W - 35, y + 8);
  tft.print(online ? "ON " : "OFF");

  for (int s = 0; s < S_COUNT; s++) {
    int col = s % 2;
    int row = s / 2;
    int sx = x + 6 + col * 75;
    int sy = y + 27 + row * 19;

    tft.setTextColor(SENSOR_META[s].accent, bgCol);
    tft.setCursor(sx, sy);
    tft.printf("%s:", SENSOR_META[s].shortLbl);

    char valBuf[10];
    if (online && nodes[idx].valid[s]) {
      snprintf(valBuf, sizeof(valBuf), "%.*f", SENSOR_META[s].decimals, nodes[idx].values[s]);
      tft.setTextColor(ILI9341_WHITE, bgCol);
    } else {
      strcpy(valBuf, "--");
      tft.setTextColor(RGB(110, 125, 140), bgCol);
    }
    tft.setCursor(sx + 26, sy);
    tft.printf("%-6.6s", valBuf);
  }
}

void drawHomeFooter() {
  if (showingWifiPortalScreen) return;
  spiSelectTFT();
  tft.fillRect(0, FOOTER_Y, SCREEN_W, SCREEN_H - FOOTER_Y, RGB(15, 25, 38));
  tft.drawFastHLine(0, FOOTER_Y, SCREEN_W, COL_BORDER_DIM);
  tft.setTextSize(1);
  tft.setTextColor(COL_SELECT, RGB(15, 25, 38));
  tft.setCursor(8, FOOTER_Y + 6);
  tft.print("[NUT 1] Chon Node");

  tft.setTextColor(ILI9341_CYAN, RGB(15, 25, 38));
  tft.setCursor(168, FOOTER_Y + 6);
  tft.printf("[NUT 2] Xem %s ->", nodes[selectedNode].id);
}

void drawHomeScreen() {
  if (showingWifiPortalScreen) return;
  spiSelectTFT();
  tft.fillRect(0, HEADER_H, SCREEN_W, SCREEN_H - HEADER_H, ILI9341_BLACK);
  drawStatus();
  for (int i = 0; i < NODE_COUNT; i++) {
    drawNodeCard(i);
  }
  drawHomeFooter();
}

int tileX(int i) { return (i % 2) ? TILE_X1 : TILE_X0; }
int tileY(int i) { return (i / 2) ? TILE_Y0 + (i / 2) * TILE_DY : TILE_Y0; }

void drawTileStatic(int nodeIdx, int sIdx) {
  if (showingWifiPortalScreen) return;
  (void)nodeIdx;
  spiSelectTFT();
  int x = tileX(sIdx), y = tileY(sIdx);
  uint16_t acc = SENSOR_META[sIdx].accent;

  tft.fillRect(x, y, TILE_W, TILE_H, acc);
  tft.fillRect(x + 2, y + 2, TILE_W - 4, TILE_H - 4, COL_PANEL);
  tft.fillRect(x, y, 2, 2, ILI9341_BLACK);
  tft.fillRect(x + TILE_W - 2, y, 2, 2, ILI9341_BLACK);
  tft.fillRect(x, y + TILE_H - 2, 2, 2, ILI9341_BLACK);
  tft.fillRect(x + TILE_W - 2, y + TILE_H - 2, 2, 2, ILI9341_BLACK);

  drawIcon(SENSOR_META[sIdx].icon, x + 6, y + 8, 3, false);

  tft.setTextSize(2);
  tft.setTextColor(acc);
  tft.setCursor(x + 58, y + 6);
  tft.print(SENSOR_META[sIdx].label);
}

void drawTileValue(int nodeIdx, int sIdx) {
  if (showingWifiPortalScreen) return;
  spiSelectTFT();
  int x = tileX(sIdx), y = tileY(sIdx);
  bool isVal = isNodeOnline(nodeIdx) && nodes[nodeIdx].valid[sIdx];

  tft.fillRect(x + 56, y + 24, TILE_W - 60, 36, COL_PANEL);

  char buf[16];
  if (isVal) snprintf(buf, sizeof(buf), "%.*f", SENSOR_META[sIdx].decimals, nodes[nodeIdx].values[sIdx]);
  else       strcpy(buf, "--");

  int size = ((int)strlen(buf) * 18 <= 92) ? 3 : 2;
  tft.setTextSize(size);
  tft.setTextColor(isVal ? ILI9341_WHITE : RGB(110, 125, 140));
  tft.setCursor(x + 58, y + (size == 3 ? 24 : 28));
  tft.print(buf);

  tft.setTextSize(1);
  tft.setTextColor(SENSOR_META[sIdx].accent);
  tft.setCursor(x + 58, y + 52);
  tft.print(SENSOR_META[sIdx].unit);
}

void drawDetailScreen() {
  if (showingWifiPortalScreen) return;
  spiSelectTFT();
  tft.fillRect(0, HEADER_H, SCREEN_W, SCREEN_H - HEADER_H, ILI9341_BLACK);
  drawStatus();
  for (int i = 0; i < S_COUNT; i++) {
    drawTileStatic(selectedNode, i);
    drawTileValue(selectedNode, i);
  }
}

void drawFullMainInterface() {
  showingWifiPortalScreen = false;
  spiSelectTFT();
  tft.fillScreen(ILI9341_BLACK);
  tft.fillRect(0, 0, SCREEN_W, HEADER_H, ILI9341_NAVY);
  prevWifiKey = -1;
  drawClock();
  updateWifiIcon(true);
  if (currentScreen == SCREEN_HOME) {
    drawHomeScreen();
  } else {
    drawDetailScreen();
  }
}

void checkScreenModeByWifi() {
  bool wifiConnected = (WiFi.status() == WL_CONNECTED);
  if (!wifiConnected && !showingWifiPortalScreen) {
    showingWifiPortalScreen = true;
    if (wifiMode != 0) {
      startAPMode();
    }
    drawWifiPortalScreen();
  } else if (wifiConnected && showingWifiPortalScreen) {
    drawFullMainInterface();
  }
}

void pollButtons() {
  if (showingWifiPortalScreen) return;
  static bool lastNextState = HIGH;
  static bool lastOkState   = HIGH;
  static unsigned long nextPressTime = 0;
  static unsigned long okPressTime   = 0;
  static bool nextLongHandled = false;

  unsigned long now = millis();
  bool curNext = digitalRead(BTN_NEXT_PIN);
  bool curOk   = digitalRead(BTN_OK_PIN);

  if (curNext == LOW && lastNextState == HIGH && (now - nextPressTime > 50)) {
    nextPressTime = now;
    nextLongHandled = false;
  } else if (curNext == LOW && !nextLongHandled && (now - nextPressTime > 650)) {
    nextLongHandled = true;
    if (currentScreen == SCREEN_DETAIL) {
      currentScreen = SCREEN_HOME;
      drawHomeScreen();
    }
  } else if (curNext == HIGH && lastNextState == LOW && (now - nextPressTime > 40)) {
    if (!nextLongHandled) {
      int prevNode = selectedNode;
      selectedNode = (selectedNode + 1) % NODE_COUNT;
      if (currentScreen == SCREEN_HOME) {
        drawNodeCard(prevNode);
        drawNodeCard(selectedNode);
        drawHomeFooter();
      } else {
        drawDetailScreen();
      }
    }
  }
  lastNextState = curNext;

  if (curOk == LOW && lastOkState == HIGH && (now - okPressTime > 50)) {
    okPressTime = now;
  } else if (curOk == HIGH && lastOkState == LOW && (now - okPressTime > 40)) {
    if (currentScreen == SCREEN_HOME) {
      currentScreen = SCREEN_DETAIL;
      drawDetailScreen();
    } else {
      currentScreen = SCREEN_HOME;
      drawHomeScreen();
    }
  }
  lastOkState = curOk;
}

void recountQueueFile() {
  if (!sdOk) return;
  spiSelectSD();
  if (!SD.exists(QUEUE_FILE)) {
    queuedCount = 0;
    queueReadPos = 0;
    spiSelectTFT();
    return;
  }
  File f = SD.open(QUEUE_FILE, FILE_READ);
  if (!f) {
    queuedCount = 0;
    spiSelectTFT();
    return;
  }
  if (queueReadPos >= f.size()) {
    f.close();
    SD.remove(QUEUE_FILE);
    queuedCount = 0;
    queueReadPos = 0;
    spiSelectTFT();
    return;
  }
  f.seek(queueReadPos);
  unsigned long cnt = 0;
  while (f.available()) {
    if (f.read() == '\n') cnt++;
  }
  f.close();
  queuedCount = cnt;
  spiSelectTFT();
}

bool startSd() {
  spiSelectSD();
  SD.end();
  sdOk = SD.begin(SD_CS);
  if (sdOk) {
    recountQueueFile();
  }
  spiSelectTFT();
  return sdOk;
}

bool enqueueOfflinePayload(const char* jsonLine) {
  if (!sdOk) return false;
  spiSelectSD();

  File f = SD.open(QUEUE_FILE, FILE_APPEND);
  if (!f) {
    sdOk = false;
    spiSelectTFT();
    return false;
  }
  f.println(jsonLine);
  f.close();
  queuedCount++;
  spiSelectTFT();
  return true;
}

void syncOfflineQueueTask() {
  if (!sdOk || queuedCount == 0) return;
  if (!WIFI_ENABLED || !TB_ENABLED || WiFi.status() != WL_CONNECTED) return;
  if (!mqttClient.connected()) return;
  if (millis() - lastSyncStep < SYNC_INTERVAL_MS) return;
  lastSyncStep = millis();

  spiSelectSD();
  File f = SD.open(QUEUE_FILE, FILE_READ);
  if (!f) {
    queuedCount = 0;
    queueReadPos = 0;
    spiSelectTFT();
    return;
  }

  if (queueReadPos >= f.size()) {
    f.close();
    SD.remove(QUEUE_FILE);
    queuedCount = 0;
    queueReadPos = 0;
    spiSelectTFT();
    drawStatus();
    return;
  }

  f.seek(queueReadPos);
  char line[320];
  int len = 0;
  while (f.available() && len < (int)sizeof(line) - 1) {
    char c = f.read();
    if (c == '\n') break;
    if (c != '\r') line[len++] = c;
  }
  uint32_t nextPos = f.position();
  uint32_t totalSize = f.size();
  f.close();
  spiSelectTFT();

  line[len] = 0;
  if (len > 2) {
    if (mqttClient.publish(TB_TOPIC, line)) {
      queueReadPos = nextPos;
      if (queuedCount > 0) queuedCount--;
      tbTxCount++;

      if (queueReadPos >= totalSize || queuedCount == 0) {
        spiSelectSD();
        SD.remove(QUEUE_FILE);
        spiSelectTFT();
        queuedCount = 0;
        queueReadPos = 0;
      }
      drawStatus();
    }
  } else {
    queueReadPos = nextPos;
  }
}

void wifiTask() {
  if (!WIFI_ENABLED) return;
  if (wifiMode == 0 || wifiCleared) return;

  static bool wasConnected = false;
  bool connected = (WiFi.status() == WL_CONNECTED);

  if (connected && !wasConnected) {
    wifiMode = 1;
  } else if (!connected && wasConnected) {
    wifiMode = 0;
    startAPMode();
  }
  wasConnected = connected;
}

void ntpTask() {
  if (!WIFI_ENABLED || WiFi.status() != WL_CONNECTED) {
    ntpActive = false;
    return;
  }
  if (!ntpActive) {
    bool due = !everSynced || (millis() - lastNtpSync > NTP_RESYNC_MS);
    if (due && (lastNtpTry == 0 || millis() - lastNtpTry > 60000)) {
      lastNtpTry = millis();
      configTime(GMT_OFFSET_SEC, DST_OFFSET_SEC, "pool.ntp.org", "time.google.com", "time.windows.com");
      ntpActive = true;
      ntpStartMs = millis();
    }
    return;
  }

  struct tm t;
  if (getLocalTime(&t, 0) && (t.tm_year + 1900 >= 2024)) {
    everSynced = true;
    lastNtpSync = millis();
    ntpActive = false;
    saveTimeToNvs();
  } else if (millis() - ntpStartMs > 20000) {
    ntpActive = false;
  }
}

void sendRelayCommandToNode(const char* targetNodeId = "ND1");
void saveConfigToPrefs();

void mqttCallback(char* topic, byte* payload, unsigned int length) {
  (void)topic;
  char msg[384];
  unsigned int copyLen = (length < sizeof(msg) - 1) ? length : (sizeof(msg) - 1);
  memcpy(msg, payload, copyLen);
  msg[copyLen] = 0;
  String s = String(msg);

  int rPos = s.indexOf("\"gw_relay_cmd\":\"");
  if (rPos >= 0) {
    int start = rPos + 16;
    int end = s.indexOf("\"", start);
    if (end > start) {
      String cmdVal = s.substring(start, end);
      int colon = cmdVal.indexOf(':');
      int atSign = cmdVal.indexOf('@');
      if (colon > 0) {
        String targetNode = cmdVal.substring(0, colon);
        String statesStr = (atSign > colon) ? cmdVal.substring(colon + 1, atSign) : cmdVal.substring(colon + 1);
        int c1 = statesStr.indexOf(',');
        int c2 = statesStr.indexOf(',', c1 + 1);
        if (c1 > 0 && c2 > c1) {
          relays[0].state = (statesStr.substring(0, c1).toInt() != 0);
          relays[1].state = (statesStr.substring(c1 + 1, c2).toInt() != 0);
          relays[2].state = (statesStr.substring(c2 + 1).toInt() != 0);
          sendRelayCommandToNode(targetNode.c_str());
        }
      }
    }
  }

  int tPos = s.indexOf("\"gw_thresholds\":\"");
  if (tPos >= 0) {
    int start = tPos + 17;
    int end = s.indexOf("\"", start);
    if (end > start) {
      String thStr = s.substring(start, end);
      int idx = 0, p0 = 0;
      while (idx < S_COUNT && p0 < (int)thStr.length()) {
        int semi = thStr.indexOf(';', p0);
        if (semi < 0) semi = thStr.length();
        String pair = thStr.substring(p0, semi);
        int comma = pair.indexOf(',');
        if (comma > 0) {
          float mn = pair.substring(0, comma).toFloat();
          float mx = pair.substring(comma + 1).toFloat();
          SENSOR_LIMITS[idx].minVal = mn;
          if (mx < 9000.0f) {
            SENSOR_LIMITS[idx].maxVal = mx;
            SENSOR_LIMITS[idx].hasMax = true;
          } else {
            SENSOR_LIMITS[idx].hasMax = false;
          }
        }
        idx++;
        p0 = semi + 1;
      }
      saveConfigToPrefs();
    }
  }
}

void tbTask() {
  if (!WIFI_ENABLED || !TB_ENABLED || WiFi.status() != WL_CONNECTED) return;

  if (mqttClient.connected()) {
    mqttClient.loop();
    return;
  }

  if (lastMqttTry == 0 || millis() - lastMqttTry >= 10000) {
    lastMqttTry = millis();
    String clientId = "WaterGW-" + String((uint32_t)ESP.getEfuseMac(), HEX);
    if (mqttClient.connect(clientId.c_str(), TB_TOKEN, NULL)) {
      mqttClient.subscribe("v1/devices/me/attributes");
      mqttClient.subscribe("v1/devices/me/attributes/response/+");
      mqttClient.publish("v1/devices/me/attributes/request/1", "{\"sharedKeys\":\"gw_thresholds\"}");
      updateWifiIcon(true);
    }
  }
}

void sendOrQueueNodeData(int nodeIdx) {
  NodeData &nd = nodes[nodeIdx];
  const char* id = nd.id;

  char valuesJson[200];
  int vPos = snprintf(valuesJson, sizeof(valuesJson), "{\"%s_seq\":%lu", id, nd.seq);
  for (int i = 0; i < S_COUNT; i++) {
    if (nd.valid[i]) {
      if (i == S_EC) {

        vPos += snprintf(valuesJson + vPos, sizeof(valuesJson) - vPos, ",\"%s_%s\":%.0f",
                         id, SENSOR_META[i].tbKey, nd.rawEc_uS);
      } else {
        vPos += snprintf(valuesJson + vPos, sizeof(valuesJson) - vPos, ",\"%s_%s\":%.*f",
                         id, SENSOR_META[i].tbKey, SENSOR_META[i].decimals, nd.values[i]);
      }
    }
  }
  snprintf(valuesJson + vPos, sizeof(valuesJson) - vPos, "}");

  char fullPayload[280];
  uint64_t tsMs = getEpochMillis();
  if (tsMs > 0) {
    snprintf(fullPayload, sizeof(fullPayload), "{\"ts\":%llu,\"values\":%s}",
             (unsigned long long)tsMs, valuesJson);
  } else {
    snprintf(fullPayload, sizeof(fullPayload), "%s", valuesJson);
  }

  bool online = WIFI_ENABLED && TB_ENABLED && (WiFi.status() == WL_CONNECTED) && mqttClient.connected();
  if (online && queuedCount == 0) {
    if (mqttClient.publish(TB_TOPIC, fullPayload)) {
      tbTxCount++;
      return;
    }
  }

  enqueueOfflinePayload(fullPayload);
}

String escapeJsonText(const String& s) {
  String out = "";
  for (size_t i = 0; i < s.length(); i++) {
    char c = s[i];
    if (c == '\"') out += "\\\"";
    else if (c == '\\') out += "\\\\";
    else if (c == '\n') out += "\\n";
    else if (c == '\r') continue;
    else out += c;
  }
  return out;
}

bool isSensorOutOfRange(int sIdx, float val) {
  if (val < SENSOR_LIMITS[sIdx].minVal) return true;
  if (SENSOR_LIMITS[sIdx].hasMax && val > SENSOR_LIMITS[sIdx].maxVal) return true;
  return false;
}

bool sendZaloMessage(const String& textMsg) {
  if (strlen(ZALO_BOT_TOKEN) == 0 || strncmp(ZALO_BOT_TOKEN, "THAY_", 5) == 0 ||
      strlen(ZALO_CHAT_ID) == 0   || strncmp(ZALO_CHAT_ID, "THAY_", 5) == 0) {
    return true;
  }

  if (WiFi.status() != WL_CONNECTED) {
    return false;
  }

  WiFiClientSecure client;
  client.setInsecure();

  HTTPClient http;
  http.setTimeout(8000);
  String url = "https://bot-api.zaloplatforms.com/bot" + String(ZALO_BOT_TOKEN) + "/sendMessage";

  if (!http.begin(client, url)) {
    return false;
  }

  http.addHeader("Content-Type", "application/json");
  String payload = String("{\"chat_id\":\"") + String(ZALO_CHAT_ID) +
                   String("\",\"text\":\"") + escapeJsonText(textMsg) + String("\"}");

  int httpCode = http.POST(payload);
  http.end();

  return (httpCode == HTTP_CODE_OK);
}

void checkAndSendZaloAlert(int nodeIdx) {
  if (!ZALO_ENABLED) return;
  NodeData &nd = nodes[nodeIdx];
  unsigned long now = millis();

  bool triggerSend = false;
  bool isNewBreach[S_COUNT] = {false};
  bool isRepeatBreach[S_COUNT] = {false};
  bool isCurrentlyOver[S_COUNT] = {false};

  for (int i = 0; i < S_COUNT; i++) {
    if (!nd.valid[i]) continue;

    float v = nd.values[i];
    bool over = isSensorOutOfRange(i, v);
    isCurrentlyOver[i] = over;

    if (over) {
      if (!nd.inAlert[i]) {
        isNewBreach[i] = true;
        triggerSend = true;
      } else if (now - nd.lastAlertMs[i] >= ZALO_REPEAT_INTERVAL_MS) {
        isRepeatBreach[i] = true;
        triggerSend = true;
      }
    } else {
      nd.inAlert[i] = false;
    }
  }

  if (!triggerSend) return;

  if (WiFi.status() != WL_CONNECTED) {
    return;
  }

  String msg = String("🚨 CẢNH BÁO [") + String(nd.title) + String(" - ") + String(nd.id) + String("]\n");
  for (int i = 0; i < S_COUNT; i++) {
    if (!isCurrentlyOver[i]) continue;

    const char* statusTag = isNewBreach[i] ? "MỚI VƯỢT NGƯỠNG" :
                            (isRepeatBreach[i] ? "NHẮC LẠI SAU 1 PHÚT" : "ĐANG VƯỢT NGƯỠNG");
    char rangeBuf[48];
    if (SENSOR_LIMITS[i].hasMax) {
      snprintf(rangeBuf, sizeof(rangeBuf), "%.*f ~ %.*f%s",
               SENSOR_META[i].decimals, SENSOR_LIMITS[i].minVal,
               SENSOR_META[i].decimals, SENSOR_LIMITS[i].maxVal,
               SENSOR_META[i].zaloUnit);
    } else {
      snprintf(rangeBuf, sizeof(rangeBuf), ">= %.*f%s",
               SENSOR_META[i].decimals, SENSOR_LIMITS[i].minVal,
               SENSOR_META[i].zaloUnit);
    }

    char lineBuf[160];
    snprintf(lineBuf, sizeof(lineBuf), "• %s: %.*f%s (%s | Ngưỡng an toàn: %s)\n",
             SENSOR_META[i].zaloName,
             SENSOR_META[i].decimals, nd.values[i],
             SENSOR_META[i].zaloUnit,
             statusTag,
             rangeBuf);
    msg += String(lineBuf);
  }

  if (sendZaloMessage(msg)) {
    for (int i = 0; i < S_COUNT; i++) {
      if (isNewBreach[i] || isRepeatBreach[i]) {
        nd.inAlert[i] = true;
        nd.lastAlertMs[i] = now;
      }
    }
  }
}

int findNodeIndex(const char* idStr) {
  for (int i = 0; i < NODE_COUNT; i++) {
    if (strcasecmp(idStr, nodes[i].id) == 0) return i;
  }
  return -1;
}

void handleLine(char* line) {
  char* tok[10];
  int n = 0;
  char* p = strtok(line, ",");
  while (p && n < 10) {
    tok[n++] = p;
    p = strtok(NULL, ",");
  }

  int offset = 0;
  if (n != 3 + S_COUNT) return;
  if (strcmp(tok[0], LORA_PSK) != 0) return;
  offset = 1;
  int nodeIdx = findNodeIndex(tok[offset + 0]);
  if (nodeIdx < 0) return;

  NodeData &nd = nodes[nodeIdx];
  nd.seq = strtoul(tok[offset + 1], NULL, 10);
  nd.lastRx = millis();
  nd.everRx = true;
  nd.wasStale = false;

  for (int i = 0; i < S_COUNT; i++) {
    char valStr[14];
    strncpy(valStr, tok[offset + 2 + i], sizeof(valStr) - 1);
    valStr[sizeof(valStr) - 1] = 0;

    if (strcmp(valStr, "nan") == 0 || strcmp(valStr, "-") == 0) {
      nd.valid[i] = false;
    } else {
      float rawVal = atof(valStr);
      if (i == S_EC) {
        nd.values[i] = convertEcToSalinityPpt(rawVal);
        nd.rawEc_uS  = (rawVal <= 100.0f) ? (rawVal * 1000.0f) : rawVal;
      } else {
        nd.values[i] = rawVal;
      }
      nd.valid[i] = true;
    }

    if (!showingWifiPortalScreen && currentScreen == SCREEN_DETAIL && selectedNode == nodeIdx) {
      drawTileValue(nodeIdx, i);
    }
  }

  sendOrQueueNodeData(nodeIdx);
  checkAndSendZaloAlert(nodeIdx);

  if (!showingWifiPortalScreen) {
    if (currentScreen == SCREEN_HOME) {
      drawNodeCard(nodeIdx);
    }
    drawStatus();
  }
}

void pollLoRa() {
  static char line[128];
  static int len = 0;
  while (LoRa.available()) {
    char c = LoRa.read();
    if (c == '\n') {
      if (len > 0) {
        line[len] = 0;
        handleLine(line);
      }
      len = 0;
    } else if (c != '\r') {
      if (len < (int)sizeof(line) - 1) line[len++] = c;
      else len = 0;
    }
  }
}

void sendRelayCommandToNode(const char* targetNodeId) {
  char cmdBuf[64];
  snprintf(cmdBuf, sizeof(cmdBuf), "%s,CMD,%s,%d,%d,%d\n",
           LORA_PSK, targetNodeId,
           relays[0].state ? 1 : 0,
           relays[1].state ? 1 : 0,
           relays[2].state ? 1 : 0);

  LoRa.print(cmdBuf);
  delay(50);
  LoRa.print(cmdBuf);

}

void saveConfigToPrefs() {
  for (int i = 0; i < S_COUNT; i++) {
    char kMin[12], kMax[12];
    snprintf(kMin, sizeof(kMin), "min_%d", i);
    snprintf(kMax, sizeof(kMax), "max_%d", i);
    prefs.putFloat(kMin, SENSOR_LIMITS[i].minVal);
    prefs.putFloat(kMax, SENSOR_LIMITS[i].maxVal);
  }
  for (int r = 0; r < 3; r++) {
    char kEn[12], kOn[12], kOff[12];
    snprintf(kEn, sizeof(kEn), "r_en_%d", r);
    snprintf(kOn, sizeof(kOn), "r_on_%d", r);
    snprintf(kOff, sizeof(kOff), "r_of_%d", r);
    prefs.putBool(kEn, relays[r].timerEn);
    prefs.putUShort(kOn, (uint16_t)(relays[r].onH * 60 + relays[r].onM));
    prefs.putUShort(kOff, (uint16_t)(relays[r].offH * 60 + relays[r].offM));
  }
}

void loadConfigFromPrefs() {
  for (int i = 0; i < S_COUNT; i++) {
    char kMin[12], kMax[12];
    snprintf(kMin, sizeof(kMin), "min_%d", i);
    snprintf(kMax, sizeof(kMax), "max_%d", i);
    if (prefs.isKey(kMin)) SENSOR_LIMITS[i].minVal = prefs.getFloat(kMin, SENSOR_LIMITS[i].minVal);
    if (prefs.isKey(kMax)) SENSOR_LIMITS[i].maxVal = prefs.getFloat(kMax, SENSOR_LIMITS[i].maxVal);
  }
  for (int r = 0; r < 3; r++) {
    char kEn[12], kOn[12], kOff[12];
    snprintf(kEn, sizeof(kEn), "r_en_%d", r);
    snprintf(kOn, sizeof(kOn), "r_on_%d", r);
    snprintf(kOff, sizeof(kOff), "r_of_%d", r);
    if (prefs.isKey(kEn)) relays[r].timerEn = prefs.getBool(kEn, false);
    if (prefs.isKey(kOn)) {
      uint16_t m = prefs.getUShort(kOn, 360);
      relays[r].onH = m / 60;
      relays[r].onM = m % 60;
    }
    if (prefs.isKey(kOff)) {
      uint16_t m = prefs.getUShort(kOff, 420);
      relays[r].offH = m / 60;
      relays[r].offM = m % 60;
    }
  }
}

void relayScheduleTask() {
  unsigned long now = millis();
  bool changed = false;

  for (int r = 0; r < 3; r++) {
    if (relays[r].state && relays[r].countdownEndMs > 0 && (long)(now - relays[r].countdownEndMs) >= 0) {
      relays[r].state = false;
      relays[r].countdownEndMs = 0;
      changed = true;
    }
  }

  struct tm ti;
  if (getValidTime(ti)) {
    int curMinuteOfDay = ti.tm_hour * 60 + ti.tm_min;
    for (int r = 0; r < 3; r++) {
      if (!relays[r].timerEn) continue;
      if (relays[r].lastTriggeredMinute == curMinuteOfDay) continue;

      int onMinute  = relays[r].onH * 60 + relays[r].onM;
      int offMinute = relays[r].offH * 60 + relays[r].offM;

      if (curMinuteOfDay == onMinute && !relays[r].state) {
        relays[r].state = true;
        relays[r].countdownEndMs = 0;
        relays[r].lastTriggeredMinute = curMinuteOfDay;
        changed = true;
      } else if (curMinuteOfDay == offMinute && relays[r].state) {
        relays[r].state = false;
        relays[r].countdownEndMs = 0;
        relays[r].lastTriggeredMinute = curMinuteOfDay;
        changed = true;
      }
    }
  }

  if (changed) {
    sendRelayCommandToNode("ND1");
  }
}

String getGatewayStatusJson() {
  struct tm ti;
  char timeBuf[32] = "--:--:--";
  if (getValidTime(ti)) {
    snprintf(timeBuf, sizeof(timeBuf), "%02d:%02d:%02d %02d/%02d/%04d",
             ti.tm_hour, ti.tm_min, ti.tm_sec, ti.tm_mday, ti.tm_mon + 1, ti.tm_year + 1900);
  }

  String json = "{\"adminUser\":\"" + adminUser + "\",\"time\":\"" + String(timeBuf) + "\",";

  json += "\"thresholds\":[";
  for (int i = 0; i < S_COUNT; i++) {
    if (i > 0) json += ",";
    json += "{\"name\":\"" + String(SENSOR_META[i].zaloName) + "\",";
    json += "\"unit\":\"" + String(SENSOR_META[i].zaloUnit) + "\",";
    json += "\"min\":" + String(SENSOR_LIMITS[i].minVal, (unsigned int)SENSOR_META[i].decimals) + ",";
    json += "\"max\":" + String(SENSOR_LIMITS[i].maxVal, (unsigned int)SENSOR_META[i].decimals) + ",";
    json += "\"hasMax\":" + String(SENSOR_LIMITS[i].hasMax ? "true" : "false") + "}";
  }
  json += "],";

  unsigned long now = millis();
  json += "\"relays\":[";
  for (int r = 0; r < 3; r++) {
    if (r > 0) json += ",";
    char onBuf[8], offBuf[8];
    snprintf(onBuf, sizeof(onBuf), "%02d:%02d", relays[r].onH, relays[r].onM);
    snprintf(offBuf, sizeof(offBuf), "%02d:%02d", relays[r].offH, relays[r].offM);
    long remSec = 0;
    if (relays[r].state && relays[r].countdownEndMs > 0 && (long)(relays[r].countdownEndMs - now) > 0) {
      remSec = (relays[r].countdownEndMs - now) / 1000;
    }
    json += "{\"state\":" + String(relays[r].state ? "true" : "false") + ",";
    json += "\"timerEn\":" + String(relays[r].timerEn ? "true" : "false") + ",";
    json += "\"onTime\":\"" + String(onBuf) + "\",";
    json += "\"offTime\":\"" + String(offBuf) + "\",";
    json += "\"remainSec\":" + String(remSec) + "}";
  }
  json += "],";

  json += "\"nodes\":[";
  for (int n = 0; n < NODE_COUNT; n++) {
    if (n > 0) json += ",";
    bool onl = isNodeOnline(n);
    json += "{\"id\":\"" + String(nodes[n].id) + "\",\"title\":\"" + String(nodes[n].title) + "\",";
    json += "\"online\":" + String(onl ? "true" : "false") + ",\"seq\":" + String(nodes[n].seq) + ",\"sensors\":[";
    for (int s = 0; s < S_COUNT; s++) {
      if (s > 0) json += ",";
      bool vld = onl && nodes[n].valid[s];
      bool alt = vld && isSensorOutOfRange(s, nodes[n].values[s]);
      json += "{\"valid\":" + String(vld ? "true" : "false") + ",";
      json += "\"val\":\"" + String(nodes[n].values[s], (unsigned int)SENSOR_META[s].decimals) + "\",";
      json += "\"alert\":" + String(alt ? "true" : "false") + "}";
    }
    json += "]}";
  }
  json += "]}";
  return json;
}

bool updateGatewayThresholdsFromWeb(WebServer &srv) {
  for (int i = 0; i < S_COUNT; i++) {
    String kMin = "min" + String(i);
    String kMax = "max" + String(i);
    if (srv.hasArg(kMin)) SENSOR_LIMITS[i].minVal = srv.arg(kMin).toFloat();
    if (srv.hasArg(kMax) && SENSOR_LIMITS[i].hasMax) SENSOR_LIMITS[i].maxVal = srv.arg(kMax).toFloat();
  }
  saveConfigToPrefs();
  return true;
}

bool updateGatewayRelayFromWeb(WebServer &srv) {
  int idx = srv.arg("idx").toInt();
  if (idx < 0 || idx > 2) return false;

  if (srv.hasArg("schedOnly")) {
    relays[idx].timerEn = (srv.arg("timerEn").toInt() != 0);
    String onT = srv.arg("onTime");
    String offT = srv.arg("offTime");
    if (onT.length() >= 5) {
      relays[idx].onH = onT.substring(0, 2).toInt();
      relays[idx].onM = onT.substring(3, 5).toInt();
    }
    if (offT.length() >= 5) {
      relays[idx].offH = offT.substring(0, 2).toInt();
      relays[idx].offM = offT.substring(3, 5).toInt();
    }
    relays[idx].lastTriggeredMinute = -1;
    saveConfigToPrefs();
    return true;
  }

  bool newState = (srv.arg("state").toInt() != 0);
  int cdMin = srv.arg("countdown").toInt();
  relays[idx].state = newState;
  if (newState && cdMin > 0) {
    relays[idx].countdownEndMs = millis() + (unsigned long)cdMin * 60000UL;
  } else {
    relays[idx].countdownEndMs = 0;
  }

  sendRelayCommandToNode("ND1");
  return true;
}

void setup() {
  Serial.begin(115200);

  pinMode(BTN_NEXT_PIN, INPUT_PULLUP);
  pinMode(BTN_OK_PIN, INPUT_PULLUP);

  pinMode(SD_CS, OUTPUT);
  digitalWrite(SD_CS, HIGH);
  pinMode(TFT_CS, OUTPUT);
  digitalWrite(TFT_CS, HIGH);

  pinMode(LORA_M0, OUTPUT);
  pinMode(LORA_M1, OUTPUT);
  pinMode(LORA_AUX, INPUT);
  digitalWrite(LORA_M0, LOW);
  digitalWrite(LORA_M1, LOW);
  delay(300);

  LoRa.begin(9600, SERIAL_8N1, LORA_RX_PIN, LORA_TX_PIN);

  prefs.begin("gw_rtc", false);
  restoreTimeFromNvs();
  loadConfigFromPrefs();

  espClient.setTimeout(1);
  mqttClient.setServer(TB_SERVER, TB_PORT);
  mqttClient.setCallback(mqttCallback);
  mqttClient.setSocketTimeout(1);
  mqttClient.setBufferSize(512);

  tft.begin(TFT_SPI_FREQ);
  tft.setRotation(SCREEN_ROTATION);
  tft.cp437(true);

  startSd();

  uint8_t macAddr[6];
  WiFi.softAPmacAddress(macAddr);
  apSsid = "ESP32-" + String(macAddr[4], HEX) + String(macAddr[5], HEX);
  apSsid.toUpperCase();
  showingWifiPortalScreen = true;
  drawWifiPortalScreen();

  if (WIFI_ENABLED) {
    wifiConfigSetup();
    wifiLastBegin = millis();
  }

  if (WiFi.status() == WL_CONNECTED) {
    drawFullMainInterface();
    initOTA();
  } else {
    showingWifiPortalScreen = true;
    drawWifiPortalScreen();
  }

}

void loop() {
  static unsigned long lastSec = 0, lastWifiUi = 0, lastWifiTask = 0;
  unsigned long now = millis();

  if (WIFI_ENABLED) {
    wifiLoop();
    otaLoop();
  }

  checkScreenModeByWifi();
  pollButtons();
  pollLoRa();
  syncOfflineQueueTask();
  relayScheduleTask();

  if (now - lastSec >= 1000) {
    lastSec = now;

    for (int i = 0; i < NODE_COUNT; i++) {
      if (nodes[i].everRx && !nodes[i].wasStale && (now - nodes[i].lastRx > NODE_TIMEOUT_MS)) {
        nodes[i].wasStale = true;
        for (int s = 0; s < S_COUNT; s++) nodes[i].valid[s] = false;

        if (!showingWifiPortalScreen) {
          if (currentScreen == SCREEN_HOME) {
            drawNodeCard(i);
          } else if (currentScreen == SCREEN_DETAIL && selectedNode == i) {
            drawDetailScreen();
          }
        }
      }
    }

    if (!showingWifiPortalScreen) {
      drawClock();
      drawStatus();
    }
  }

  if (now - lastWifiUi >= 150) {
    lastWifiUi = now;
    if (!showingWifiPortalScreen) {
      updateWifiIcon();
    }
  }

  if (now - lastWifiTask >= 500) {
    lastWifiTask = now;
    wifiTask();
    ntpTask();
    tbTask();
  }

  if (everSynced && (now - lastNvsSave >= NVS_SAVE_MS)) {
    lastNvsSave = now;
    saveTimeToNvs();
  }

  if (!sdOk && now - lastSdTry >= 10000) {
    lastSdTry = now;
    startSd();
  }
}
