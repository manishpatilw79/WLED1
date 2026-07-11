/*
 * ST7735_display.cpp
 *
 * See ST7735_display.h for design notes (RAM rules, SPI sharing, v1
 * usermod API). This file only ever redraws the fixed-width value box of
 * a field that actually changed - never the full screen - to keep SPI
 * traffic (and therefore LED FPS impact) minimal.
 */
#include "wled.h"
#include "ST7735_display.h"

ST7735DisplayClass ST7735Display;

// ---------------------------------------------------------------------
// Layout constants (160x128, landscape, TFT_eSPI default 6x8 font, size 1)
// Left margin = 2px, top margin = 2px, all boxes verified to stay within
// the 160x128 visible area (right/bottom margin kept >= 2px as well).
// Every "VAL_*_X / W_*" pair is a fixed reserved box: printField() always
// clears exactly that box before writing, so field width must be able to
// hold the largest value that field can ever show.
// ---------------------------------------------------------------------
static const int16_t LABEL_X      = 2;
static const int16_t COL2_LABEL_X = 76;  // 2nd label on a shared row (Int:, FS:)

static const int16_t VAL_TIME_X   = 42,  W_TIME   = 60;   // ends 102
static const int16_t VAL_BRIGHT_X = 54,  W_BRIGHT = 40;   // ends 94
static const int16_t VAL_EFFECT_X = 54,  W_EFFECT = 102;  // ends 156
static const int16_t VAL_SPD_X    = 36,  W_SPD    = 34;   // ends 70
static const int16_t VAL_INT_X    = 110, W_INT    = 40;   // ends 150
static const int16_t VAL_RAM_X    = 36,  W_RAM    = 34;   // ends 70
static const int16_t VAL_FS_X     = 104, W_FS     = 52;   // ends 156
static const int16_t VAL_FPS_X    = 36,  W_FPS    = 40;   // ends 76
static const int16_t VAL_SONG_X   = 42,  W_SONG   = 112;  // ends 154
static const int16_t VAL_STATUS_X = 54,  W_STATUS = 100;  // ends 154

// Normal page rows - top margin 2px, 20px pitch, clearly separated
static const int16_t Y_N_TIME = 26, Y_N_BRIGHT = 39, Y_N_EFFECT = 53,
                      Y_N_SPDINT = 67, Y_N_RAMFS = 81, Y_N_FPS = 95;

// Song page rows
static const int16_t Y_S_TIME = 26, Y_S_SONG = 42, Y_S_STATUS = 59,
                      Y_S_RAMFS = 77, Y_S_FPS = 95;

static const uint32_t FAST_INTERVAL_MS   = 100;
static const uint32_t SECOND_INTERVAL_MS = 1000;
static const uint32_t FS_INTERVAL_MS     = 5000;

// ---------------------------------------------------------------------
// Per-line colors - each line has its own color, kept distinct within
// whichever page it appears on.
// ---------------------------------------------------------------------
static const uint16_t COLOR_TIME      = TFT_YELLOW;
static const uint16_t COLOR_SONG      = TFT_CYAN;
static const uint16_t COLOR_STATUS    = TFT_GREEN;
static const uint16_t COLOR_BRIGHT    = TFT_ORANGE;
static const uint16_t COLOR_EFFECT    = TFT_MAGENTA;
static const uint16_t COLOR_SPEED     = TFT_WHITE;
static const uint16_t COLOR_INTENSITY = TFT_CYAN;
static const uint16_t COLOR_RAM       = TFT_PINK;
static const uint16_t COLOR_FS        = TFT_BLUE;
static const uint16_t COLOR_FPS       = TFT_RED;

// Font1 @ textSize(1) advances 6px per character - used to convert a pixel
// box width into a max character count for wrap-free truncation.
static const int16_t CHAR_W = 6;

// ---------------------------------------------------------------------
// Truncates text in place to fit within widthPx, appending "..." if it
// had to cut anything. Never wraps - the caller always follows this with
// a single print() on a single cursor line.
// ---------------------------------------------------------------------
static void truncateToWidth(char* text, int16_t widthPx) {
  const int16_t maxChars = widthPx / CHAR_W;
  int16_t len = (int16_t)strlen(text);
  if (len <= maxChars) return;
  if (maxChars > 3) {
    text[maxChars - 3] = '.';
    text[maxChars - 2] = '.';
    text[maxChars - 1] = '.';
    text[maxChars] = '\0';
  } else if (maxChars > 0) {
    text[maxChars] = '\0';
  }
}

// ---------------------------------------------------------------------
// public API
// ---------------------------------------------------------------------
void ST7735DisplayClass::begin() {
  _tft.init();
  _tft.setRotation(1);          // landscape, 160x128 (use 3 if display is mirrored)
  _tft.fillScreen(TFT_BLACK);
  _tft.setTextFont(1);
  _tft.setTextSize(1);
  _tft.setTextWrap(false);      // never wrap - every line stays on one row
  _tft.setTextColor(TFT_WHITE, TFT_BLACK);
  // Backlight is assumed wired directly to VCC (no BL pin was specified) -
  // it is on as soon as the module has power, nothing to drive here.

  // <<<<< इथे animation टाक >>>>>

  for (int repeat = 0; repeat < 3; repeat++) {
    for (int dots = 1; dots <= 7; dots++) {

      char txt[32] = "Initializing";
      for (int i = 0; i < dots; i++) strcat(txt, ".");

      _tft.fillScreen(TFT_BLACK);
      _tft.setCursor(15, 60);
      _tft.print(txt);

      delay(150);
    }
  }

  _tft.fillScreen(TFT_BLACK);

  _activePage   = 0xFF;
  _forceRefresh = true;
  switchPage(PAGE_NORMAL);
}  

// Animation page end. 

void ST7735DisplayClass::connected() {
  // Nothing shown on-screen depends on WiFi state; just make sure every
  // field gets repainted once so the display is guaranteed in sync.
  _forceRefresh = true;
}

void ST7735DisplayClass::loop() {
  uint32_t now = millis();

  bool doFast   = _forceRefresh || (now - _lastFastCheck   >= FAST_INTERVAL_MS);
  bool doSecond = _forceRefresh || (now - _lastSecondTick  >= SECOND_INTERVAL_MS);
  bool doFs     = _forceRefresh || (now - _lastFsTick      >= FS_INTERVAL_MS);

  if (!doFast && !doSecond && !doFs) return; // nothing due - skip all SPI work

  if (doFast)   _lastFastCheck  = now;
  if (doSecond) _lastSecondTick = now;
  if (doFs)     _lastFsTick     = now;

  if (doFast) {
    bool songActive = !song.stateString().equalsIgnoreCase(F("IDLE"));
    uint8_t wantPage = songActive ? PAGE_SONG : PAGE_NORMAL;
    if (wantPage != _activePage) {
      switchPage(wantPage);   // also sets _forceRefresh = true
    }
  }

  bool force = _forceRefresh;
  if (_activePage == PAGE_SONG) updateSongPage(doSecond, doFs, force);
  else                          updateNormalPage(doSecond, doFs, force);

  _forceRefresh = false;
}

// ---------------------------------------------------------------------
// page switching
// ---------------------------------------------------------------------
void ST7735DisplayClass::switchPage(uint8_t page) {
  _activePage = page;
  _tft.fillScreen(TFT_BLACK);
  if (page == PAGE_SONG) drawSongLabels();
  else                   drawNormalLabels();

  // invalidate cached values so the next update draws every field once
  _lastHour = _lastMinute = _lastSecond = -1;
  _lastBri = _lastEffect = _lastSpeed = _lastIntensity = 0xFF;
  _lastHeapKB = 0xFFFFFFFF;
  _lastFps = 0xFFFF;
  _lastFsUsedKB = _lastFsTotalKB = (size_t)-1;
  _lastSongFile[0] = '\0';
  _lastSongStatus[0] = '\0';
  _forceRefresh = true;
}

void ST7735DisplayClass::drawNormalLabels() {
  _tft.setTextColor(COLOR_TIME, TFT_BLACK);
  _tft.setCursor(LABEL_X, Y_N_TIME);      _tft.print(F("Time :"));
  _tft.setTextColor(COLOR_BRIGHT, TFT_BLACK);
  _tft.setCursor(LABEL_X, Y_N_BRIGHT);    _tft.print(F("Bright :"));
  _tft.setTextColor(COLOR_EFFECT, TFT_BLACK);
  _tft.setCursor(LABEL_X, Y_N_EFFECT);    _tft.print(F("Effect :"));
  _tft.setTextColor(COLOR_SPEED, TFT_BLACK);
  _tft.setCursor(LABEL_X, Y_N_SPDINT);    _tft.print(F("Spd :"));
  _tft.setTextColor(COLOR_INTENSITY, TFT_BLACK);
  _tft.setCursor(COL2_LABEL_X, Y_N_SPDINT); _tft.print(F("Int :"));
  _tft.setTextColor(COLOR_RAM, TFT_BLACK);
  _tft.setCursor(LABEL_X, Y_N_RAMFS);     _tft.print(F("RAM :"));
  _tft.setTextColor(COLOR_FS, TFT_BLACK);
  _tft.setCursor(COL2_LABEL_X, Y_N_RAMFS);  _tft.print(F("FS :"));
  _tft.setTextColor(COLOR_FPS, TFT_BLACK);
  _tft.setCursor(LABEL_X, Y_N_FPS);       _tft.print(F("FPS :"));
}

void ST7735DisplayClass::drawSongLabels() {
  _tft.setTextColor(COLOR_TIME, TFT_BLACK);
  _tft.setCursor(LABEL_X, Y_S_TIME);   _tft.print(F("Time :"));
  _tft.setTextColor(COLOR_SONG, TFT_BLACK);
  _tft.setCursor(LABEL_X, Y_S_SONG);   _tft.print(F("Song :"));
  _tft.setTextColor(COLOR_STATUS, TFT_BLACK);
  _tft.setCursor(LABEL_X, Y_S_STATUS); _tft.print(F("Status :"));
  _tft.setTextColor(COLOR_RAM, TFT_BLACK);
  _tft.setCursor(LABEL_X, Y_S_RAMFS);  _tft.print(F("RAM :"));
  _tft.setTextColor(COLOR_FS, TFT_BLACK);
  _tft.setCursor(COL2_LABEL_X, Y_S_RAMFS); _tft.print(F("FS :"));
  _tft.setTextColor(COLOR_FPS, TFT_BLACK);
  _tft.setCursor(LABEL_X, Y_S_FPS);    _tft.print(F("FPS :"));
}

// ---------------------------------------------------------------------
// field redraw helper - clears its fixed box, then prints (no wrap)
// ---------------------------------------------------------------------
void ST7735DisplayClass::printField(int16_t x, int16_t y, int16_t w, const char* text, uint16_t color) {
  _tft.fillRect(x, y, w, 10, TFT_BLACK);
  _tft.setTextColor(color, TFT_BLACK);
  _tft.setCursor(x, y);
  _tft.print(text);
}

// ---------------------------------------------------------------------
// Normal page
// ---------------------------------------------------------------------
void ST7735DisplayClass::updateNormalPage(bool doSecond, bool doFs, bool force) {
  if (doSecond || force) {
    int h = hour(localTime), m = minute(localTime), s = second(localTime);
    if (force || h != _lastHour || m != _lastMinute || s != _lastSecond) {
      char buf[9];
      snprintf(buf, sizeof(buf), "%02d:%02d:%02d", h, m, s);
      printField(VAL_TIME_X, Y_N_TIME, W_TIME, buf, COLOR_TIME);
      _lastHour = h; _lastMinute = m; _lastSecond = s;
    }


//RAM Section  code

    float heapKB = (float)ESP.getFreeHeap() / 1024.0f;

if (force || (uint16_t)(heapKB * 10) != _lastHeapKB) {

    char buf[16];
    dtostrf(heapKB, 0, 1, buf);   // 1 decimal

    strcat(buf, " KB");

    printField(VAL_RAM_X, Y_N_RAMFS, W_RAM, buf, COLOR_RAM);

    _lastHeapKB = (uint16_t)(heapKB * 10);
}

// FPS SECTION

    uint16_t fps = strip.getFps();
    if (force || fps != _lastFps) {
      char buf[6];
      snprintf(buf, sizeof(buf), "%u", fps);
      printField(VAL_FPS_X, Y_N_FPS, W_FPS, buf, COLOR_FPS);
      _lastFps = fps;
    }
  }

// FS Section

  if (doFs || force) {
    size_t usedKB  = fsBytesUsed  / 1024;
    size_t totalKB = fsBytesTotal / 1024;

    if (force || usedKB != _lastFsUsedKB || totalKB != _lastFsTotalKB) {

        float usedMB  = (float)fsBytesUsed  / (1024.0f * 1024.0f);
        float totalMB = (float)fsBytesTotal / (1024.0f * 1024.0f);

        char buf[20];
        snprintf(buf, sizeof(buf), "%.2f/%.2fM", usedMB, totalMB);

        truncateToWidth(buf, W_FS);
        printField(VAL_FS_X, Y_N_RAMFS, W_FS, buf, COLOR_FS);

        _lastFsUsedKB  = usedKB;
        _lastFsTotalKB = totalKB;
    }
}
  // Cheap int comparisons - fine to check every fast tick for near-instant feel
  if (force || bri != _lastBri) {
    char buf[5];
    snprintf(buf, sizeof(buf), "%u", bri);
    printField(VAL_BRIGHT_X, Y_N_BRIGHT, W_BRIGHT, buf, COLOR_BRIGHT);
    _lastBri = bri;
  }

  if (force || effectCurrent != _lastEffect) {
    // Real WLED effect name, read directly from the FX.h-generated mode
    // table via WS2812FX - never a separate/hardcoded name list.
    char name[32];
    extractModeName(effectCurrent, strip.getModeData(effectCurrent), name, sizeof(name));
    truncateToWidth(name, W_EFFECT);
    printField(VAL_EFFECT_X, Y_N_EFFECT, W_EFFECT, name, COLOR_EFFECT);
    _lastEffect = effectCurrent;
  }

  if (force || effectSpeed != _lastSpeed) {
    char buf[5];
    snprintf(buf, sizeof(buf), "%u", effectSpeed);
    printField(VAL_SPD_X, Y_N_SPDINT, W_SPD, buf, COLOR_SPEED);
    _lastSpeed = effectSpeed;
  }

  if (force || effectIntensity != _lastIntensity) {
    char buf[5];
    snprintf(buf, sizeof(buf), "%u", effectIntensity);
    printField(VAL_INT_X, Y_N_SPDINT, W_INT, buf, COLOR_INTENSITY);
    _lastIntensity = effectIntensity;
  }
}

// ---------------------------------------------------------------------
// Song page
// ---------------------------------------------------------------------
void ST7735DisplayClass::updateSongPage(bool doSecond, bool doFs, bool force) {
  if (doSecond || force) {
    int h = hour(localTime), m = minute(localTime), s = second(localTime);
    if (force || h != _lastHour || m != _lastMinute || s != _lastSecond) {
      char buf[9];
      snprintf(buf, sizeof(buf), "%02d:%02d:%02d", h, m, s);
      printField(VAL_TIME_X, Y_S_TIME, W_TIME, buf, COLOR_TIME);
      _lastHour = h; _lastMinute = m; _lastSecond = s;
    }

// RAM SECTION

    float heapKB = (float)ESP.getFreeHeap() / 1024.0f;

if (force || (uint16_t)(heapKB * 10) != _lastHeapKB) {

    char buf[16];
    dtostrf(heapKB, 0, 1, buf);   // 1 decimal

    strcat(buf, " KB");

    printField(VAL_RAM_X, Y_N_RAMFS, W_RAM, buf, COLOR_RAM);

    _lastHeapKB = (uint16_t)(heapKB * 10);
}

// Fps section

    uint16_t fps = strip.getFps();
    if (force || fps != _lastFps) {
      char buf[6];
      snprintf(buf, sizeof(buf), "%u", fps);
      printField(VAL_FPS_X, Y_S_FPS, W_FPS, buf, COLOR_FPS);
      _lastFps = fps;
    }
  }

//  FS section

  if (doFs || force) {
    size_t usedKB  = fsBytesUsed  / 1024;
    size_t totalKB = fsBytesTotal / 1024;

    if (force || usedKB != _lastFsUsedKB || totalKB != _lastFsTotalKB) {

        float usedMB  = (float)fsBytesUsed  / (1024.0f * 1024.0f);
        float totalMB = (float)fsBytesTotal / (1024.0f * 1024.0f);

        char buf[20];
        snprintf(buf, sizeof(buf), "%.2f/%.2fM", usedMB, totalMB);

        truncateToWidth(buf, W_FS);
        printField(VAL_FS_X, Y_N_RAMFS, W_FS, buf, COLOR_FS);

        _lastFsUsedKB  = usedKB;
        _lastFsTotalKB = totalKB;
    }
}

  char file[24];
  snprintf(file, sizeof(file), "%s", song.currentFile().c_str());
  truncateToWidth(file, W_SONG);
  if (force || strcmp(file, _lastSongFile) != 0) {
    printField(VAL_SONG_X, Y_S_SONG, W_SONG, file, COLOR_SONG);
    strncpy(_lastSongFile, file, sizeof(_lastSongFile) - 1);
    _lastSongFile[sizeof(_lastSongFile) - 1] = '\0';
  }

  char status[10];
  snprintf(status, sizeof(status), "%s", song.stateString().c_str());
  truncateToWidth(status, W_STATUS);
  if (force || strcmp(status, _lastSongStatus) != 0) {
    printField(VAL_STATUS_X, Y_S_STATUS, W_STATUS, status, COLOR_STATUS);
    strncpy(_lastSongStatus, status, sizeof(_lastSongStatus) - 1);
    _lastSongStatus[sizeof(_lastSongStatus) - 1] = '\0';
  }
}
