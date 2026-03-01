#include "UITask.h"
#include <helpers/TxtDataHelpers.h>
#include "../MyMesh.h"
#include "target.h"
#if defined(TOUCH_CS) && defined(TOUCH_IRQ) && defined(TOUCH_SPI_SCK) && defined(TOUCH_SPI_MOSI) && defined(TOUCH_SPI_MISO)
  #include <SPI.h>
  #include <XPT2046_Touchscreen.h>
#endif
#ifdef WIFI_SSID
  #include <WiFi.h>
#endif

#ifndef AUTO_OFF_MILLIS
  #define AUTO_OFF_MILLIS     15000   // 15 seconds
#endif
#define BOOT_SCREEN_MILLIS   3000   // 3 seconds

#ifdef PIN_STATUS_LED
#define LED_ON_MILLIS     20
#define LED_ON_MSG_MILLIS 200
#define LED_CYCLE_MILLIS  4000
#endif

#define LONG_PRESS_MILLIS   1200

#ifndef UI_RECENT_LIST_SIZE
  #define UI_RECENT_LIST_SIZE 4
#endif

#if UI_HAS_JOYSTICK
  #define PRESS_LABEL "press Enter"
#else
  #define PRESS_LABEL "long press"
#endif

#if defined(TOUCH_CS) && defined(TOUCH_IRQ) && defined(TOUCH_SPI_SCK) && defined(TOUCH_SPI_MOSI) && defined(TOUCH_SPI_MISO)
  #ifndef TOUCH_ROTATION
    #define TOUCH_ROTATION 1
  #endif
  #ifndef TOUCH_X_MIN
    #define TOUCH_X_MIN 200
  #endif
  #ifndef TOUCH_X_MAX
    #define TOUCH_X_MAX 3900
  #endif
  #ifndef TOUCH_Y_MIN
    #define TOUCH_Y_MIN 200
  #endif
  #ifndef TOUCH_Y_MAX
    #define TOUCH_Y_MAX 3900
  #endif
  #ifndef TOUCH_DOT_MILLIS
    #define TOUCH_DOT_MILLIS 220
  #endif

  static SPIClass xpt2046_spi(HSPI);
  static XPT2046_Touchscreen xpt2046(TOUCH_CS, TOUCH_IRQ);
  static bool xpt2046_ready = false;
  static bool xpt2046_was_down = false;
  static uint32_t xpt2046_last_tap_millis = 0;
  static int touch_dot_x = -1;
  static int touch_dot_y = -1;
  static uint32_t touch_dot_until = 0;

  static float normalizeTouchAxis(int raw, int in_min, int in_max) {
    if (in_min == in_max) return 0.0f;
    float n = (float)(raw - in_min) / (float)(in_max - in_min);
    if (n < 0.0f) n = 0.0f;
    if (n > 1.0f) n = 1.0f;
    return n;
  }

  static void mapRawTouchToDisplay(DisplayDriver* display, int raw_x, int raw_y, int* x, int* y) {
    if (display == NULL) {
      *x = 0;
      *y = 0;
      return;
    }

    float u = normalizeTouchAxis(raw_x, TOUCH_X_MIN, TOUCH_X_MAX);
    float v = normalizeTouchAxis(raw_y, TOUCH_Y_MIN, TOUCH_Y_MAX);

    // Rotate touch frame by 90 degrees clockwise.
    float u_rot = v;
    float v_rot = 1.0f - u;

    int max_x = display->width() - 1;
    int max_y = display->height() - 1;
    *x = (int)(u_rot * max_x + 0.5f);
    *y = (int)(v_rot * max_y + 0.5f);

    if (*x < 0) *x = 0;
    if (*x > max_x) *x = max_x;
    if (*y < 0) *y = 0;
    if (*y > max_y) *y = max_y;
  }

  static char mapXpt2046TouchToKey(DisplayDriver* display, int raw_x, int raw_y) {
    if (display == NULL) return KEY_NEXT;
    int x = 0, y = 0;
    mapRawTouchToDisplay(display, raw_x, raw_y, &x, &y);
    int left_cutoff = display->width() / 3;
    int right_cutoff = (display->width() * 2) / 3;
    if (x < left_cutoff) return KEY_PREV;
    if (x > right_cutoff) return KEY_NEXT;
    return KEY_ENTER;
  }

  static void updateTouchDot(DisplayDriver* display, int raw_x, int raw_y) {
    if (display == NULL) return;
    mapRawTouchToDisplay(display, raw_x, raw_y, &touch_dot_x, &touch_dot_y);
    touch_dot_until = millis() + TOUCH_DOT_MILLIS;
  }

  static void drawTouchDotPixel(DisplayDriver& display, int x, int y) {
    if (x < 0 || y < 0 || x >= display.width() || y >= display.height()) return;
    display.fillRect(x, y, 1, 1);
  }

  static void drawTouchDebugDot(DisplayDriver& display) {
    if ((int32_t)(touch_dot_until - millis()) <= 0) return;
    if (touch_dot_x < 0 || touch_dot_y < 0) return;

    display.setColor(DisplayDriver::LIGHT);
    drawTouchDotPixel(display, touch_dot_x, touch_dot_y - 2);
    drawTouchDotPixel(display, touch_dot_x + 1, touch_dot_y - 1);
    drawTouchDotPixel(display, touch_dot_x + 2, touch_dot_y);
    drawTouchDotPixel(display, touch_dot_x + 1, touch_dot_y + 1);
    drawTouchDotPixel(display, touch_dot_x, touch_dot_y + 2);
    drawTouchDotPixel(display, touch_dot_x - 1, touch_dot_y + 1);
    drawTouchDotPixel(display, touch_dot_x - 2, touch_dot_y);
    drawTouchDotPixel(display, touch_dot_x - 1, touch_dot_y - 1);
    drawTouchDotPixel(display, touch_dot_x, touch_dot_y);
  }
#endif

#include "icons.h"

// 8x8 tab icons for compact left-side rail
static const uint8_t tab_icon_messages[] = {
  0x00, 0x7E, 0x42, 0x5A, 0x66, 0x42, 0x7E, 0x00
};
static const uint8_t tab_icon_nearby[] = {
  0x10, 0x38, 0x7C, 0x10, 0x10, 0x28, 0x44, 0x00
};
static const uint8_t tab_icon_radio[] = {
  0x10, 0x18, 0x5C, 0x7E, 0x5C, 0x18, 0x10, 0x00
};
static const uint8_t tab_icon_link[] = {
  0x0C, 0x12, 0x30, 0x0C, 0x06, 0x09, 0x30, 0x00
};
static const uint8_t tab_icon_power[] = {
  0x18, 0x18, 0x7E, 0xDB, 0xDB, 0x7E, 0x18, 0x18
};
static const uint8_t tab_icon_setup[] = {
  0x18, 0x3C, 0x7E, 0xFF, 0xFF, 0x7E, 0x3C, 0x18
};

class SplashScreen : public UIScreen {
  UITask* _task;
  unsigned long dismiss_after;
  char _version_info[12];

public:
  SplashScreen(UITask* task) : _task(task) {
    // strip off dash and commit hash by changing dash to null terminator
    // e.g: v1.2.3-abcdef -> v1.2.3
    const char *ver = FIRMWARE_VERSION;
    const char *dash = strchr(ver, '-');

    int len = dash ? dash - ver : strlen(ver);
    if (len >= sizeof(_version_info)) len = sizeof(_version_info) - 1;
    memcpy(_version_info, ver, len);
    _version_info[len] = 0;

    dismiss_after = millis() + BOOT_SCREEN_MILLIS;
  }

  int render(DisplayDriver& display) override {
    display.setColor(DisplayDriver::DARK);
    display.fillRect(0, 0, display.width(), display.height());

    int mid_x = display.width() / 2;

    // 1. MeshCore Logo
    display.setColor(DisplayDriver::LIGHT);
    int logoWidth = 128;
    display.drawXbm(mid_x - 64, 15, meshcore_logo, logoWidth, 13);

    // 2. Base attribution
    display.setTextSize(1);
    char base_info[64];
    snprintf(base_info, sizeof(base_info), "Powered by MeshCore %s", _version_info);
    display.drawTextCentered(mid_x, 35, base_info);

    // 3. Large "TOUCH" Headline
    display.setTextSize(6);
    display.drawTextCentered(mid_x, 85, "TOUCH");

    // 4. Touch Version & Date
    display.setTextSize(2);
    display.drawTextCentered(mid_x, 155, "v1.1.0 - 2026.02.28");

    // 5. Author Credit
    display.setTextSize(1);
    display.drawTextCentered(mid_x, 210, "Created by Quark1980");

    return 2000; // Splash redrawn every 2s is plenty
  }

  void poll() override {
    if (millis() >= dismiss_after) {
      _task->gotoHomeScreen();
    }
  }
};

class HomeScreen : public UIScreen {
public:
  enum Tab { TAB_MESSAGES, TAB_NEARBY, TAB_CHAT, TAB_RADIO, TAB_LINK, TAB_POWER, TAB_SETTINGS, TAB_RAW, TAB_COUNT };
private:
  Tab _tab;

  uint8_t _active_chat_idx;
  bool _active_chat_is_group;
  char _chat_draft[64];
  bool _keyboard_visible;
  int _kb_shift; // 0=lower, 1=upper, 2=symbols/numbers
  int _chat_scroll; // scroll offset for chat messages
  bool _chat_dropdown_open;
  
  bool _show_msg_detail;
  int _msg_cursor;
  int _msg_scroll;
  int _nearby_scroll;
  
  int _settings_cursor;
  bool _num_input_visible;
  char _num_input_buf[16];
  const char* _num_input_title;
  bool _editing_node_name;

  bool _radio_raw_mode;
  bool _power_armed;
  uint32_t _power_armed_until;
  AdvertPath recent[UI_RECENT_LIST_SIZE];

  bool _msg_unread = false;
  bool _chat_unread = false;

  UITask* _task;
  mesh::RTCClock* _rtc;
  SensorManager* _sensors;
  NodePrefs* _node_prefs;

  // Cached layout (updated every render).
  int _screen_w = 320;
  int _screen_h = 240;
  int _rail_w = 56;
  int _tab_top = 6;
  int _tab_h = 42;
  int _tab_gap = 4;
  int _content_x = 62;
  int _content_w = 252;
  int _header_h = 34;
  int _list_y = 44;
  int _row_h = 24;
  int _list_rows = 8;

  int _scroll_btn_w = 28;
  int _scroll_up_y = 44;
  int _scroll_down_y = 200;
  int _link_btn_h = 34;
  int _link_ble_btn_y = 94;
  int _link_adv_btn_y = 140;
  int _power_btn_y = 104;
  int _power_btn_h = 48;
  int _radio_toggle_y = 48;
  int _radio_toggle_h = 20;
  int _radio_toggle_w = 100;
  int _radio_reset_y = 204;
  int _radio_reset_h = 24;

  void updateLayout(DisplayDriver& display) {
    _screen_w = display.width();
    _screen_h = display.height();

    _rail_w = (_screen_w >= 300) ? 40 : 25;
    _tab_top = (_screen_h >= 220) ? 6 : 4;
    _tab_gap = (_screen_h >= 220) ? 4 : 2;
    _tab_h = (_screen_h - _tab_top * 2 - (TAB_COUNT - 1) * _tab_gap) / TAB_COUNT;
    if (_tab_h < 16) _tab_h = 16;

    _content_x = _rail_w + 6;
    _content_w = _screen_w - _content_x - 6;
    if (_content_w < 24) _content_w = 24;

    _header_h = (_screen_h >= 220) ? 34 : (_screen_h >= 180 ? 28 : 22);
    _row_h = (_screen_h >= 220) ? 24 : (_screen_h >= 180 ? 20 : 14);
    _list_y = _header_h + 10;
    _list_rows = (_screen_h - _list_y - 8) / _row_h;
    if (_list_rows < 1) _list_rows = 1;
    if (_list_rows > 8) _list_rows = 8;

    _link_btn_h = (_screen_h >= 220) ? 34 : (_screen_h >= 180 ? 28 : 20);
    _link_ble_btn_y = _list_y + (_screen_h >= 220 ? 48 : 34);
    _link_adv_btn_y = _link_ble_btn_y + _link_btn_h + (_screen_h >= 220 ? 12 : 8);

    _power_btn_h = (_screen_h >= 220) ? 48 : (_screen_h >= 180 ? 34 : 20);
    _power_btn_y = _list_y + (_screen_h >= 220 ? 60 : 36);

    _radio_toggle_h = (_screen_h >= 220) ? 20 : 16;
    _radio_toggle_y = _list_y + 4;
    _radio_toggle_w = (_content_w - 20) / 2;
    if (_radio_toggle_w < 36) _radio_toggle_w = 36;

    _radio_reset_h = (_screen_h >= 220) ? 24 : 18;
    _radio_reset_y = _screen_h - _radio_reset_h - 8;

    _scroll_btn_w = (_screen_w >= 300) ? 28 : 22;
    _scroll_up_y = _list_y;
    _scroll_down_y = _screen_h - _row_h - 6;
  }

  int tabY(int idx) const {
    return _tab_top + idx * (_tab_h + _tab_gap);
  }

  bool isInRect(int x, int y, int rx, int ry, int rw, int rh) const {
    return (x >= rx) && (y >= ry) && (x < rx + rw) && (y < ry + rh);
  }

  const uint8_t* tabIcon(uint8_t i) const {
    switch (i) {
      case TAB_MESSAGES: return tab_icon_messages;
      case TAB_NEARBY: return tab_icon_nearby;
      case TAB_RADIO: return tab_icon_radio;
      case TAB_LINK: return tab_icon_link;
      case TAB_POWER: return tab_icon_power;
      case TAB_SETTINGS: return tab_icon_setup;
      default: return tab_icon_messages;
    }
  }

  const char* tabLabel(uint8_t i) const {
    switch (i) {
      case TAB_MESSAGES: return "MSG";
      case TAB_NEARBY: return "NBR";
      case TAB_CHAT: return "CHT";
      case TAB_RADIO: return "RF";
      case TAB_LINK: return "BLE";
      case TAB_POWER: return "PWR";
      case TAB_SETTINGS: return "SET";
      default: return "TAB";
    }
  }

  void formatAge(uint32_t timestamp, char* out, size_t out_len) {
    int secs = (int)(_rtc->getCurrentTime() - timestamp);
    if (secs < 0) secs = 0;
    if (secs < 60) {
      snprintf(out, out_len, "%ds", secs);
    } else if (secs < 3600) {
      snprintf(out, out_len, "%dm", secs / 60);
    } else {
      snprintf(out, out_len, "%dh", secs / 3600);
    }
  }

  void drawChrome(DisplayDriver& display, const char* title) {
    display.setColor(DisplayDriver::DARK);
    display.fillRect(0, 0, _screen_w, _screen_h);

    // Slim Status Bar at top
    int status_h = 24;
    display.setColor(DisplayDriver::SLATE_GREY);
    display.drawRect(0, 0, _screen_w, status_h);
    
    // Battery & Node Name
    char node[sizeof(_node_prefs->node_name)];
    display.translateUTF8ToBlocks(node, _node_prefs->node_name, sizeof(node));
    int batt_mv = _task->getBattMilliVolts();
    int batt_pct = (batt_mv - 3300) * 100 / 900;
    if (batt_pct < 0) batt_pct = 0;
    if (batt_pct > 100) batt_pct = 100;

    int batt_w = 20;
    int batt_h = 9;
    int batt_x = _screen_w - batt_w - 6;
    int batt_y = (status_h - batt_h) / 2;

    display.setColor(DisplayDriver::SLATE_GREY);
    display.drawRect(batt_x, batt_y, batt_w, batt_h);
    display.fillRect(batt_x + batt_w, batt_y + 2, 2, batt_h - 4);
    int batt_fill = ((batt_w - 2) * batt_pct) / 100;
    if (batt_fill > 0) {
      display.setColor(DisplayDriver::NEON_CYAN);
      display.fillRect(batt_x + 1, batt_y + 1, batt_fill, batt_h - 2);
    }

    display.setColor(DisplayDriver::LIGHT); // White for node name
    display.drawTextEllipsized(_rail_w + 6, 6, _content_w - batt_w - 30, node);

    char batt_txt[8];
    snprintf(batt_txt, sizeof(batt_txt), "%d%%", batt_pct);
    display.setColor(DisplayDriver::NEON_CYAN);
    display.drawTextRightAlign(batt_x - 4, 6, batt_txt);

    // Title / Header below status bar
    display.setColor(DisplayDriver::LIGHT);
    display.drawTextLeftAlign(_content_x + 4, _header_h - 10, title);
    
    // Thin horizontal separator
    display.setColor(DisplayDriver::SLATE_GREY);
    display.fillRect(_content_x, _header_h + 2, _content_w, 1);
  }

  void drawTabRail(DisplayDriver& display) {
    int tab_x = 2;
    int tab_w = _rail_w - 4;
    int r = 4; // Rounded radius
    for (int i = 0; i < TAB_COUNT; i++) {
        int y = tabY(i);
        bool active = (i == _tab);
        bool unread = false;
        if (i == TAB_MESSAGES && _msg_unread) unread = true;
        if (i == TAB_CHAT && _chat_unread) unread = true;

        if (active) {
            display.setColor(DisplayDriver::NEON_CYAN);
            display.drawRoundRect(tab_x, y, tab_w, _tab_h, r);
            display.drawRoundRect(tab_x + 1, y + 1, tab_w - 2, _tab_h - 2, r);
        } else if (unread) {
            display.setColor(DisplayDriver::DARK_GREEN);
            display.drawRoundRect(tab_x, y, tab_w, _tab_h, r);
        } else {
            display.setColor(DisplayDriver::SLATE_GREY);
            display.drawRoundRect(tab_x, y, tab_w, _tab_h, r);
        }

        display.setColor(active ? DisplayDriver::NEON_CYAN : (unread ? DisplayDriver::DARK_GREEN : DisplayDriver::GREY));
        display.drawTextCentered(tab_x + tab_w / 2, y + (_tab_h / 2) - 3, tabLabel(i));
    }
  }

  void drawButton(DisplayDriver& display, int x, int y, int w, int h, const char* label, bool active) {
    if (w < 8 || h < 8) return;

    int r = 4;
    display.setColor(active ? DisplayDriver::NEON_CYAN : DisplayDriver::SLATE_GREY);
    display.drawRoundRect(x, y, w, h, r);
    if (active) {
        display.drawRoundRect(x + 1, y + 1, w - 2, h - 2, r);
    }

    display.setColor(active ? DisplayDriver::NEON_CYAN : DisplayDriver::LIGHT);
    display.drawTextCentered(x + w / 2, y + (h / 2) - 3, label);
  }

  void renderMessagesList(DisplayDriver& display) {
    int x = _content_x;
    int w = _content_w;
    int total = _task->getStoredMessageCount();
    int panel_h = _screen_h - _list_y - 6;

    display.setColor(DisplayDriver::DARK);
    display.fillRect(x, _list_y, w, panel_h);

    if (total == 0) {
      display.setColor(DisplayDriver::LIGHT);
      display.drawTextCentered(x + w / 2, _list_y + 16, "No messages yet");
      display.setColor(DisplayDriver::LIGHT);
      display.drawTextCentered(x + w / 2, _list_y + 30, "Touch tabs to explore");
      return;
    }

    if (_msg_cursor >= total) _msg_cursor = total - 1;
    if (_msg_cursor < 0) _msg_cursor = 0;

    if (_msg_scroll > _msg_cursor) _msg_scroll = _msg_cursor;
    if (_msg_cursor >= _msg_scroll + _list_rows) _msg_scroll = _msg_cursor - _list_rows + 1;
    if (_msg_scroll < 0) _msg_scroll = 0;

    int list_w = w - _scroll_btn_w - 4;
    for (int i = 0; i < _list_rows; i++) {
      int idx = _msg_scroll + i;
      if (idx >= total) break;

      UITask::MessageEntry e;
      if (!_task->getStoredMessage(idx, e)) break;

      int y = _list_y + i * _row_h;
      bool selected = (idx == _msg_cursor);
      display.setColor(selected ? DisplayDriver::NEON_CYAN : DisplayDriver::DARK);
      if (selected) {
          display.drawRect(x + 1, y, list_w - 2, _row_h - 1);
          display.drawRect(x + 2, y + 1, list_w - 4, _row_h - 3);
      } else {
          display.setColor(DisplayDriver::SLATE_GREY);
          display.fillRect(x + 1, y + _row_h - 1, list_w - 2, 1); // Thin separator line at bottom
      }

      char age[8];
      formatAge(e.timestamp, age, sizeof(age));
      int age_w = display.getTextWidth(age);
      int name_w = list_w - age_w - 12;
      if (name_w < 10) name_w = 10;

      char filtered_origin[sizeof(e.origin)];
      char filtered_msg[sizeof(e.text)];
      display.translateUTF8ToBlocks(filtered_origin, e.origin, sizeof(filtered_origin));
      display.translateUTF8ToBlocks(filtered_msg, e.text, sizeof(filtered_msg));

      display.setColor(DisplayDriver::LIGHT);
      display.drawTextEllipsized(x + 6, y + 2, name_w, filtered_origin);
      display.drawTextRightAlign(x + list_w - 4, y + 2, age);

      if (_row_h >= 18) {
        display.drawTextEllipsized(x + 6, y + (_row_h / 2) + 1, list_w - 12, filtered_msg);
      }
    }

    // scroll buttons
    int btn_x = x + w - _scroll_btn_w;
    drawButton(display, btn_x, _scroll_up_y, _scroll_btn_w, _row_h, "^", false);
    drawButton(display, btn_x, _scroll_down_y, _scroll_btn_w, _row_h, "v", false);
  }

  void renderMessageDetail(DisplayDriver& display) {
    int x = _content_x;
    int w = _content_w;
    int panel_h = _screen_h - _list_y - 6;
    UITask::MessageEntry e;
    if (!_task->getStoredMessage(_msg_cursor, e)) {
      _show_msg_detail = false;
      return;
    }

    char age[8];
    formatAge(e.timestamp, age, sizeof(age));

    display.setColor(DisplayDriver::DARK);
    display.fillRect(x, _list_y, w, panel_h);

    // back button
    display.setColor(DisplayDriver::DARK);
    display.fillRect(x + 4, _list_y + 4, 70, 16);
    display.setColor(DisplayDriver::LIGHT);
    display.drawRect(x + 4, _list_y + 4, 70, 16);
    display.drawTextCentered(x + 39, _list_y + 9, "< Back");
    display.drawTextRightAlign(x + w - 4, _list_y + 9, age);

    char filtered_origin[sizeof(e.origin)];
    char filtered_msg[sizeof(e.text)];
    display.translateUTF8ToBlocks(filtered_origin, e.origin, sizeof(filtered_origin));
    display.translateUTF8ToBlocks(filtered_msg, e.text, sizeof(filtered_msg));

    display.setColor(DisplayDriver::LIGHT);
    display.drawTextEllipsized(x + 4, _list_y + 26, w - 8, filtered_origin);

    int box_y = _list_y + 42;
    int box_h = _screen_h - box_y - 6;
    display.setColor(DisplayDriver::DARK);
    display.fillRect(x + 2, box_y + 1, w - 4, box_h - 2);

    display.setColor(DisplayDriver::LIGHT);
    display.setCursor(x + 5, box_y + 4);
    display.printWordWrap(filtered_msg, w - 10);
  }

  void renderNearby(DisplayDriver& display) {
    int x = _content_x;
    int w = _content_w;
    int panel_h = _screen_h - _list_y - 6;

    display.setColor(DisplayDriver::DARK);
    display.fillRect(x, _list_y, w, panel_h);

    the_mesh.getRecentlyHeard(recent, UI_RECENT_LIST_SIZE);
    int total = 0;
    for (int i = 0; i < UI_RECENT_LIST_SIZE; i++) {
      if (recent[i].name[0] != 0) total++;
    }

    if (total == 0) {
      display.setColor(DisplayDriver::LIGHT);
      display.drawTextCentered(x + w / 2, _list_y + 16, "No nearby nodes");
      return;
    }

    if (_nearby_scroll >= total) _nearby_scroll = total - 1;
    if (_nearby_scroll < 0) _nearby_scroll = 0;

    int list_w = w - _scroll_btn_w - 4;
    int shown = 0;
    int skipped = 0;
    for (int i = 0; i < UI_RECENT_LIST_SIZE && shown < _list_rows; i++) {
      AdvertPath* a = &recent[i];
      if (a->name[0] == 0) continue;
      
      if (skipped < _nearby_scroll) {
        skipped++;
        continue;
      }

      int y = _list_y + shown * _row_h;
      shown++;

      char age[8];
      formatAge(a->recv_timestamp, age, sizeof(age));
      int age_w = display.getTextWidth(age);
      int max_name_w = list_w - age_w - 12;
      if (max_name_w < 8) max_name_w = 8;

      char filtered_name[sizeof(a->name)];
      display.translateUTF8ToBlocks(filtered_name, a->name, sizeof(filtered_name));

      display.setColor(DisplayDriver::LIGHT);
      display.drawTextEllipsized(x + 6, y + 4, max_name_w, filtered_name);
      display.setColor(DisplayDriver::NEON_CYAN); // Neon for age
      display.drawTextRightAlign(x + list_w - 4, y + 4, age);
    }

    // scroll buttons
    int btn_x = x + w - _scroll_btn_w;
    drawButton(display, btn_x, _scroll_up_y, _scroll_btn_w, _row_h, "^", false);
    drawButton(display, btn_x, _scroll_down_y, _scroll_btn_w, _row_h, "v", false);
  }

  void renderChat(DisplayDriver& display) {
    int x = _content_x;
    int w = _content_w;
    int panel_h = _screen_h - _list_y;
    display.setColor(DisplayDriver::DARK);
    display.fillRect(x, _list_y, w, panel_h);

    // Top: Channel Selector
    char ch_name[32] = "Select Channel / Contact";
    if (_active_chat_idx != 0xFF) {
        if (_active_chat_is_group) {
            ChannelDetails ch;
            if (the_mesh.getChannel(_active_chat_idx, ch)) strcpy(ch_name, ch.name);
        } else {
            ContactInfo ci;
            if (the_mesh.getContactByIdx(_active_chat_idx, ci)) strcpy(ch_name, ci.name);
        }
    }
    drawButton(display, x + 4, _list_y + 4, w - 8, 22, ch_name, _chat_dropdown_open);

    // Bottom: Input Field
    int input_y = _screen_h - 26;
    display.setColor(DisplayDriver::DARK);
    display.fillRect(x + 2, input_y, w - 4, 24);
    display.setColor(DisplayDriver::LIGHT);
    display.drawRect(x + 1, input_y - 1, w - 2, 26);
    if (_chat_draft[0] == 0) {
        display.setColor(DisplayDriver::GREY);
        display.drawTextLeftAlign(x + 6, input_y + 4, "Write message...");
    } else {
        display.setColor(DisplayDriver::LIGHT);
        display.drawTextLeftAlign(x + 6, input_y + 4, _chat_draft);
    }

    // Middle: History (Filtered)
    int hist_y = _list_y + 30;
    int hist_h = input_y - hist_y - 4;
    int rows = hist_h / 14;
    int shown = 0;
    for (int i = 0; i < _task->getStoredMessageCount() && shown < rows; i++) {
        UITask::MessageEntry e;
        if (!_task->getStoredMessage(i + _chat_scroll, e)) break;
        
        bool match = false;
        if (_active_chat_idx != 0xFF) {
            if (_active_chat_is_group) {
                match = e.is_group && (e.channel_idx == _active_chat_idx);
            } else {
                // For direct, we'd need pubkey matching, but for now we'll match by channel_idx=0xFF and origin name prefix
                // This is a simplification for the first version
                match = !e.is_group && (e.channel_idx == 0xFF);
            }
        }

        if (match) {
            int ry = hist_y + (rows - 1 - shown) * 14;
            if (e.is_sent) {
                display.setColor(e.status == 1 ? DisplayDriver::GREEN : DisplayDriver::RED);
                char tag[16];
                snprintf(tag, sizeof(tag), " Me:%d", e.repeat_count);
                display.drawTextEllipsized(x + 4, ry, 60, tag);
            } else {
                display.setColor(DisplayDriver::BLUE);
                display.drawTextEllipsized(x + 4, ry, 60, e.origin);
            }
            display.setColor(DisplayDriver::LIGHT);
            display.drawTextEllipsized(x + 66, ry, w - 70, e.text);
            shown++;
        }
    }

    if (_keyboard_visible) renderKeyboard(display);
    if (_chat_dropdown_open) renderChatDropdown(display);
  }

  void renderKeyboard(DisplayDriver& display) {
    int kb_h = 160;
    int kb_y = _screen_h - kb_h;
    display.setColor(DisplayDriver::DARK);
    display.fillRect(0, kb_y, _screen_w, kb_h);
    display.setColor(DisplayDriver::SLATE_GREY);
    display.drawRect(0, kb_y, _screen_w, kb_h);

    // Text Preview at top of keyboard
    display.setColor(DisplayDriver::DARK);
    display.fillRect(2, kb_y + 2, _screen_w - 4, 30);
    display.setColor(DisplayDriver::SLATE_GREY);
    display.drawRect(1, kb_y + 1, _screen_w - 2, 32);
    if (_chat_draft[0] == 0) {
        display.setColor(DisplayDriver::GREY);
        display.drawTextLeftAlign(6, kb_y + 8, "Type message...");
    } else {
        display.setColor(DisplayDriver::LIGHT);
        display.drawTextLeftAlign(6, kb_y + 8, _chat_draft);
    }

    const char* rows[3];
    if (_kb_shift == 0) {
        rows[0] = "qwertyuiop";
        rows[1] = "asdfghjkl";
        rows[2] = "zxcvbnm";
    } else if (_kb_shift == 1) {
        rows[0] = "QWERTYUIOP";
        rows[1] = "ASDFGHJKL";
        rows[2] = "ZXCVBNM";
    } else {
        rows[0] = "1234567890";
        rows[1] = "-/()$&@\"";
        rows[2] = ".,?!'#";
    }

    int ky = kb_y + 36;
    int kw = _screen_w / 10;
    int kh = 26;

    for (int r = 0; r < 3; r++) {
        int len = strlen(rows[r]);
        int ox = (_screen_w - (len * kw)) / 2;
        for (int c = 0; c < len; c++) {
            char key[2] = { rows[r][c], 0 };
            drawKey(display, ox + c * kw, ky + r * (kh + 4), kw - 2, kh, key);
        }
    }

    // Special keys
    int bottom_y = ky + 3 * (kh + 4);
    drawKey(display, 4, bottom_y, 45, kh, _kb_shift == 0 ? "ABC" : "abc");
    drawKey(display, 55, bottom_y, 130, kh, "SPACE");
    drawKey(display, 190, bottom_y, 55, kh, "BKSP");
    drawKey(display, 250, bottom_y, 65, kh, "SEND");
  }

  void drawKey(DisplayDriver& display, int x, int y, int w, int h, const char* label) {
    display.setColor(DisplayDriver::DARK);
    display.fillRoundRect(x, y, w, h, 2);
    display.setColor(DisplayDriver::SLATE_GREY);
    display.drawRoundRect(x, y, w, h, 2); // Cyber-tech: Always use slate grey for inactive keys
    display.setColor(DisplayDriver::LIGHT);
    display.drawTextCentered(x + w / 2, y + (h - 12) / 2, label);
  }

  void renderChatDropdown(DisplayDriver& display) {
    int dx = _content_x + 10;
    int dy = _list_y + 26;
    int dw = _content_w - 20;
    display.setColor(DisplayDriver::DARK);
    display.fillRect(dx, dy, dw, 120);
    display.setColor(DisplayDriver::LIGHT);
    display.drawRect(dx, dy, dw, 120);

    // Static channels for now: Public + any custom
    for (int i = 0; i < 4; i++) {
        ChannelDetails ch;
        if (the_mesh.getChannel(i, ch)) {
          display.setColor(i == _active_chat_idx ? DisplayDriver::NEON_CYAN : DisplayDriver::SLATE_GREY);
          display.drawRoundRect(dx, dy + i * 20, dw, 20, 2);
          display.setColor(i == _active_chat_idx ? DisplayDriver::NEON_CYAN : DisplayDriver::LIGHT);
          display.drawTextLeftAlign(dx + 6, dy + 4 + i * 20, ch.name);
        }
    }
  }

  void renderRadio(DisplayDriver& display) {
    int x = _content_x;
    int w = _content_w;
    int panel_h = _screen_h - _list_y - 6;

    display.setColor(DisplayDriver::DARK);
    display.fillRect(x, _list_y, w, panel_h);

    int left_btn_x = x + 6;
    int right_btn_x = left_btn_x + _radio_toggle_w + 8;
    drawButton(display, left_btn_x, _radio_toggle_y, _radio_toggle_w, _radio_toggle_h, "Config", !_radio_raw_mode);
    drawButton(display, right_btn_x, _radio_toggle_y, _radio_toggle_w, _radio_toggle_h, "Raw", _radio_raw_mode);

    int y = _radio_toggle_y + _radio_toggle_h + 8;
    char tmp[64];

    if (!_radio_raw_mode) {
      display.setColor(DisplayDriver::LIGHT);
      snprintf(tmp, sizeof(tmp), "Freq: %.3f MHz", _node_prefs->freq);
      display.drawTextLeftAlign(x + 6, y, tmp);
      y += 13;
      snprintf(tmp, sizeof(tmp), "SF: %d   CR: %d", _node_prefs->sf, _node_prefs->cr);
      display.drawTextLeftAlign(x + 6, y, tmp);
      y += 13;
      snprintf(tmp, sizeof(tmp), "BW: %.2f kHz", _node_prefs->bw);
      display.drawTextLeftAlign(x + 6, y, tmp);
      y += 13;
      snprintf(tmp, sizeof(tmp), "TX Power: %ddBm", _node_prefs->tx_power_dbm);
      display.drawTextLeftAlign(x + 6, y, tmp);
      y += 13;
      display.setColor(DisplayDriver::NEON_CYAN);
      snprintf(tmp, sizeof(tmp), "Noise Floor: %d", radio_driver.getNoiseFloor());
      display.drawTextLeftAlign(x + 6, y, tmp);
      y += 16;
      display.drawTextLeftAlign(x + 6, y, "Tap Raw for TX/RX diagnostics");
      return;
    }

    display.setColor(DisplayDriver::LIGHT);
    snprintf(tmp, sizeof(tmp), "RSSI: %.1f dBm", radio_driver.getLastRSSI());
    display.drawTextLeftAlign(x + 6, y, tmp);
    y += 12;
    snprintf(tmp, sizeof(tmp), "SNR:  %.2f dB", radio_driver.getLastSNR());
    display.drawTextLeftAlign(x + 6, y, tmp);
    y += 12;
    snprintf(tmp, sizeof(tmp), "Noise Floor: %d", radio_driver.getNoiseFloor());
    display.drawTextLeftAlign(x + 6, y, tmp);
    y += 12;
    snprintf(tmp, sizeof(tmp), "Pkts RX/TX: %lu / %lu",
             (unsigned long)radio_driver.getPacketsRecv(),
             (unsigned long)radio_driver.getPacketsSent());
    display.drawTextLeftAlign(x + 6, y, tmp);
    y += 12;
    snprintf(tmp, sizeof(tmp), "Flood TX/RX: %lu / %lu",
             (unsigned long)the_mesh.getNumSentFlood(),
             (unsigned long)the_mesh.getNumRecvFlood());
    display.drawTextLeftAlign(x + 6, y, tmp);
    y += 12;
    snprintf(tmp, sizeof(tmp), "Direct TX/RX: %lu / %lu",
             (unsigned long)the_mesh.getNumSentDirect(),
             (unsigned long)the_mesh.getNumRecvDirect());
    display.drawTextLeftAlign(x + 6, y, tmp);
    y += 12;
    snprintf(tmp, sizeof(tmp), "Air TX/RX sec: %lu / %lu",
             (unsigned long)(the_mesh.getTotalAirTime() / 1000),
             (unsigned long)(the_mesh.getReceiveAirTime() / 1000));
    display.drawTextLeftAlign(x + 6, y, tmp);

    drawButton(display, x + 8, _radio_reset_y, w - 16, _radio_reset_h, "Reset Radio Stats", false);
  }

  void renderLink(DisplayDriver& display) {
    int x = _content_x;
    int w = _content_w;
    int panel_h = _screen_h - _list_y - 6;

    display.setColor(DisplayDriver::DARK);
    display.fillRect(x, _list_y, w, panel_h);

    display.setColor(DisplayDriver::SLATE_GREY);
    display.drawRect(x + 4, _list_y + 4, w - 8, panel_h - 8);

    display.setColor(DisplayDriver::NEON_CYAN);
    display.drawTextCentered(x + w / 2, _list_y + 16, "CONNECTION STATUS");
    
    display.setColor(DisplayDriver::SLATE_GREY);
    display.fillRect(x + 10, _list_y + 32, w - 20, 1);
    if (_task->hasConnection()) {
      display.setColor(DisplayDriver::NEON_CYAN);
      display.drawRect(x + 6, _list_y + 6, w - 12, 18);
      display.drawRect(x + 7, _list_y + 7, w - 14, 16);
      display.setColor(DisplayDriver::NEON_CYAN);
    } else {
      display.setColor(DisplayDriver::LIGHT); // Dimmer white for disconnected
    }
    display.drawTextCentered(x + w / 2, _list_y + 11, _task->hasConnection() ? "Connected" : "Disconnected");

    char tmp[32];
    if (the_mesh.getBLEPin() != 0) {
      snprintf(tmp, sizeof(tmp), "PIN %d", the_mesh.getBLEPin());
      display.setColor(DisplayDriver::LIGHT);
      display.drawTextRightAlign(x + w - 8, _list_y + 30, tmp);
    }

    drawButton(display, x + 8, _link_ble_btn_y, w - 16, _link_btn_h,
               _task->isSerialEnabled() ? "Disable BLE" : "Enable BLE",
               _task->isSerialEnabled());

    drawButton(display, x + 8, _link_adv_btn_y, w - 16, _link_btn_h, "Send Advert", false);
  }

  void renderPower(DisplayDriver& display) {
    int x = _content_x;
    int w = _content_w;
    int panel_h = _screen_h - _list_y - 6;

    display.setColor(DisplayDriver::DARK);
    display.fillRect(x, _list_y, w, panel_h);

    char tmp[32];
    snprintf(tmp, sizeof(tmp), "Battery %umV", _task->getBattMilliVolts());
    display.setColor(DisplayDriver::LIGHT);
    display.drawTextLeftAlign(x + 8, _list_y + 8, tmp);

    drawButton(display, x + 8, _power_btn_y, w - 16, _power_btn_h, "", _power_armed);

    if (_power_armed && (int32_t)(_power_armed_until - millis()) > 0) {
      display.setColor(DisplayDriver::NEON_CYAN);
      display.drawTextCentered(x + w / 2, _power_btn_y + (_power_btn_h / 2) - 9, "Tap Again To");
      display.drawTextCentered(x + w / 2, _power_btn_y + (_power_btn_h / 2) + 3, "Hibernate");
    } else {
      display.setColor(DisplayDriver::LIGHT);
      display.drawTextCentered(x + w / 2, _power_btn_y + (_power_btn_h / 2) - 3, "Hibernate");
    }
  }

  void applyUKNarrowPreset() {
    _node_prefs->freq = 869.525f;
    _node_prefs->bw = 62.5f;
    _node_prefs->sf = 9;
    _node_prefs->cr = 5; // 4/5
    _node_prefs->tx_power_dbm = 14;
    the_mesh.savePrefs();
    _task->showAlert("UK Narrow Applied", 2000);
  }

  void renderSettings(DisplayDriver& display) {
    int x = _content_x;
    int w = _content_w;
    int panel_h = _screen_h - _list_y - 6;

    if (_num_input_visible) {
        renderNumKeypad(display);
        return;
    }

    display.setColor(DisplayDriver::DARK);
    display.fillRect(x, _list_y, w, panel_h);

    const int SETTINGS_COUNT = 10;
    const char* labels[SETTINGS_COUNT] = {
        "UK Narrow Preset",
        "Node Name",
        "Frequency",
        "Spreading Factor",
        "Bandwidth",
        "Coding Rate",
        "TX Power",
        "BLE PIN",
        "GPS Mode",
        "Buzzer"
    };

    int list_w = w - _scroll_btn_w - 4;
    for (int i = 0; i < _list_rows; i++) {
        int idx = i; // simple scroll for now
        if (idx >= SETTINGS_COUNT) break;

        int y = _list_y + i * _row_h;
        bool selected = (idx == _settings_cursor);

        display.setColor(selected ? DisplayDriver::NEON_CYAN : DisplayDriver::SLATE_GREY);
        display.drawRoundRect(x + 1, y, list_w - 2, _row_h - 1, 4);

        display.setColor(DisplayDriver::LIGHT);
        display.drawTextLeftAlign(x + 6, y + 4, labels[idx]);

        char val[32] = "";
        display.setColor(DisplayDriver::NEON_CYAN);
        switch(idx) {
            case 1: snprintf(val, sizeof(val), "%s", _node_prefs->node_name); break;
            case 2: snprintf(val, sizeof(val), "%.3f", _node_prefs->freq); break;
            case 3: snprintf(val, sizeof(val), "SF%d", _node_prefs->sf); break;
            case 4: snprintf(val, sizeof(val), "%.1f", _node_prefs->bw); break;
            case 5: snprintf(val, sizeof(val), "4/%d", _node_prefs->cr); break;
            case 6: snprintf(val, sizeof(val), "%ddBm", _node_prefs->tx_power_dbm); break;
            case 7: snprintf(val, sizeof(val), "%06lu", _node_prefs->ble_pin); break;
            case 8: snprintf(val, sizeof(val), "%s", _node_prefs->gps_enabled ? "ON" : "OFF"); break;
            case 9: snprintf(val, sizeof(val), "%s", _node_prefs->buzzer_quiet ? "QUIET" : "BEEP"); break;
        }
        display.drawTextRightAlign(x + list_w - 6, y + 4, val);
    }
  }

  void renderNumKeypad(DisplayDriver& display) {
    int x = _content_x;
    int w = _content_w;
    int panel_h = _screen_h - _list_y - 6;

    display.setColor(DisplayDriver::DARK);
    display.fillRect(x, _list_y, w, panel_h);

    display.setColor(DisplayDriver::NEON_CYAN);
    display.drawTextCentered(x + w / 2, _list_y + 4, _num_input_title);
    
    display.setColor(DisplayDriver::SLATE_GREY);
    display.drawRoundRect(x + 10, _list_y + 20, w - 20, 24, 4);
    display.setColor(DisplayDriver::LIGHT);
    display.drawTextCentered(x + w / 2, _list_y + 24, _num_input_buf);

    const char* nkeys[12] = {"1","2","3","4","5","6","7","8","9",".","0","X"};
    int kw = (w - 20) / 3;
    int kh = (panel_h - 60) / 4;
    for (int i = 0; i < 12; i++) {
        int kx = x + 10 + (i % 3) * kw;
        int ky = _list_y + 48 + (i / 3) * kh;
        drawButton(display, kx + 2, ky + 2, kw - 4, kh - 4, nkeys[i], false);
    }
    drawButton(display, x + w - 45, _list_y + panel_h - 22, 40, 20, "OK", true);
  }

  void activateCurrentTab() {
    if (_tab == TAB_LINK) {
      if (_task->isSerialEnabled()) _task->disableSerial();
      else _task->enableSerial();
      return;
    }
    if (_tab == TAB_POWER) {
      if (_power_armed && (int32_t)(_power_armed_until - millis()) > 0) {
        _task->shutdown();
      } else {
        _power_armed = true;
        _power_armed_until = millis() + 2000;
      }
      return;
    }
    if (_tab == TAB_MESSAGES) {
      int total = _task->getStoredMessageCount();
      if (total == 0) return;
      _show_msg_detail = !_show_msg_detail;
      if (_msg_cursor < 0) _msg_cursor = 0;
      if (_msg_cursor >= total) _msg_cursor = total - 1;
    }
  }

public:
  HomeScreen(UITask* task, mesh::RTCClock* rtc, SensorManager* sensors, NodePrefs* node_prefs)
     : _task(task), _rtc(rtc), _sensors(sensors), _node_prefs(node_prefs),
       _tab(TAB_MESSAGES), _show_msg_detail(false), _msg_cursor(0), _msg_scroll(0), _nearby_scroll(0),
       _active_chat_idx(0), _active_chat_is_group(true), _keyboard_visible(false), _kb_shift(0), _chat_scroll(0), _chat_dropdown_open(false),
       _radio_raw_mode(false),
       _power_armed(false), _power_armed_until(0),
       _msg_unread(false), _chat_unread(false) {
     _chat_draft[0] = 0;
     _num_input_buf[0] = 0;
     _num_input_title = "";
   }

  void setUnread(Tab tab) {
      if (tab == TAB_MESSAGES) _msg_unread = true;
      if (tab == TAB_CHAT) _chat_unread = true;
  }

  int render(DisplayDriver& display) override {
    updateLayout(display);
    display.setTextSize(1);

    if (_tab == TAB_MESSAGES) _msg_unread = false;
    if (_tab == TAB_CHAT) _chat_unread = false;

    if (_power_armed && (int32_t)(_power_armed_until - millis()) <= 0) {
      _power_armed = false;
    }

    const char* title = "Messages";
    if (_tab == TAB_NEARBY) title = "Nearby";
    else if (_tab == TAB_CHAT) title = "Chat";
    else if (_tab == TAB_RADIO) title = _radio_raw_mode ? "Radio Raw" : "Radio";
    else if (_tab == TAB_LINK) title = "Link";
    else if (_tab == TAB_POWER) title = "Power";
    else if (_tab == TAB_SETTINGS) title = "Settings";

    drawChrome(display, title);
    drawTabRail(display);

    if (_tab == TAB_MESSAGES) {
      if (_show_msg_detail) renderMessageDetail(display);
      else renderMessagesList(display);
    } else if (_tab == TAB_NEARBY) {
      renderNearby(display);
    } else if (_tab == TAB_CHAT) {
      renderChat(display);
    } else if (_tab == TAB_RADIO) {
      renderRadio(display);
    } else if (_tab == TAB_LINK) {
      renderLink(display);
    } else if (_tab == TAB_POWER) {
      renderPower(display);
    } else if (_tab == TAB_SETTINGS) {
      renderSettings(display);
    }

    return 250;
  }

  bool handleInput(char c) override {
    if (c == KEY_LEFT || c == KEY_PREV) {
      _tab = (Tab)((_tab + TAB_COUNT - 1) % TAB_COUNT);
      _show_msg_detail = false;
      return true;
    }
    if (c == KEY_NEXT || c == KEY_RIGHT) {
      _tab = (Tab)((_tab + 1) % TAB_COUNT);
      _show_msg_detail = false;
      return true;
    }

    if (_tab == TAB_MESSAGES) {
      int total = _task->getStoredMessageCount();
      if (c == KEY_DOWN && !_show_msg_detail && total > 0) {
        _msg_cursor++;
        if (_msg_cursor >= total) _msg_cursor = total - 1;
        return true;
      }
      if (c == KEY_UP && !_show_msg_detail && total > 0) {
        _msg_cursor--;
        if (_msg_cursor < 0) _msg_cursor = 0;
        return true;
      }
      if (c == KEY_ENTER) {
        activateCurrentTab();
        return true;
      }
    }

    if (_tab == TAB_RADIO && c == KEY_ENTER) {
      _radio_raw_mode = !_radio_raw_mode;
      return true;
    }

    if (_tab == TAB_LINK && c == KEY_ENTER) {
      activateCurrentTab();
      return true;
    }

    if (_tab == TAB_POWER && c == KEY_ENTER) {
      activateCurrentTab();
      return true;
    }

    if (_tab == TAB_SETTINGS) {
        if (_num_input_visible) {
            if (c == KEY_ENTER) {
                // OK (simplified keyboard mapping)
                float fval = atof(_num_input_buf);
                int ival = atoi(_num_input_buf);
                switch(_settings_cursor) {
                    case 2: _node_prefs->freq = fval; break;
                    case 3: _node_prefs->sf = (uint8_t)ival; break;
                    case 4: _node_prefs->bw = fval; break;
                    case 5: _node_prefs->cr = (uint8_t)ival; break;
                    case 6: _node_prefs->tx_power_dbm = (int8_t)ival; break;
                    case 7: _node_prefs->ble_pin = (uint32_t)atoll(_num_input_buf); break;
                }
                the_mesh.savePrefs();
                _num_input_visible = false;
                return true;
            }
            if (c == KEY_CANCEL || c == KEY_SELECT) {
                _num_input_visible = false;
                return true;
            }
        } else {
            if (c == KEY_DOWN) {
                _settings_cursor = (_settings_cursor + 1) % 10;
                return true;
            }
            if (c == KEY_UP) {
                _settings_cursor = (_settings_cursor + 9) % 10;
                return true;
            }
            if (c == KEY_ENTER) {
                int idx = _settings_cursor;
                if (idx == 0) applyUKNarrowPreset();
                else if (idx == 1) {
                    _editing_node_name = true;
                    _keyboard_visible = true;
                    strncpy(_chat_draft, _node_prefs->node_name, sizeof(_chat_draft));
                } else if (idx >= 2 && idx <= 7) {
                    _num_input_visible = true;
                    _num_input_buf[0] = 0;
                    const char* l[] = {"","","Freq","SF","BW","CR","TX Power","BLE PIN"};
                    _num_input_title = l[idx];
                } else if (idx == 8) {
                    _node_prefs->gps_enabled = !_node_prefs->gps_enabled;
                    the_mesh.savePrefs();
                } else if (idx == 9) {
                    _node_prefs->buzzer_quiet = !_node_prefs->buzzer_quiet;
                    the_mesh.savePrefs();
                }
                return true;
            }
        }
    }

    return false;
  }

  bool handleTouch(int x, int y) override {
    if (_keyboard_visible) {
        int kb_h = 160;
        int kb_y = _screen_h - kb_h;
        if (y >= kb_y) {
            int ky = kb_y + 36;
            int kh = 26;
            int kw = _screen_w / 10;
            
            // Check special keys first (bottom row)
            int bottom_y = ky + 3 * (kh + 4);
            if (isInRect(x, y, 4, bottom_y, 45, kh)) {
                _kb_shift = (_kb_shift + 1) % 3;
                return true;
            }
            if (isInRect(x, y, 55, bottom_y, 130, kh)) {
                int len = strlen(_chat_draft);
                if (len < sizeof(_chat_draft) - 1) {
                    _chat_draft[len] = ' ';
                    _chat_draft[len+1] = 0;
                }
                return true;
            }
            if (isInRect(x, y, 190, bottom_y, 55, kh)) {
                int len = strlen(_chat_draft);
                if (len > 0) _chat_draft[len-1] = 0;
                return true;
            }
            if (isInRect(x, y, 250, bottom_y, 65, kh)) {
                // SEND / OK
                if (_editing_node_name) {
                    strncpy(_node_prefs->node_name, _chat_draft, sizeof(_node_prefs->node_name));
                    the_mesh.savePrefs();
                    _editing_node_name = false;
                    _keyboard_visible = false;
                    return true;
                }
                if (_chat_draft[0] != 0 && _active_chat_idx != 0xFF) {
                    uint32_t expected_ack = 0;
                    if (_active_chat_is_group) {
                        ChannelDetails ch;
                        if (the_mesh.getChannel(_active_chat_idx, ch)) {
                            the_mesh.sendGroupMessage(_rtc->getCurrentTime(), ch.channel, _node_prefs->node_name, _chat_draft, strlen(_chat_draft));
                            // Use a simple sum-based checksum for repeat detection
                            expected_ack = 0;
                            for (int i = 0; _chat_draft[i]; i++) expected_ack += _chat_draft[i];
                        }
                    } else {
                        ContactInfo ci;
                        if (the_mesh.getContactByIdx(_active_chat_idx, ci)) {
                            uint32_t est_timeout;
                            the_mesh.sendMessage(ci, _rtc->getCurrentTime(), 0, _chat_draft, expected_ack, est_timeout);
                        }
                    }
                    // Store in local history too
                    _task->storeMessage(0, "Me", _chat_draft, _active_chat_idx, _active_chat_is_group, true, expected_ack);
                    _chat_draft[0] = 0;
                    _keyboard_visible = false;
                }
                return true;
            }

            // Regular rows
            const char* krows[3];
            if (_kb_shift == 0) { krows[0] = "qwertyuiop"; krows[1] = "asdfghjkl"; krows[2] = "zxcvbnm"; }
            else if (_kb_shift == 1) { krows[0] = "QWERTYUIOP"; krows[1] = "ASDFGHJKL"; krows[2] = "ZXCVBNM"; }
            else { krows[0] = "1234567890"; krows[1] = "-/()$&@\""; krows[2] = ".,?!'#"; }

            for (int r = 0; r < 3; r++) {
                int ry = ky + r * (kh + 4);
                if (y >= ry && y < ry + kh) {
                    int len = strlen(krows[r]);
                    int ox = (_screen_w - (len * kw)) / 2;
                    if (x >= ox && x < ox + len * kw) {
                        int c_idx = (x - ox) / kw;
                        int d_len = strlen(_chat_draft);
                        if (d_len < sizeof(_chat_draft) - 1) {
                            _chat_draft[d_len] = krows[r][c_idx];
                            _chat_draft[d_len+1] = 0;
                        }
                        return true;
                    }
                }
            }
            return true; // Absorb all touches in KB area
        }
    }

    if (x < _rail_w) {
      for (int tab = 0; tab < TAB_COUNT; tab++) {
        if (isInRect(x, y, 0, tabY(tab), _rail_w, _tab_h)) {
          _tab = (Tab)tab;
          _show_msg_detail = false;
          _keyboard_visible = false;
          _num_input_visible = false;
          _editing_node_name = false;
          return true;
        }
      }
    }

    if (_tab == TAB_SETTINGS) {
        if (_num_input_visible) {
            int kw = (_content_w - 20) / 3;
            int kh = (_screen_h - _list_y - 6 - 60) / 4;
            const char* nkeys[12] = {"1","2","3","4","5","6","7","8","9",".","0","X"};
            for (int i = 0; i < 12; i++) {
                int kx = _content_x + 10 + (i % 3) * kw;
                int ky = _list_y + 48 + (i / 3) * kh;
                if (isInRect(x, y, kx, ky, kw, kh)) {
                    if (i == 11) { // X (backspace)
                        int len = strlen(_num_input_buf);
                        if (len > 0) _num_input_buf[len-1] = 0;
                    } else if (strlen(_num_input_buf) < sizeof(_num_input_buf) - 1) {
                        strcat(_num_input_buf, nkeys[i]);
                    }
                    return true;
                }
            }
            if (isInRect(x, y, _content_x + _content_w - 45, _list_y + (_screen_h - _list_y - 6) - 22, 40, 20)) {
                // OK
                float fval = atof(_num_input_buf);
                int ival = atoi(_num_input_buf);
                switch(_settings_cursor) {
                    case 2: _node_prefs->freq = fval; break;
                    case 3: _node_prefs->sf = (uint8_t)ival; break;
                    case 4: _node_prefs->bw = fval; break;
                    case 5: _node_prefs->cr = (uint8_t)ival; break;
                    case 6: _node_prefs->tx_power_dbm = (int8_t)ival; break;
                    case 7: _node_prefs->ble_pin = (uint32_t)atoll(_num_input_buf); break;
                }
                the_mesh.savePrefs();
                _num_input_visible = false;
                return true;
            }
            return true;
        }

        int list_w = _content_w - _scroll_btn_w - 4;
        for (int i = 0; i < _list_rows; i++) {
            int idx = i;
            int iy = _list_y + i * _row_h;
            if (isInRect(x, y, _content_x + 1, iy, list_w - 2, _row_h - 1)) {
                _settings_cursor = idx;
                if (idx == 0) applyUKNarrowPreset();
                else if (idx == 1) {
                    _editing_node_name = true;
                    _keyboard_visible = true;
                    strncpy(_chat_draft, _node_prefs->node_name, sizeof(_chat_draft));
                } else if (idx >= 2 && idx <= 7) {
                    _num_input_visible = true;
                    _num_input_buf[0] = 0;
                    const char* l[] = {"","","Freq","SF","BW","CR","TX Power","BLE PIN"};
                    _num_input_title = l[idx];
                } else if (idx == 8) {
                    _node_prefs->gps_enabled = !_node_prefs->gps_enabled;
                    the_mesh.savePrefs();
                } else if (idx == 9) {
                    _node_prefs->buzzer_quiet = !_node_prefs->buzzer_quiet;
                    the_mesh.savePrefs();
                }
                return true;
            }
        }
        return true;
    }

    if (_tab == TAB_MESSAGES) {
      if (_show_msg_detail) {
        if (isInRect(x, y, _content_x + 4, _list_y + 4, 70, 16)) {
          _show_msg_detail = false;
          return true;
        }
        return true;
      }

      int total = _task->getStoredMessageCount();
      int btn_x = _content_x + _content_w - _scroll_btn_w;
      if (isInRect(x, y, btn_x, _scroll_up_y, _scroll_btn_w, _row_h)) {
        if (_msg_cursor > 0) _msg_cursor--;
        return true;
      }
      if (isInRect(x, y, btn_x, _scroll_down_y, _scroll_btn_w, _row_h)) {
        if (_msg_cursor < total - 1) _msg_cursor++;
        return true;
      }

      if (y >= _list_y) {
        int row = (y - _list_y) / _row_h;
        if (row >= 0 && row < _list_rows) {
          int list_w = _content_w - _scroll_btn_w - 4;
          if (x < _content_x + list_w) {
            int idx = _msg_scroll + row;
            if (idx >= 0 && idx < total) {
              _msg_cursor = idx;
              _show_msg_detail = true;
              return true;
            }
          }
        }
      }
      return true;
    }

    if (_tab == TAB_CHAT) {
        if (_chat_dropdown_open) {
            int dx = _content_x + 10;
            int dy = _list_y + 26;
            int dw = _content_w - 20;
            if (isInRect(x, y, dx, dy, dw, 120)) {
                int row = (y - dy - 6) / 20;
                if (row >= 0 && row < MAX_GROUP_CHANNELS) {
                    ChannelDetails ch;
                    if (the_mesh.getChannel(row, ch)) {
                        _active_chat_idx = row;
                        _active_chat_is_group = true;
                    }
                }
                _chat_dropdown_open = false;
                return true;
            }
            _chat_dropdown_open = false;
            return true;
        }

        if (isInRect(x, y, _content_x + 4, _list_y + 4, _content_w - 8, 22)) {
            _chat_dropdown_open = true;
            return true;
        }

        int input_y = _screen_h - 26;
        if (isInRect(x, y, _content_x + 1, input_y - 1, _content_w - 2, 26)) {
            _keyboard_visible = true;
            return true;
        }

        if (y > _list_y + 30 && y < input_y) {
            // Scroll history? (Optional)
        }
        return true;
    }

    if (_tab == TAB_NEARBY) {
      the_mesh.getRecentlyHeard(recent, UI_RECENT_LIST_SIZE);
      int total = 0;
      for (int i = 0; i < UI_RECENT_LIST_SIZE; i++) {
        if (recent[i].name[0] != 0) total++;
      }
      if (total == 0) return true;

      int btn_x = _content_x + _content_w - _scroll_btn_w;
      if (isInRect(x, y, btn_x, _scroll_up_y, _scroll_btn_w, _row_h)) {
        if (_nearby_scroll > 0) _nearby_scroll--;
        return true;
      }
      if (isInRect(x, y, btn_x, _scroll_down_y, _scroll_btn_w, _row_h)) {
        if (_nearby_scroll < total - 1) _nearby_scroll++;
        return true;
      }
      return true;
    }

    if (_tab == TAB_RADIO) {
      int left_btn_x = _content_x + 6;
      int right_btn_x = left_btn_x + _radio_toggle_w + 8;
      if (isInRect(x, y, left_btn_x, _radio_toggle_y, _radio_toggle_w, _radio_toggle_h)) {
        _radio_raw_mode = false;
        return true;
      }
      if (isInRect(x, y, right_btn_x, _radio_toggle_y, _radio_toggle_w, _radio_toggle_h)) {
        _radio_raw_mode = true;
        return true;
      }
      if (_radio_raw_mode && isInRect(x, y, _content_x + 8, _radio_reset_y, _content_w - 16, _radio_reset_h)) {
        radio_driver.resetStats();
        the_mesh.resetStats();
        _task->showAlert("Radio stats reset", 900);
        return true;
      }
      return true;
    }

    if (_tab == TAB_LINK) {
      if (isInRect(x, y, _content_x + 8, _link_ble_btn_y, _content_w - 16, _link_btn_h)) {
        if (_task->isSerialEnabled()) {
          _task->disableSerial();
        } else {
          _task->enableSerial();
        }
        return true;
      }
      if (isInRect(x, y, _content_x + 8, _link_adv_btn_y, _content_w - 16, _link_btn_h)) {
        _task->notify(UIEventType::ack);
        if (the_mesh.advert()) {
          _task->showAlert("Advert sent!", 1000);
        } else {
          _task->showAlert("Advert failed..", 1000);
        }
        return true;
      }
    }

    if (_tab == TAB_POWER) {
      if (!isInRect(x, y, _content_x + 8, _power_btn_y, _content_w - 16, _power_btn_h)) {
        return true;
      }
      if (_power_armed && (int32_t)(_power_armed_until - millis()) > 0) {
        _task->shutdown();
      } else {
        _power_armed = true;
        _power_armed_until = millis() + 2000;
      }
      return true;
    }

    return false;
  }
};

class MsgPreviewScreen : public UIScreen {
  UITask* _task;
  mesh::RTCClock* _rtc;

  struct MsgEntry {
    uint32_t timestamp;
    char origin[62];
    char msg[78];
  };
  #define MAX_UNREAD_MSGS   32
  int num_unread;
  int head = MAX_UNREAD_MSGS - 1; // index of latest unread message
  MsgEntry unread[MAX_UNREAD_MSGS];

public:
  MsgPreviewScreen(UITask* task, mesh::RTCClock* rtc) : _task(task), _rtc(rtc) { num_unread = 0; }

  void addPreview(uint8_t path_len, const char* from_name, const char* msg) {
    head = (head + 1) % MAX_UNREAD_MSGS;
    if (num_unread < MAX_UNREAD_MSGS) num_unread++;

    auto p = &unread[head];
    p->timestamp = _rtc->getCurrentTime();
    if (path_len == 0xFF) {
      sprintf(p->origin, "(D) %s:", from_name);
    } else {
      sprintf(p->origin, "(%d) %s:", (uint32_t) path_len, from_name);
    }
    StrHelper::strncpy(p->msg, msg, sizeof(p->msg));
  }

  int render(DisplayDriver& display) override {
    char tmp[16];
    display.setColor(DisplayDriver::DARK);
    display.fillRect(0, 0, display.width(), display.height());
    display.setCursor(0, 0);
    display.setTextSize(1);
    display.setColor(DisplayDriver::LIGHT);
    sprintf(tmp, "Unread: %d", num_unread);
    display.print(tmp);

    auto p = &unread[head];

    int secs = _rtc->getCurrentTime() - p->timestamp;
    if (secs < 60) {
      sprintf(tmp, "%ds", secs);
    } else if (secs < 60*60) {
      sprintf(tmp, "%dm", secs / 60);
    } else {
      sprintf(tmp, "%dh", secs / (60*60));
    }
    display.setCursor(display.width() - display.getTextWidth(tmp) - 2, 0);
    display.print(tmp);

    display.drawRect(0, 11, display.width(), 1);  // horiz line

    display.setCursor(0, 14);
    display.setColor(DisplayDriver::LIGHT);
    char filtered_origin[sizeof(p->origin)];
    display.translateUTF8ToBlocks(filtered_origin, p->origin, sizeof(filtered_origin));
    display.print(filtered_origin);

    display.setCursor(0, 25);
    display.setColor(DisplayDriver::LIGHT);
    char filtered_msg[sizeof(p->msg)];
    display.translateUTF8ToBlocks(filtered_msg, p->msg, sizeof(filtered_msg));
    display.printWordWrap(filtered_msg, display.width());

    return 0;
  }

  bool handleInput(char c) override {
    if (c == KEY_NEXT || c == KEY_RIGHT) {
      head = (head + MAX_UNREAD_MSGS - 1) % MAX_UNREAD_MSGS;
      num_unread--;
      if (num_unread == 0) {
        _task->gotoHomeScreen();
      }
      return true;
    }
    if (c == KEY_ENTER) {
      num_unread = 0;  // clear unread queue
      _task->gotoHomeScreen();
      return true;
    }
    return false;
  }
};

void UITask::begin(DisplayDriver* display, SensorManager* sensors, NodePrefs* node_prefs) {
  _display = display;
  _sensors = sensors;
  _auto_off = millis() + AUTO_OFF_MILLIS;

#if defined(PIN_USER_BTN)
  user_btn.begin();
#endif
#if defined(PIN_USER_BTN_ANA)
  analog_btn.begin();
#endif

#if defined(TOUCH_CS) && defined(TOUCH_IRQ) && defined(TOUCH_SPI_SCK) && defined(TOUCH_SPI_MOSI) && defined(TOUCH_SPI_MISO)
  pinMode(TOUCH_IRQ, INPUT_PULLUP);
  xpt2046_spi.begin(TOUCH_SPI_SCK, TOUCH_SPI_MISO, TOUCH_SPI_MOSI, TOUCH_CS);
  xpt2046_ready = xpt2046.begin(xpt2046_spi);
  if (xpt2046_ready) {
    xpt2046.setRotation(TOUCH_ROTATION);
  }
#endif

  _node_prefs = node_prefs;

#if ENV_INCLUDE_GPS == 1
  // Apply GPS preferences from stored prefs
  if (_sensors != NULL && _node_prefs != NULL) {
    _sensors->setSettingValue("gps", _node_prefs->gps_enabled ? "1" : "0");
    if (_node_prefs->gps_interval > 0) {
      char interval_str[12];  // Max: 24 hours = 86400 seconds (5 digits + null)
      sprintf(interval_str, "%u", _node_prefs->gps_interval);
      _sensors->setSettingValue("gps_interval", interval_str);
    }
  }
#endif

  if (_display != NULL) {
    _display->turnOn();
  }

#ifdef PIN_BUZZER
  buzzer.begin();
  buzzer.quiet(_node_prefs->buzzer_quiet);
#endif

#ifdef PIN_VIBRATION
  vibration.begin();
#endif

  ui_started_at = millis();
  _alert_expiry = 0;

  splash = new SplashScreen(this);
  home = new HomeScreen(this, &rtc_clock, sensors, node_prefs);
  msg_preview = new MsgPreviewScreen(this, &rtc_clock);
  setCurrScreen(splash);
}

void UITask::showAlert(const char* text, int duration_millis) {
  strcpy(_alert, text);
  _alert_expiry = millis() + duration_millis;
}

void UITask::notify(UIEventType t) {
#if defined(PIN_BUZZER)
switch(t){
  case UIEventType::contactMessage:
    // gemini's pick
    buzzer.play("MsgRcv3:d=4,o=6,b=200:32e,32g,32b,16c7");
    break;
  case UIEventType::channelMessage:
    buzzer.play("kerplop:d=16,o=6,b=120:32g#,32c#");
    break;
  case UIEventType::ack:
    buzzer.play("ack:d=32,o=8,b=120:c");
    break;
  case UIEventType::roomMessage:
  case UIEventType::newContactMessage:
  case UIEventType::none:
  default:
    break;
}
#endif

#ifdef PIN_VIBRATION
  // Trigger vibration for all UI events except none
  if (t != UIEventType::none) {
      vibration.trigger();
  }
#endif
}

void UITask::storeMessage(uint8_t path_len, const char* from_name, const char* text, uint8_t channel_idx, bool is_group, bool is_sent, uint32_t ack_hash) {
  _messages_head = (_messages_head + 1) % MAX_STORED_MESSAGES;
  if (_messages_count < MAX_STORED_MESSAGES) _messages_count++;

  MessageEntry& e = _messages[_messages_head];
  e.timestamp = rtc_clock.getCurrentTime();
  e.channel_idx = channel_idx;
  e.is_group = is_group;
  e.is_sent = is_sent;
  e.status = 0; // pending
  e.repeat_count = 0;
  e.ack_hash = ack_hash;
  StrHelper::strzcpy(e.origin, from_name, sizeof(e.origin));
  StrHelper::strzcpy(e.text, text, sizeof(e.text));
}

void UITask::updateMessageAck(uint32_t ack_hash) {
  for (int i = 0; i < _messages_count; i++) {
    if (_messages[i].ack_hash == ack_hash && _messages[i].is_sent) {
      _messages[i].status = 1; // Acked/Repeated
      _messages[i].repeat_count++;
      return;
    }
  }
}

bool UITask::getStoredMessage(int newest_index, MessageEntry& out) const {
  if (_messages_count <= 0 || _messages_head < 0) return false;
  if (newest_index < 0 || newest_index >= _messages_count) return false;

  int idx = _messages_head - newest_index;
  while (idx < 0) idx += MAX_STORED_MESSAGES;
  out = _messages[idx];
  return true;
}


void UITask::msgRead(int msgcount) {
  _msgcount = msgcount;
  if (msgcount == 0) {
    gotoHomeScreen();
  }
}

void UITask::newMsg(uint8_t path_len, const char* from_name, const char* text, int msgcount, uint8_t channel_idx, bool is_group) {
  _msgcount = msgcount;
 
  storeMessage(path_len, from_name, text, channel_idx, is_group);
  
  if (home != NULL) {
      HomeScreen* hs = (HomeScreen*)home;
      hs->setUnread(HomeScreen::TAB_MESSAGES);
      if (channel_idx != 0xFF || is_group) {
          hs->setUnread(HomeScreen::TAB_CHAT);
      }
  }

  setCurrScreen(home);

  if (_display != NULL) {
    if (!_display->isOn() && !hasConnection()) {
      _display->turnOn();
    }
    if (_display->isOn()) {
    _auto_off = millis() + AUTO_OFF_MILLIS;  // extend the auto-off timer
    _next_refresh = 0;  // trigger refresh
    }
  }
}

void UITask::userLedHandler() {
#ifdef PIN_STATUS_LED
  int cur_time = millis();
  if (cur_time > next_led_change) {
    if (led_state == 0) {
      led_state = 1;
      if (_msgcount > 0) {
        last_led_increment = LED_ON_MSG_MILLIS;
      } else {
        last_led_increment = LED_ON_MILLIS;
      }
      next_led_change = cur_time + last_led_increment;
    } else {
      led_state = 0;
      next_led_change = cur_time + LED_CYCLE_MILLIS - last_led_increment;
    }
    digitalWrite(PIN_STATUS_LED, led_state == LED_STATE_ON);
  }
#endif
}

void UITask::setCurrScreen(UIScreen* c) {
  curr = c;
  if (_display != NULL && _display->isOn()) {
    _display->clear();
  }
  _next_refresh = 0;
}

/*
  hardware-agnostic pre-shutdown activity should be done here
*/
void UITask::shutdown(bool restart){

  #ifdef PIN_BUZZER
  /* note: we have a choice here -
     we can do a blocking buzzer.loop() with non-deterministic consequences
     or we can set a flag and delay the shutdown for a couple of seconds
     while a non-blocking buzzer.loop() plays out in UITask::loop()
  */
  buzzer.shutdown();
  uint32_t buzzer_timer = millis(); // fail-safe shutdown
  while (buzzer.isPlaying() && (millis() - 2500) < buzzer_timer)
    buzzer.loop();

  #endif // PIN_BUZZER

  if (restart) {
    _board->reboot();
  } else {
    _display->turnOff();
    radio_driver.powerOff();
    _board->powerOff();
  }
}

bool UITask::isButtonPressed() const {
#ifdef PIN_USER_BTN
  return user_btn.isPressed();
#else
  return false;
#endif
}

void UITask::loop() {
  char c = 0;
#if UI_HAS_JOYSTICK
  int ev = user_btn.check();
  if (ev == BUTTON_EVENT_CLICK) {
    c = checkDisplayOn(KEY_ENTER);
  } else if (ev == BUTTON_EVENT_LONG_PRESS) {
    c = handleLongPress(KEY_ENTER);  // REVISIT: could be mapped to different key code
  }
  ev = joystick_left.check();
  if (ev == BUTTON_EVENT_CLICK) {
    c = checkDisplayOn(KEY_LEFT);
  } else if (ev == BUTTON_EVENT_LONG_PRESS) {
    c = handleLongPress(KEY_LEFT);
  }
  ev = joystick_right.check();
  if (ev == BUTTON_EVENT_CLICK) {
    c = checkDisplayOn(KEY_RIGHT);
  } else if (ev == BUTTON_EVENT_LONG_PRESS) {
    c = handleLongPress(KEY_RIGHT);
  }
  ev = back_btn.check();
  if (ev == BUTTON_EVENT_TRIPLE_CLICK) {
    c = handleTripleClick(KEY_SELECT);
  }
#elif defined(PIN_USER_BTN)
  int ev = user_btn.check();
  if (ev == BUTTON_EVENT_CLICK) {
    c = checkDisplayOn(KEY_NEXT);
  } else if (ev == BUTTON_EVENT_LONG_PRESS) {
    c = handleLongPress(KEY_ENTER);
  } else if (ev == BUTTON_EVENT_DOUBLE_CLICK) {
    c = handleDoubleClick(KEY_PREV);
  } else if (ev == BUTTON_EVENT_TRIPLE_CLICK) {
    c = handleTripleClick(KEY_SELECT);
  }
#endif
#if defined(PIN_USER_BTN_ANA)
  if (abs(millis() - _analogue_pin_read_millis) > 10) {
    ev = analog_btn.check();
    if (ev == BUTTON_EVENT_CLICK) {
      c = checkDisplayOn(KEY_NEXT);
    } else if (ev == BUTTON_EVENT_LONG_PRESS) {
      c = handleLongPress(KEY_ENTER);
    } else if (ev == BUTTON_EVENT_DOUBLE_CLICK) {
      c = handleDoubleClick(KEY_PREV);
    } else if (ev == BUTTON_EVENT_TRIPLE_CLICK) {
      c = handleTripleClick(KEY_SELECT);
    }
    _analogue_pin_read_millis = millis();
  }
#endif
#if defined(TOUCH_CS) && defined(TOUCH_IRQ) && defined(TOUCH_SPI_SCK) && defined(TOUCH_SPI_MOSI) && defined(TOUCH_SPI_MISO)
  if (c == 0 && xpt2046_ready) {
    bool is_down = xpt2046.touched();
    if (is_down) {
      TS_Point p = xpt2046.getPoint();
      updateTouchDot(_display, p.x, p.y);
      if (_display != NULL && _display->isOn()) {
        _next_refresh = 0;
      }
      if (!xpt2046_was_down && (millis() - xpt2046_last_tap_millis) > 90) {
        int tx = 0, ty = 0;
        mapRawTouchToDisplay(_display, p.x, p.y, &tx, &ty);
        bool touch_consumed = false;
        if (_display != NULL && _display->isOn() && curr != NULL) {
          touch_consumed = curr->handleTouch(tx, ty);
        }
        if (touch_consumed) {
          _auto_off = millis() + AUTO_OFF_MILLIS;
          _next_refresh = 0;
        } else {
          c = checkDisplayOn(mapXpt2046TouchToKey(_display, p.x, p.y));
        }
        xpt2046_last_tap_millis = millis();
      }
    }
    xpt2046_was_down = is_down;
  }
#endif
#if defined(BACKLIGHT_BTN)
  if (millis() > next_backlight_btn_check) {
    bool touch_state = digitalRead(PIN_BUTTON2);
#if defined(DISP_BACKLIGHT)
    digitalWrite(DISP_BACKLIGHT, !touch_state);
#elif defined(EXP_PIN_BACKLIGHT)
    expander.digitalWrite(EXP_PIN_BACKLIGHT, !touch_state);
#endif
    next_backlight_btn_check = millis() + 300;
  }
#endif

  if (c != 0 && curr) {
    curr->handleInput(c);
    _auto_off = millis() + AUTO_OFF_MILLIS;   // extend auto-off timer
    _next_refresh = 0;  // trigger refresh
  }

  userLedHandler();

#ifdef PIN_BUZZER
  if (buzzer.isPlaying())  buzzer.loop();
#endif

  if (curr) curr->poll();

  if (_display != NULL && _display->isOn()) {
    if (millis() >= _next_refresh && curr) {
      _display->startFrame();
      int delay_millis = curr->render(*_display);
      if (millis() < _alert_expiry) {  // render alert popup
        _display->setTextSize(1);
        int y = _display->height() / 3;
        int p = _display->height() / 32;
        _display->setColor(DisplayDriver::DARK);
        _display->fillRect(p, y, _display->width() - p*2, y);
        _display->setColor(DisplayDriver::LIGHT);  // draw box border
        _display->drawRect(p, y, _display->width() - p*2, y);
        _display->drawTextCentered(_display->width() / 2, y + p*3, _alert);
        _next_refresh = _alert_expiry;   // will need refresh when alert is dismissed
      } else {
        if (delay_millis <= 0) {
          _next_refresh = 0xFFFFFFFFUL;  // event-driven: no periodic redraw
        } else {
          _next_refresh = millis() + (uint32_t)delay_millis;
        }
      }
#if defined(TOUCH_CS) && defined(TOUCH_IRQ) && defined(TOUCH_SPI_SCK) && defined(TOUCH_SPI_MOSI) && defined(TOUCH_SPI_MISO)
      drawTouchDebugDot(*_display);
#endif
      _display->endFrame();
    }
#if AUTO_OFF_MILLIS > 0
    if (millis() > _auto_off) {
      _display->turnOff();
    }
#endif
  }

#ifdef PIN_VIBRATION
  vibration.loop();
#endif

#ifdef AUTO_SHUTDOWN_MILLIVOLTS
  if (millis() > next_batt_chck) {
    uint16_t milliVolts = getBattMilliVolts();
    if (milliVolts > 0 && milliVolts < AUTO_SHUTDOWN_MILLIVOLTS) {

      // show low battery shutdown alert
      // we should only do this for eink displays, which will persist after power loss
      #if defined(THINKNODE_M1) || defined(LILYGO_TECHO)
      if (_display != NULL) {
        _display->startFrame();
        _display->setTextSize(2);
        _display->setColor(DisplayDriver::LIGHT);
        _display->drawTextCentered(_display->width() / 2, 20, "Low Battery.");
        _display->drawTextCentered(_display->width() / 2, 40, "Shutting Down!");
        _display->endFrame();
      }
      #endif

      shutdown();

    }
    next_batt_chck = millis() + 8000;
  }
#endif
}

char UITask::checkDisplayOn(char c) {
  if (_display != NULL) {
    if (!_display->isOn()) {
      _display->turnOn();   // turn display on and consume event
      c = 0;
    }
    _auto_off = millis() + AUTO_OFF_MILLIS;   // extend auto-off timer
    _next_refresh = 0;  // trigger refresh
  }
  return c;
}

char UITask::handleLongPress(char c) {
  if (millis() - ui_started_at < 8000) {   // long press in first 8 seconds since startup -> CLI/rescue
    the_mesh.enterCLIRescue();
    c = 0;   // consume event
  }
  return c;
}

char UITask::handleDoubleClick(char c) {
  MESH_DEBUG_PRINTLN("UITask: double click triggered");
  checkDisplayOn(c);
  return c;
}

char UITask::handleTripleClick(char c) {
  MESH_DEBUG_PRINTLN("UITask: triple click triggered");
  checkDisplayOn(c);
  toggleBuzzer();
  c = 0;
  return c;
}

bool UITask::getGPSState() {
  if (_sensors != NULL) {
    int num = _sensors->getNumSettings();
    for (int i = 0; i < num; i++) {
      if (strcmp(_sensors->getSettingName(i), "gps") == 0) {
        return !strcmp(_sensors->getSettingValue(i), "1");
      }
    }
  } 
  return false;
}

void UITask::toggleGPS() {
    if (_sensors != NULL) {
    // toggle GPS on/off
    int num = _sensors->getNumSettings();
    for (int i = 0; i < num; i++) {
      if (strcmp(_sensors->getSettingName(i), "gps") == 0) {
        if (strcmp(_sensors->getSettingValue(i), "1") == 0) {
          _sensors->setSettingValue("gps", "0");
          _node_prefs->gps_enabled = 0;
          notify(UIEventType::ack);
        } else {
          _sensors->setSettingValue("gps", "1");
          _node_prefs->gps_enabled = 1;
          notify(UIEventType::ack);
        }
        the_mesh.savePrefs();
        showAlert(_node_prefs->gps_enabled ? "GPS: Enabled" : "GPS: Disabled", 800);
        _next_refresh = 0;
        break;
      }
    }
  }
}

void UITask::toggleBuzzer() {
    // Toggle buzzer quiet mode
  #ifdef PIN_BUZZER
    if (buzzer.isQuiet()) {
      buzzer.quiet(false);
      notify(UIEventType::ack);
    } else {
      buzzer.quiet(true);
    }
    _node_prefs->buzzer_quiet = buzzer.isQuiet();
    the_mesh.savePrefs();
    showAlert(buzzer.isQuiet() ? "Buzzer: OFF" : "Buzzer: ON", 800);
    _next_refresh = 0;  // trigger refresh
  #endif
}
