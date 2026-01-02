#include <Arduino.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include <SPI.h>
#include <math.h>

#include "epd2in66g.h"
#include "epdif.h"

#ifndef WIFI_SSID
#error "WIFI_SSID is not defined. Create secrets.ini and set [secrets] WIFI_SSID"
#endif
#ifndef WIFI_PASS
#error "WIFI_PASS is not defined. Create secrets.ini and set [secrets] WIFI_PASS"
#endif
#ifndef MQTT_HOST
#error "MQTT_HOST is not defined. Create secrets.ini and set [secrets] MQTT_HOST"
#endif
#ifndef MQTT_PORT
#error "MQTT_PORT is not defined. Create secrets.ini and set [secrets] MQTT_PORT"
#endif

// ===================== USER CONFIG =====================
// WiFi
static const char* WIFI_SSID_S = WIFI_SSID;
static const char* WIFI_PASS_S = WIFI_PASS;

// MQTT (Home Assistant broker)
static const char* MQTT_HOST_S = MQTT_HOST;   // <-- CHANGE to your broker IP
static const uint16_t MQTT_PORT_U16 = (uint16_t)MQTT_PORT;
static const char* MQTT_USER_S = MQTT_USER;           // optional
static const char* MQTT_PASS_S = MQTT_PASS;              // optional

// Topic that HA publishes (integer, retained)
static const char* TOPIC_TEMP_INT = "boiler/temp_int";

// --- Transition behavior ---
static const bool ENABLE_COLOR_TRANSITION_EVERY_UPDATE = true;
static const uint16_t TRANSITION_DELAY_MS = 250;

// --- Deep sleep (optional) ---
static const bool USE_DEEP_SLEEP = false;
static const uint32_t SLEEP_SECONDS = 900;

// ===================== BATTERY CONFIG =====================
static const bool ENABLE_BATTERY_ICON = false;

static const int  BAT_ADC_PIN = 34;     // ADC1 pin recommended
static const float ADC_VREF = 3.3f;
static const float ADC_MAX = 4095.0f;

// Divider ratio: Vbat = Vadc * DIVIDER_RATIO
static const float DIVIDER_RATIO = 2.0f;

// crude Li-ion mapping
static const float VBAT_EMPTY = 3.20f;
static const float VBAT_FULL  = 4.15f;

// ===================== DISPLAY =====================
static const int W = EPD_WIDTH;    // 184
static const int H = EPD_HEIGHT;   // 360
static const int ROW_BYTES = (W % 4 == 0) ? (W / 4) : (W / 4 + 1);
static uint8_t img[ROW_BYTES * H];

// Waveshare 2.66G colors (from your driver)
static const uint8_t C_BLACK  = black;
static const uint8_t C_WHITE  = white;
static const uint8_t C_YELLOW = yellow;
static const uint8_t C_RED    = red;

// ===================== LANDSCAPE COORD SYSTEM =====================
// Logical landscape coordinates: 360x184
// Mapping: 90° clockwise => physical x=LY, physical y=H-1-LX
static inline void set_px_l(int lx, int ly, uint8_t c) {
  int x = ly;
  int y = H - 1 - lx;
  if (x < 0 || x >= W || y < 0 || y >= H) return;

  int byteIndex = y * ROW_BYTES + (x / 4);
  int shift = (3 - (x % 4)) * 2;
  img[byteIndex] =
    (img[byteIndex] & ~(0x3 << shift)) | ((c & 0x3) << shift);
}

static void fill(uint8_t c) {
  uint8_t v =
    ((c & 0x3) << 6) |
    ((c & 0x3) << 4) |
    ((c & 0x3) << 2) |
    (c & 0x3);
  memset(img, v, sizeof(img));
}

static void rect_l(int x, int y, int w, int h, uint8_t c) {
  for (int yy = y; yy < y + h; yy++)
    for (int xx = x; xx < x + w; xx++)
      set_px_l(xx, yy, c);
}

static void hline_l(int x, int y, int w, uint8_t c) { rect_l(x, y, w, 1, c); }
static void vline_l(int x, int y, int h, uint8_t c) { rect_l(x, y, 1, h, c); }

static void border_l(uint8_t c) {
  const int CANVAS_W = 360;
  const int CANVAS_H = 184;
  hline_l(0, 0, CANVAS_W, c);
  hline_l(0, CANVAS_H - 1, CANVAS_W, c);
  vline_l(0, 0, CANVAS_H, c);
  vline_l(CANVAS_W - 1, 0, CANVAS_H, c);
}

// ===================== SIMPLE 5x7 BLOCK FONT =====================
static void draw_char_5x7_l(int x, int y, char ch, int scale, uint8_t c) {
  const uint8_t* rows = nullptr;

  static const uint8_t B_[7] = {0b11110,0b10001,0b10001,0b11110,0b10001,0b10001,0b11110};
  static const uint8_t O_[7] = {0b01110,0b10001,0b10001,0b10001,0b10001,0b10001,0b01110};
  static const uint8_t I_[7] = {0b11111,0b00100,0b00100,0b00100,0b00100,0b00100,0b11111};
  static const uint8_t L_[7] = {0b10000,0b10000,0b10000,0b10000,0b10000,0b10000,0b11111};
  static const uint8_t E_[7] = {0b11111,0b10000,0b10000,0b11110,0b10000,0b10000,0b11111};
  static const uint8_t R_[7] = {0b11110,0b10001,0b10001,0b11110,0b10100,0b10010,0b10001};
  static const uint8_t T_[7] = {0b11111,0b00100,0b00100,0b00100,0b00100,0b00100,0b00100};
  static const uint8_t M_[7] = {0b10001,0b11011,0b10101,0b10101,0b10001,0b10001,0b10001};
  static const uint8_t P_[7] = {0b11110,0b10001,0b10001,0b11110,0b10000,0b10000,0b10000};
  static const uint8_t A_[7] = {0b01110,0b10001,0b10001,0b11111,0b10001,0b10001,0b10001};
  static const uint8_t C_[7] = {0b01111,0b10000,0b10000,0b10000,0b10000,0b10000,0b01111};
  static const uint8_t SPC[7] = {0,0,0,0,0,0,0};

  switch (ch) {
    case 'B': rows = B_; break;
    case 'O': rows = O_; break;
    case 'I': rows = I_; break;
    case 'L': rows = L_; break;
    case 'E': rows = E_; break;
    case 'R': rows = R_; break;
    case 'T': rows = T_; break;
    case 'M': rows = M_; break;
    case 'P': rows = P_; break;
    case 'A': rows = A_; break;
    case 'C': rows = C_; break;
    default: rows = SPC; break;
  }

  for (int ry = 0; ry < 7; ry++) {
    uint8_t bits = rows[ry];
    for (int rx = 0; rx < 5; rx++) {
      if (bits & (1 << (4 - rx))) {
        rect_l(x + rx*scale, y + ry*scale, scale, scale, c);
      }
    }
  }
}

static int text_width_5x7(const char* s, int scale, int spacing) {
  int n = (int)strlen(s);
  if (n == 0) return 0;
  return n * (5*scale + spacing) - spacing;
}

static void draw_text_5x7_l(int x, int y, const char* s, int scale, int spacing, uint8_t c) {
  int cx = x;
  for (const char* p = s; *p; p++) {
    draw_char_5x7_l(cx, y, *p, scale, c);
    cx += (5*scale + spacing);
  }
}

// ===================== 7-SEG DIGITS =====================
static void draw_digit7seg_l(int x, int y, int s, int d, uint8_t c) {
  bool seg[7] = {0};
  switch (d) {
    case 0: seg[0]=seg[1]=seg[2]=seg[3]=seg[4]=seg[5]=1; break;
    case 1: seg[1]=seg[2]=1; break;
    case 2: seg[0]=seg[1]=seg[6]=seg[4]=seg[3]=1; break;
    case 3: seg[0]=seg[1]=seg[6]=seg[2]=seg[3]=1; break;
    case 4: seg[5]=seg[6]=seg[1]=seg[2]=1; break;
    case 5: seg[0]=seg[5]=seg[6]=seg[2]=seg[3]=1; break;
    case 6: seg[0]=seg[5]=seg[6]=seg[2]=seg[3]=seg[4]=1; break;
    case 7: seg[0]=seg[1]=seg[2]=1; break;
    case 8: for (int i=0;i<7;i++) seg[i]=1; break;
    case 9: seg[0]=seg[1]=seg[2]=seg[3]=seg[5]=seg[6]=1; break;
    default: break;
  }

  int t = s;
  int w = 6*s;
  int h = 10*s;

  if (seg[0]) rect_l(x + t, y, w - 2*t, t, c);
  if (seg[1]) rect_l(x + w - t, y + t, t, (h/2) - t, c);
  if (seg[2]) rect_l(x + w - t, y + h/2, t, (h/2) - t, c);
  if (seg[3]) rect_l(x + t, y + h - t, w - 2*t, t, c);
  if (seg[4]) rect_l(x, y + h/2, t, (h/2) - t, c);
  if (seg[5]) rect_l(x, y + t, t, (h/2) - t, c);
  if (seg[6]) rect_l(x + t, y + (h/2) - (t/2), w - 2*t, t, c);
}

static void draw_degC_icon_l(int x, int y, uint8_t fg, uint8_t bg) {
  // degree box
  rect_l(x,     y,     6, 6, fg);
  rect_l(x + 2, y + 2, 2, 2, bg);

  // "C" block
  int cx = x + 10;
  int cy = y + 2;
  rect_l(cx, cy,     14, 3, fg);
  rect_l(cx, cy,      3, 16, fg);
  rect_l(cx, cy + 13, 14, 3, fg);
}

// ===================== ARROWS =====================
enum ArrowDir : uint8_t { ARROW_NONE=0, ARROW_UP=1, ARROW_DOWN=2 };

// Small triangle arrow (filled) in logical landscape coords
static void draw_arrow_up_l(int x, int y, int size, uint8_t c) {
  // apex at top center
  for (int r = 0; r < size; r++) {
    int w = 1 + 2*r;
    int start = x - r;
    rect_l(start, y + r, w, 1, c);
  }
  // small stem
  rect_l(x - 1, y + size, 3, size + 2, c);
}

static void draw_arrow_down_l(int x, int y, int size, uint8_t c) {
  // apex at bottom center
  for (int r = 0; r < size; r++) {
    int w = 1 + 2*r;
    int start = x - r;
    rect_l(start, y + (size - 1 - r), w, 1, c);
  }
  // small stem above
  rect_l(x - 1, y - (size + 2), 3, size + 2, c);
}

// ===================== THEME BY TEMP =====================
struct Theme { uint8_t header_bg; uint8_t header_fg; };

static Theme theme_for_temp(int t) {
  // your existing rule
  if (t > 41) return Theme{C_RED, C_WHITE};
  if (t > 36 && t <= 41) return Theme{C_YELLOW, C_BLACK};
  // requested: low temp header black w/ white text
  return Theme{C_BLACK, C_WHITE};
}

// ===================== BATTERY % (ADC) =====================
static int read_battery_percent() {
  if (!ENABLE_BATTERY_ICON) return -1;

  uint32_t sum = 0;
  const int N = 20;
  for (int i = 0; i < N; i++) {
    sum += analogRead(BAT_ADC_PIN);
    delay(2);
  }
  float raw = (float)sum / (float)N;
  float v_adc = (raw / ADC_MAX) * ADC_VREF;
  float v_bat = v_adc * DIVIDER_RATIO;

  float pctf = (v_bat - VBAT_EMPTY) / (VBAT_FULL - VBAT_EMPTY) * 100.0f;
  if (pctf < 0) pctf = 0;
  if (pctf > 100) pctf = 100;
  return (int)lroundf(pctf);
}

// ===================== E-PAPER =====================
Epd epd;

// keep last displayed temp across deep sleep
RTC_DATA_ATTR int rtc_lastDisplayed = -9999;

static void draw_screen_frame(int tempC, int batteryPct, ArrowDir dir, uint8_t header_bg_override = 255) {
  const int CANVAS_W = 360;
  const int CANVAS_H = 184;

  int t = tempC;
  if (t < 0) t = 0;
  if (t > 99) t = 99;

  Theme th = theme_for_temp(t);
  uint8_t header_bg = (header_bg_override == 255) ? th.header_bg : header_bg_override;

  // Choose readable header fg
  uint8_t header_fg = th.header_fg;
  if (header_bg_override != 255) {
    // if overriding to white for transition, use black text
    if (header_bg_override == C_WHITE) header_fg = C_BLACK;
  }

  // Body background always white (easier for readability)
  fill(C_WHITE);

  // Border matches header fg (clean + consistent)
  border_l(header_fg);

  // Header
  const int HEADER_H = 28;
  rect_l(1, 1, CANVAS_W - 2, HEADER_H, header_bg);

  // Separator line under header (1px like border)
  rect_l(1, 1 + HEADER_H, CANVAS_W - 2, 1, header_fg);

  // "BOILER" centered with balanced padding
  {
    const char* title = "BOILER";
    int scale = 3;
    int spacing = 3;
    int title_w = text_width_5x7(title, scale, spacing);
    int tx = (CANVAS_W - title_w) / 2;

    int text_h = 7 * scale;
    int header_top = 1;
    int header_bottom = 1 + HEADER_H;
    int available_h = header_bottom - header_top;
    int ty = header_top + (available_h - text_h) / 2;

    if (ty < header_top + 2) ty = header_top + 2;
    draw_text_5x7_l(tx, ty, title, scale, spacing, header_fg);
  }

  // digits sizes (2 digits)
  int tens = t / 10;
  int ones = t % 10;

  int s = 9;
  int digit_w = 6*s;
  int digit_h = 10*s;
  int gap = 22;

  int icon_w = 28;
  int icon_gap = 12;

  int digits_width = 2*digit_w + gap;
  int group_width = digits_width + icon_gap + icon_w;
  int start_x = (CANVAS_W - group_width) / 2;

  // body layout
  int top = 1 + HEADER_H + 1 + 10;
  int avail_h = (CANVAS_H - 1) - top;
  int y_digits = top + (avail_h - digit_h) / 2 + 6;

  // "TEMP" label above digits (uses header_fg to keep nice contrast)
  {
    const char* label = "TEMP";
    int scale = 2;
    int spacing = 2;
    int label_w = text_width_5x7(label, scale, spacing);
    int lx = (CANVAS_W - label_w) / 2;

    int ly = y_digits - (7*scale) - 10;
    int min_ly = 1 + HEADER_H + 1 + 4;
    if (ly < min_ly) ly = min_ly;

    draw_text_5x7_l(lx, ly, label, scale, spacing, header_fg);
  }

  // digits
  int x = start_x;
  draw_digit7seg_l(x, y_digits, s, tens, C_BLACK);
  x += digit_w + gap;
  draw_digit7seg_l(x, y_digits, s, ones, C_BLACK);

  // °C icon
  int digits_end_x = start_x + digits_width;
  int icon_x = digits_end_x + icon_gap;
  int icon_y = y_digits + 10;

  if (icon_x + icon_w > CANVAS_W - 1) icon_x = (CANVAS_W - 1) - icon_w;
  if (icon_y + 20 > CANVAS_H - 1) icon_y = (CANVAS_H - 1) - 20;

  draw_degC_icon_l(icon_x, icon_y, C_BLACK, C_WHITE);

  // Arrow indicator: to the right of digits, below the °C icon
  // (your request: right of temps + below the °C icon)
  if (dir != ARROW_NONE) {
    int ax = icon_x + icon_w/2;     // centered under the icon
    int ay = icon_y + 28;           // below the °C icon
    int size = 6;                   // small arrow
    // keep inside screen
    if (ay + size + 12 > CANVAS_H - 2) ay = CANVAS_H - 2 - (size + 12);

    if (dir == ARROW_UP)   draw_arrow_up_l(ax, ay, size, C_BLACK);
    if (dir == ARROW_DOWN) draw_arrow_down_l(ax, ay, size, C_RED);
  }

  // Battery icon (top-right in header)
  if (ENABLE_BATTERY_ICON && batteryPct >= 0) {
    int bw = 32;
    int bh = 12;
    int bx = CANVAS_W - 1 - 6 - bw - 4;
    int by = 1 + 7;
    // battery fg/bg match header readability
    rect_l(bx, by, bw, bh, header_fg);
    rect_l(bx + 1, by + 1, bw - 2, bh - 2, header_bg);
    int nub_w = max(2, bw / 10);
    int nub_h = max(4, bh / 2);
    rect_l(bx + bw, by + (bh - nub_h) / 2, nub_w, nub_h, header_fg);

    int inner_w = bw - 2;
    int inner_h = bh - 2;
    int fill_w = (inner_w * batteryPct) / 100;
    rect_l(bx + 1, by + 1, fill_w, inner_h, header_fg);
    if (batteryPct > 0 && fill_w == 0) rect_l(bx + 1, by + 1, 1, inner_h, header_fg);
  }
}

static void show_temp_on_epaper(int tempC, ArrowDir dir) {
  int batteryPct = read_battery_percent();

  Serial.printf("[EPD] start update -> %d\n", tempC);
  Serial.println("[EPD] Init()");
  if (epd.Init() != 0) return;

  Serial.println("[EPD] Clear()");
  epd.Clear(C_WHITE);

  if (ENABLE_COLOR_TRANSITION_EVERY_UPDATE) {
    Serial.println("[EPD] Frame1 (white header)");
    draw_screen_frame(tempC, batteryPct, dir, C_WHITE);
    epd.Display(img);
    delay(TRANSITION_DELAY_MS);

    Serial.println("[EPD] Frame2 (final header)");
    Theme th = theme_for_temp(tempC);
    draw_screen_frame(tempC, batteryPct, dir, th.header_bg);
    epd.Display(img);
  } else {
    Theme th = theme_for_temp(tempC);
    draw_screen_frame(tempC, batteryPct, dir, th.header_bg);
    epd.Display(img);
  }

  Serial.println("[EPD] Sleep()");
  epd.Sleep();
  Serial.println("[EPD] done");
}

// ===================== WIFI + MQTT =====================
WiFiClient wifiClient;
PubSubClient mqtt(wifiClient);

static void ensureWifi() {
  if (WiFi.status() == WL_CONNECTED) return;
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID_S, WIFI_PASS_S);
  while (WiFi.status() != WL_CONNECTED) delay(500);
  Serial.printf("WiFi OK, IP: %s\n", WiFi.localIP().toString().c_str());
}

static void ensureMqtt() {
  if (mqtt.connected()) return;
  Serial.print("Connecting MQTT...");
  while (!mqtt.connected()) {
    String cid = "boiler-epd-" + String((uint32_t)ESP.getEfuseMac(), HEX);
    bool ok = false;
    if (strlen(MQTT_USER_S)) ok = mqtt.connect(cid.c_str(), MQTT_USER_S, MQTT_PASS_S);
    else ok = mqtt.connect(cid.c_str());

    if (ok) {
      Serial.println("OK");
      Serial.printf("Subscribing: %s\n", TOPIC_TEMP_INT);
      mqtt.subscribe(TOPIC_TEMP_INT);
    } else {
      Serial.println("FAIL, retrying...");
      delay(2000);
    }
  }
}

static bool parseIntPayload(const byte* payload, unsigned int len, int& out) {
  if (len == 0 || len > 32) return false;
  char buf[40];
  memcpy(buf, payload, len);
  buf[len] = '\0';

  if (!strcmp(buf, "unknown") || !strcmp(buf, "unavailable") || !strcmp(buf, "None") ||
      !strcmp(buf, "null") || !strcmp(buf, "nan") || !strcmp(buf, "NaN")) {
    return false;
  }

  char* endp = nullptr;
  long v = strtol(buf, &endp, 10);
  if (endp == buf) return false;
  out = (int)v;
  return true;
}

static void onMqtt(char* topic, byte* payload, unsigned int len) {
  if (strcmp(topic, TOPIC_TEMP_INT) != 0) return;

  // log raw payload
  char tmp[64];
  unsigned int n = min(len, (unsigned int)63);
  memcpy(tmp, payload, n);
  tmp[n] = '\0';
  Serial.printf("MQTT msg topic: %s\n", topic);
  Serial.printf("MQTT payload raw: '%s'\n", tmp);

  int t = 0;
  if (!parseIntPayload(payload, len, t)) {
    Serial.println("MQTT payload invalid (ignored)");
    return;
  }

  if (t == rtc_lastDisplayed) {
    Serial.println("Temp unchanged (ignored)");
    return;
  }

  // Determine direction for arrow
  ArrowDir dir = ARROW_NONE;
  if (rtc_lastDisplayed != -9999) {
    if (t > rtc_lastDisplayed) dir = ARROW_UP;
    else if (t < rtc_lastDisplayed) dir = ARROW_DOWN;
  }

  Serial.printf("Temp changed: %d -> %d. Queuing update...\n", rtc_lastDisplayed, t);
  rtc_lastDisplayed = t;

  Serial.printf("[MAIN] Applying temp %d\n", t);
  show_temp_on_epaper(t, dir);

  if (USE_DEEP_SLEEP) {
    esp_sleep_enable_timer_wakeup((uint64_t)SLEEP_SECONDS * 1000000ULL);
    esp_deep_sleep_start();
  }
}

// ===================== ARDUINO =====================
void setup() {
  Serial.begin(115200);
  delay(500);

  // SPI pins (match your wiring; CS is controlled by epdif/CS_PIN)
  SPI.begin(18, -1, 23, CS_PIN);

  if (ENABLE_BATTERY_ICON) {
    analogReadResolution(12);
    analogSetPinAttenuation(BAT_ADC_PIN, ADC_11db);
    pinMode(BAT_ADC_PIN, INPUT);
  }

  ensureWifi();
  mqtt.setServer(MQTT_HOST_S, MQTT_PORT_U16);
  mqtt.setCallback(onMqtt);
  ensureMqtt();

  Serial.println("Setup done. Waiting for MQTT updates...");
}

void loop() {
  ensureWifi();
  ensureMqtt();
  mqtt.loop();

  if (USE_DEEP_SLEEP) {
    static uint32_t start = millis();
    if (millis() - start > 15000) {
      Serial.println("No update received, going to sleep.");
      esp_sleep_enable_timer_wakeup((uint64_t)SLEEP_SECONDS * 1000000ULL);
      esp_deep_sleep_start();
    }
  }

  delay(10);
}