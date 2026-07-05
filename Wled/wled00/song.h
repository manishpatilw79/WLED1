#pragma once
/*
 * song.h
 * Song playback controller.
 *
 * Owns the .bin song playback engine and pushes each record's JSON packet
 * straight into WLED's internal JSON state parser. There is no UART, no
 * external command protocol and no HTTP round-trip anywhere in this path -
 * a song file is a sequence of (delay, JSON) pairs applied to WLED exactly
 * like a JSON API client would, just internally and with precise timing.
 *
 * Integration points (called directly from wled.cpp, see WLED::setup()/loop()):
 *   song.begin()  -> once at boot
 *   song.tick()   -> every loop() iteration, non-blocking
 */
#include <Arduino.h>
#include "song_storage.h"
#include "song_player.h"
#include "song_web.h"


// Applies one JSON packet to WLED via the internal JSON state parser.
// Used as the SongPlayer::JsonHandler callback. Declared here so it is
// visible wherever song.h is included.
void songApplyJson(const String& json);

class Song {
  public:
    // Called once from WLED::setup(). Brings up the /songs storage layer,
    // the playback engine, and registers the /song web UI + /songs/* endpoints.
    void begin();

    // Called every loop() iteration from WLED::loop(). Never blocks.
    void tick();

    // ---- Status accessors used by xml.cpp and ST7789_display.cpp ----
    const String& currentFile() const { return songPlayer.currentFile(); }

    String stateString() const {
      switch (songPlayer.state()) {
        case SongState::PLAYING: return F("PLAYING");
        case SongState::PAUSED:  return F("PAUSED");
        default:                 return F("IDLE");
      }
    }

    size_t fsUsedBytes()  const { return songStorage.usedBytes(); }
    size_t fsTotalBytes() const { return songStorage.totalBytes(); }
};

extern Song song;
