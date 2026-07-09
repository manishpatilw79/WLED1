#include "song_web.h"
#include "song_storage.h"
#include "song_player.h"

// Minimal, WLED-styled settings-like page. Reuses WLED's existing
// /style.css and /common.js (same as the other settings sub-pages),
// no external libraries, no Bootstrap, no jQuery.
static const char SONG_PAGE_HTML[] PROGMEM = R"RAW(<!DOCTYPE html><html><head>
<meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>Song</title>
<link rel="stylesheet" href="/style.css">
<script src="/common.js"></script>
</head><body>
<div class="toprow">
<button type="button" onclick="window.location.href='/'">&lt; Back</button>
<h2>Song</h2>
</div>
<div class="helpB"></div>
<div id="fsline">FS : -- / -- KB</div>
<br>
<form id="uf">
<input type="file" id="ufile" accept=".bin">
<button type="button" onclick="doUpload()">Upload</button>
</form>
<br>
<input type="text" id="songSearch" placeholder="Search songs..." oninput="filterSongs()" style="width:100%;box-sizing:border-box">
<br><br>
<select id="songList" size="8" style="width:100%" onclick="onSongPick()" onchange="onSongPick()"></select>
<br><br>
<button type="button" onclick="doAction('play')">Play</button>
<button type="button" id="pauseBtn" onclick="doPauseResume()">Pause</button>
<button type="button" onclick="doAction('stop')">Stop</button>
<button type="button" onclick="doDelete()">Delete</button>
<div id="status"></div>
<script>
var curState = 'IDLE';
var allFiles = [];
var curFile = '';
var pickedFile = null; // sticks until user picks another or it disappears from the list
function sel(){var e=document.getElementById('songList');return e.options.length?e.options[e.selectedIndex].value:null;}
function onSongPick(){ var f=sel(); if(f) pickedFile=f; }
function renderSongList(){
  var q=(document.getElementById('songSearch').value||'').toLowerCase();
  var l=document.getElementById('songList'); l.innerHTML='';
  var wanted = pickedFile || curFile;
  allFiles.forEach(function(f){
    if(q && f.toLowerCase().indexOf(q)===-1) return;
    var o=document.createElement('option');o.value=f;o.textContent=f;
    if(f===wanted)o.selected=true;
    l.appendChild(o);
  });
}
function filterSongs(){ renderSongList(); }
function refresh(){
  fetch('/songs/list').then(r=>r.json()).then(d=>{
    allFiles = d.files;
    curFile = d.current || '';
    if (pickedFile && allFiles.indexOf(pickedFile) === -1) pickedFile = null; // deleted/renamed
    renderSongList();
    document.getElementById('fsline').textContent='FS : '+d.used+'/'+d.total+' KB';
    curState = d.state || 'IDLE';
    document.getElementById('pauseBtn').textContent = (curState==='PAUSED') ? 'Resume' : 'Pause';
  });
}
function doAction(a){
  var f=sel(); if(a==='play'&&!f){return;}
  if(f) pickedFile=f;
  var url='/songs/'+a+(f?('?file='+encodeURIComponent(f)):'');
  fetch(url).then(r=>r.json()).then(d=>{document.getElementById('status').textContent=d.msg||'';refresh();});
}
function doPauseResume(){
  var a = (curState==='PAUSED') ? 'resume' : 'pause';
  fetch('/songs/'+a).then(r=>r.json()).then(d=>{document.getElementById('status').textContent=d.msg||'';refresh();});
}
function doDelete(){
  var f=sel(); if(!f) return;
  if(!confirm('Delete '+f+'?')) return;
  fetch('/songs/delete?file='+encodeURIComponent(f)).then(r=>r.json()).then(d=>{document.getElementById('status').textContent=d.msg||'';refresh();});
}
function doUpload(){
  var fi=document.getElementById('ufile');
  if(!fi.files.length) return;
  var file=fi.files[0];
  if(!file.name.toLowerCase().endsWith('.bin')){document.getElementById('status').textContent='Only .bin files allowed';return;}
  var fd=new FormData(); fd.append('data', file, file.name);
  fetch('/songs/upload',{method:'POST',body:fd}).then(r=>r.json()).then(d=>{document.getElementById('status').textContent=d.msg||'';refresh();});
}
refresh();
setInterval(refresh, 2000);
</script>
</body></html>)RAW";

static void jsonMsg(AsyncWebServerRequest *request, int code, const String& msg, bool ok = true) {
  String out = "{\"ok\":";
  out += ok ? "true" : "false";
  out += ",\"msg\":\"" + msg + "\"}";
  request->send(code, "application/json", out);
}

static const char* stateStr() {
  switch (songPlayer.state()) {
    case SongState::PLAYING: return "PLAYING";
    case SongState::PAUSED:  return "PAUSED";
    default:                 return "IDLE";
  }
}

void registerSongEndpoints(AsyncWebServer &server) {

  server.on("/song", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send_P(200, "text/html", SONG_PAGE_HTML);
  });

  server.on("/songs/list", HTTP_GET, [](AsyncWebServerRequest *request){
    String files[SONG_MAX_FILES];
    uint16_t n = songStorage.list(files, SONG_MAX_FILES);
    String out = "{\"files\":[";
    for (uint16_t i = 0; i < n; i++) {
      if (i) out += ",";
      out += "\"" + files[i] + "\"";
    }
    out += "],\"current\":\"" + songPlayer.currentFile() + "\"" +
           ",\"state\":\"" + String(stateStr()) + "\"" +
           ",\"used\":" + String(songStorage.usedBytes() / 1024) +
           ",\"total\":" + String(songStorage.totalBytes() / 1024) + "}";
    request->send(200, "application/json", out);
  });

  server.on("/songs/storage", HTTP_GET, [](AsyncWebServerRequest *request){
    String out = "{\"used\":" + String(songStorage.usedBytes() / 1024) +
                 ",\"total\":" + String(songStorage.totalBytes() / 1024) + "}";
    request->send(200, "application/json", out);
  });

  server.on("/songs/play", HTTP_GET, [](AsyncWebServerRequest *request){
    if (!request->hasParam("file")) { jsonMsg(request, 400, "Missing file", false); return; }
    String f = request->getParam("file")->value();
    if (!SongStorage::isValidFilename(f)) { jsonMsg(request, 400, "Invalid filename", false); return; }
    bool ok = songPlayer.play(f);
    jsonMsg(request, ok ? 200 : 500, ok ? ("Playing " + f) : songPlayer.lastError(), ok);
  });

  server.on("/songs/pause", HTTP_GET, [](AsyncWebServerRequest *request){
    songPlayer.pause();
    jsonMsg(request, 200, "Paused");
  });

  server.on("/songs/resume", HTTP_GET, [](AsyncWebServerRequest *request){
    songPlayer.resume();
    jsonMsg(request, 200, "Resumed");
  });

  server.on("/songs/stop", HTTP_GET, [](AsyncWebServerRequest *request){
    songPlayer.stop();
    jsonMsg(request, 200, "Stopped");
  });

  server.on("/songs/delete", HTTP_GET, [](AsyncWebServerRequest *request){
    if (!request->hasParam("file")) { jsonMsg(request, 400, "Missing file", false); return; }
    String f = request->getParam("file")->value();
    if (!SongStorage::isValidFilename(f)) { jsonMsg(request, 400, "Invalid filename", false); return; }
    if (songPlayer.currentFile() == f) songPlayer.stop(); // unload before delete
    bool ok = songStorage.remove(f);
    jsonMsg(request, ok ? 200 : 500, ok ? ("Deleted " + f) : "Delete failed", ok);
  });

  // Upload handler: body handler streams multipart data straight to LittleFS
  server.on("/songs/upload", HTTP_POST,
    [](AsyncWebServerRequest *request){
      jsonMsg(request, 200, "Upload complete");
    },
    [](AsyncWebServerRequest *request, String filename, size_t index, uint8_t *data, size_t len, bool final){
      static File uploadFile;
      if (!index) {
        if (!SongStorage::isValidFilename(filename)) {
          request->_tempObject = (void*)"reject";
          return;
        }
        uploadFile = songStorage.openWrite(filename);
      }
      if (uploadFile && request->_tempObject == nullptr) {
        uploadFile.write(data, len);
      }
      if (final && uploadFile) {
        uploadFile.close();
      }
    }
  );
}
