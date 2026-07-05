#pragma once
/*
 * song_storage.h
 * LittleFS storage layer for the Song Controller usermod.
 * Handles /songs/ directory, upload, delete, listing and free space reporting.
 */
#include "wled.h"

#define SONG_DIR "/songs"
#define SONG_MAX_FILES 64
#define SONG_MAX_NAME_LEN 48

class SongStorage {
  public:
    bool begin() {
      if (!WLED_FS.exists(SONG_DIR)) {
        // LittleFS has no real directories, but WLED_FS.mkdir keeps things
        // consistent with other WLED FS usage (presets, ir, etc).
        WLED_FS.mkdir(SONG_DIR);
      }
      return true;
    }

    // Returns true and fills 'out' with up to SONG_MAX_FILES song filenames (no path, no extension check needed - all are .bin)
    uint16_t list(String out[], uint16_t maxCount) {
      uint16_t count = 0;
      #ifdef ARDUINO_ARCH_ESP32
      File dir = WLED_FS.open(SONG_DIR);
      if (!dir || !dir.isDirectory()) return 0;
      File f = dir.openNextFile();
      while (f && count < maxCount) {
        if (!f.isDirectory()) {
          String name = String(f.name());
          int slash = name.lastIndexOf('/');
          if (slash >= 0) name = name.substring(slash + 1);
          if (name.endsWith(".bin")) out[count++] = name;
        }
        f = dir.openNextFile();
      }
      #else
      Dir dir = WLED_FS.openDir(SONG_DIR);
      while (dir.next() && count < maxCount) {
        String name = dir.fileName();
        int slash = name.lastIndexOf('/');
        if (slash >= 0) name = name.substring(slash + 1);
        if (name.endsWith(".bin")) out[count++] = name;
      }
      #endif
      return count;
    }

    bool exists(const String& filename) {
      return WLED_FS.exists(path(filename));
    }

    bool remove(const String& filename) {
      if (!exists(filename)) return false;
      return WLED_FS.remove(path(filename));
    }

    File openRead(const String& filename) {
      return WLED_FS.open(path(filename), "r");
    }

    File openWrite(const String& filename) {
      return WLED_FS.open(path(filename), "w");
    }

    size_t usedBytes() {
      #ifdef ARDUINO_ARCH_ESP32
      return WLED_FS.usedBytes();
      #else
      FSInfo fs_info;
      WLED_FS.info(fs_info);
      return fs_info.usedBytes;
      #endif
    }

    size_t totalBytes() {
      #ifdef ARDUINO_ARCH_ESP32
      return WLED_FS.totalBytes();
      #else
      FSInfo fs_info;
      WLED_FS.info(fs_info);
      return fs_info.totalBytes;
      #endif
    }

    String path(const String& filename) {
      String p = String(SONG_DIR) + "/" + filename;
      return p;
    }

    // Only accept safe, .bin filenames
    static bool isValidFilename(const String& filename) {
      if (filename.length() == 0 || filename.length() > SONG_MAX_NAME_LEN) return false;
      if (!filename.endsWith(".bin")) return false;
      if (filename.indexOf('/') >= 0 || filename.indexOf("..") >= 0) return false;
      return true;
    }
};

extern SongStorage songStorage;
