#include "song.h"

Song song;

void Song::begin() {
  songPlayer.begin(&songApplyJson);
  initSongUpload(); // file.cpp: ensures /songs exists, registers /song UI + /songs/* endpoints
}

void Song::tick() {
  songPlayer.tick();
}

// ---------------------------------------------------------------------------
// songApplyJson()
// Parses one JSON packet decoded from the song file and applies it directly
// through WLED's internal JSON state parser (deserializeState()) - the exact
// same function WLED's own /json/state HTTP API uses - with no UART, no
// Serial and no network hop involved.
// ---------------------------------------------------------------------------
void songApplyJson(const String& json) {
  if (json.length() == 0) return;

  // Small dynamic buffer sized to the incoming record; released as soon as
  // this function returns, so no more than one record's JSON is ever
  // resident in RAM at a time.
  size_t capacity = json.length() * 3 + 256;
  if (capacity > 4096) capacity = 4096; // hard ceiling, keeps RAM bounded on ESP8266
  DynamicJsonDocument doc(capacity);

  DeserializationError err = deserializeJson(doc, json);
  if (err) return; // corrupt record - skip it, keep playback running

  JsonObject root = doc.as<JsonObject>();
  if (root.isNull()) return;

  deserializeState(root, CALL_MODE_DIRECT_CHANGE);
}
