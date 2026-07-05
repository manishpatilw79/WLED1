#pragma once

#include <Arduino.h>
#include "FS.h"
#include "LittleFS.h"
#include "song_storage.h"

// ---------------------------------------------------------------------------
// Song command IDs
//
// Each record in the .bin file now stores a numeric command + value instead
// of a raw JSON string. The firmware reconstructs the exact same JSON that
// was previously stored verbatim, via the switch statement in
// SongPlayer::buildJson().
//
// To add a new command: append a new ID below (never reuse/reorder existing
// values - already-generated .bin files depend on them) and add a matching
// case in buildJson().
// ---------------------------------------------------------------------------
enum SongCommandId : uint8_t {
  SONG_CMD_ON         = 0,
  SONG_CMD_OFF        = 1,
  SONG_CMD_TOGGLE     = 2,
  SONG_CMD_EFFECT     = 3,
  SONG_CMD_BRIGHTNESS = 4,
  SONG_CMD_SPEED      = 5,
  SONG_CMD_INTENSITY  = 6,
  SONG_CMD_PALETTE    = 7,
  SONG_CMD_PRESET     = 8,
  SONG_CMD_COLOR      = 9   // extension beyond the original spec, for the "C" TXT command
};

// On-disk record layout, little-endian, packed, fixed size:
//   uint32_t delayMs  (4 bytes) - ms to wait after the previous record fires
//   uint8_t  cmdId    (1 byte)  - one of the SongCommandId values above
//   uint16_t value    (2 bytes) - command-specific payload
#define SONG_RECORD_SIZE 7

// Longest JSON string buildJson() can produce for any current command.
// {"seg":[{"col":[[255,255,255,0]]}]} is the longest at 36 chars + NUL.
#ifndef SONG_MAX_JSON_LEN
#define SONG_MAX_JSON_LEN 64
#endif

enum class SongState : uint8_t {
  IDLE = 0,
  PLAYING,
  PAUSED
};

class SongPlayer {
public:

  typedef void (*JsonHandler)(const String& json);

  void begin(JsonHandler handler);

  bool play(const String& filename);

  void pause();

  void resume();

  void stop();

  void tick();

  SongState state() const { return _state; }

  const String& currentFile() const { return _filename; }

  const String& lastError() const { return _lastError; }

private:

  bool readNextRecord();

  // Builds the JSON packet for a given command/value pair into `out`
  // (a buffer of at least SONG_MAX_JSON_LEN bytes). Returns false if
  // the command ID is unrecognized.
  static bool buildJson(uint8_t cmdId, uint16_t value, char* out, size_t outLen);

  JsonHandler   _handler = nullptr;
  File          _file;
  String        _filename;
  SongState     _state = SongState::IDLE;
  unsigned long _nextEventAt = 0;
  unsigned long _pausedRemaining = 0;
  bool          _haveNextRecord = false;
  String        _pendingJson;
  String        _lastError;
};

extern SongPlayer songPlayer;
