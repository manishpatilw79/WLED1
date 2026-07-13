#pragma once
/*
 * ST7735_display.h
 *
 * Lightweight ST7735 128x160 status display for WLED (ESP8266 / TFT_eSPI).
 * Plain v1 usermod - NOT a WLED Usermod V2, NOT a PlatformIO library.
 * Wired in only through userSetup()/userConnected()/userLoop() in usermod.cpp.
 *
 * RAM rules followed throughout:
 *   - no Arduino String
 *   - only char[], snprintf(), const char*
 *   - no malloc/new/vectors, all buffers are static/fixed size
 *
 * SPI is owned by TFT_eSPI (hardware SPI, shared bus). TFT_eSPI wraps every
 * transfer in its own begin/endTransaction, so any future device on the same
 * bus (e.g. an external W25Qxx flash chip) just needs its own CS pin - this
 * file never calls SPI.begin()/end() itself and never leaves the bus locked.
 */
#include <TFT_eSPI.h>

class ST7735DisplayClass {
  public:
    void begin();
    void connected();
    void loop();

  private:
    TFT_eSPI _tft = TFT_eSPI();



    enum : uint8_t { PAGE_NORMAL = 0, PAGE_SONG = 1 };
    uint8_t _activePage   = 0xFF;
    bool    _forceRefresh = true;

    uint32_t _lastFastCheck = 0;
    uint32_t _lastSecondTick = 0;
    uint32_t _lastFsTick     = 0;

    int8_t   _lastHour = -1, _lastMinute = -1, _lastSecond = -1;
    uint8_t  _lastBri = 0xFF;
    uint8_t  _lastEffect = 0xFF;
    uint8_t  _lastSpeed = 0xFF, _lastIntensity = 0xFF;
    uint32_t _lastHeapKB = 0xFFFFFFFF;
    uint16_t _lastFps = 0xFFFF;
    size_t   _lastFsUsedKB = (size_t)-1, _lastFsTotalKB = (size_t)-1;
    char     _lastSongFile[24]   = {0};
    char     _lastSongStatus[10] = {0};
    bool     _lastSongActive = false;


    void showBootLogo();
    void switchPage(uint8_t page);
    void drawNormalLabels();
    void drawSongLabels();
    void updateNormalPage(bool doSecond, bool doFs, bool force);
    void updateSongPage(bool doSecond, bool doFs, bool force);

    void printField(int16_t x, int16_t y, int16_t w, const char* text, uint16_t color = TFT_WHITE);
};

extern ST7735DisplayClass ST7735Display;
