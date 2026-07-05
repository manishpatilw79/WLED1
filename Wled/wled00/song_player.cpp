#include "wled.h"
#include "song_player.h"

SongPlayer songPlayer;

// Color table for SONG_CMD_COLOR (the "C" TXT command). Index 0 is unused
// (values start at 1) so the table can be indexed directly by `value`.
// {r, g, b, w} - identical to the palette in the web BIN generator.
static const uint8_t SONG_COLOR_TABLE[13][4] = {
  {   0,   0,   0,   0 },  // 0  - unused
  { 255,   0,   0,   0 },  // 1  - red
  {   0, 255,   0,   0 },  // 2  - green
  {   0,   0, 255,   0 },  // 3  - blue
  { 255, 255, 255,   0 },  // 4  - white
  { 255, 220,   0,   0 },  // 5  - yellow
  { 255,   0, 255,   0 },  // 6  - purple
  { 255, 160,   0,   0 },  // 7  - orange
  {   0, 255, 247,   0 },  // 8  - cyan
  { 255,  20, 147,   0 },  // 9  - pink
  { 128,   0, 255,   0 },  // 10 - violet
  { 245, 154, 154,   0 },  // 11 - peacock
  { 255, 180,  80,   0 }   // 12 - warm white
};

void SongPlayer::begin(JsonHandler handler)
{
  _handler = handler;
  _state = SongState::IDLE;
}

bool SongPlayer::play(const String& filename)
{
  stop();

  if (!SongStorage::isValidFilename(filename) || !songStorage.exists(filename)) {
    _lastError = F("Invalid or missing file");
    return false;
  }

  _file = songStorage.openRead(filename);

  if (!_file) {
    _lastError = F("Failed to open file");
    return false;
  }

  _filename = filename;
  _state = SongState::PLAYING;
  _nextEventAt = millis();
  _haveNextRecord = false;
  _pendingJson = "";
  _lastError = "";
  return true;
}

void SongPlayer::pause()
{
  if (_state == SongState::PLAYING) {
    _state = SongState::PAUSED;
    long remaining = (long)(_nextEventAt - millis());
    _pausedRemaining = (remaining > 0) ? (unsigned long)remaining : 0;
  }
}

void SongPlayer::resume()
{
  if (_state == SongState::PAUSED) {
    _state = SongState::PLAYING;
    _nextEventAt = millis() + _pausedRemaining;
  }
}

void SongPlayer::stop()
{
  if (_file) _file.close();

  _state = SongState::IDLE;
  _filename = "";
  _haveNextRecord = false;
  _pendingJson = "";
}

void SongPlayer::tick()
{
  if (_state != SongState::PLAYING) return;

  if (!_haveNextRecord) {
    if (!readNextRecord()) {
      stop();
      return;
    }
  }

  if ((long)(millis() - _nextEventAt) >= 0) {
    if (_handler) _handler(_pendingJson);

    _pendingJson = "";
    _haveNextRecord = false;
  }
}

// ---------------------------------------------------------------------------
// buildJson()
// Reconstructs the exact JSON packet that the OLD format used to store
// verbatim, from a compact (cmdId, value) pair. This is the only place that
// needs to change if a new command is added.
// ---------------------------------------------------------------------------
bool SongPlayer::buildJson(uint8_t cmdId, uint16_t value, char* out, size_t outLen)
{
  switch (cmdId) {

    case SONG_CMD_ON:
      snprintf(out, outLen, "{\"on\":true}");
      return true;

    case SONG_CMD_OFF:
      snprintf(out, outLen, "{\"on\":false}");
      return true;

    case SONG_CMD_TOGGLE:
      snprintf(out, outLen, "{\"on\":\"t\"}");
      return true;

    case SONG_CMD_EFFECT:
      snprintf(out, outLen, "{\"seg\":[{\"fx\":%u}]}", (unsigned)value);
      return true;

    case SONG_CMD_BRIGHTNESS: {
      // `value` is stored as a 0-100 percentage; convert to 0-255 exactly
      // like Arduino's map(value, 0, 100, 0, 255).
      long bri = map((long)value, 0, 100, 0, 255);
      snprintf(out, outLen, "{\"bri\":%ld}", bri);
      return true;
    }

    case SONG_CMD_SPEED:
      snprintf(out, outLen, "{\"seg\":[{\"sx\":%u}]}", (unsigned)value);
      return true;

    case SONG_CMD_INTENSITY:
      snprintf(out, outLen, "{\"seg\":[{\"ix\":%u}]}", (unsigned)value);
      return true;

    case SONG_CMD_PALETTE:
      snprintf(out, outLen, "{\"seg\":[{\"pal\":%u}]}", (unsigned)value);
      return true;

    case SONG_CMD_PRESET:
      snprintf(out, outLen, "{\"ps\":%u}", (unsigned)value);
      return true;

    case SONG_CMD_COLOR: {
      if (value < 1 || value > 12) return false;
      const uint8_t* c = SONG_COLOR_TABLE[value];
      snprintf(out, outLen, "{\"seg\":[{\"col\":[[%u,%u,%u,%u]]}]}",
                c[0], c[1], c[2], c[3]);
      return true;
    }

    default:
      return false; // unknown command - caller skips this record
  }
}

bool SongPlayer::readNextRecord()
{
  if (!_file || !_file.available()) return false;

  uint8_t rec[SONG_RECORD_SIZE];

  if (_file.read(rec, SONG_RECORD_SIZE) != SONG_RECORD_SIZE)
    return false;

  uint32_t delayMs =
      (uint32_t)rec[0] |
      ((uint32_t)rec[1] << 8) |
      ((uint32_t)rec[2] << 16) |
      ((uint32_t)rec[3] << 24);

  uint8_t cmdId = rec[4];

  uint16_t value =
      (uint16_t)rec[5] |
      ((uint16_t)rec[6] << 8);

  char buf[SONG_MAX_JSON_LEN];

  if (!buildJson(cmdId, value, buf, sizeof(buf))) {
    _lastError = F("Unknown command ID in record");
    return false;
  }

  _pendingJson = String(buf);

  _nextEventAt = millis() + delayMs;

  _haveNextRecord = true;

  return true;
}
