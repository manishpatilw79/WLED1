#pragma once
/*
 * song_web.h
 * Registers the /song page and the /songs/* JSON endpoints on WLED's
 * existing AsyncWebServer instance: file upload, playback transport
 * (play/pause/resume/stop), delete, listing and FS usage. Playback itself
 * never touches this file - the web layer only starts/stops it and manages
 * .bin files on LittleFS.
 */
#include "wled.h"

void registerSongEndpoints(AsyncWebServer &server);

// Implemented in file.cpp: ensures /songs exists on LittleFS and registers
// the endpoints above on WLED's global 'server' instance. Called once from
// Song::begin().
void initSongUpload();
