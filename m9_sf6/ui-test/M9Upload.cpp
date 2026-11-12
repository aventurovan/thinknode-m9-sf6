#include "M9Upload.h"
#include <WiFi.h>
#include <WebServer.h>
#include <SD.h>

#define DIR      "/mapa"
#define WBUF_SZ  32768

static WebServer* srv = NULL;
static File up_file;
static bool up_ok = false;
static char up_name[40] = "";
static uint32_t up_bytes = 0, up_speed = 0, up_chunk_bytes = 0;
static unsigned long up_chunk_ms = 0;
static uint8_t* wbuf = NULL;
static uint32_t wlen = 0;
static bool changed = false;

static const char PAGE[] PROGMEM = R"HTML(<!doctype html><html lang="pl"><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>M9 — mapa</title>
<style>body{font:16px system-ui,sans-serif;background:#0b0f14;color:#f2f5f7;max-width:640px;margin:24px auto;padding:0 16px}
h1{font-size:22px}button,label.b{background:#ffb020;color:#0b0f14;border:0;border-radius:6px;padding:10px 16px;font:inherit;font-weight:700;cursor:pointer;display:inline-block}
button.s{background:#243040;color:#f2f5f7;padding:4px 10px;font-weight:400}table{width:100%;border-collapse:collapse;margin:12px 0}
td{padding:6px 4px;border-bottom:1px solid #243040}td.r{text-align:right;color:#9aa7b4}#st{margin:14px 0;min-height:48px}progress{width:100%}</style>
<h1>M9 — mapa na karcie SD</h1>
<p>Wybierz pliki <b>.mcm</b>. Trafią do folderu /mapa. Nie zamykaj strony i nie wychodź ze strony „Wgrywanie” na urządzeniu, dopóki trwa wgrywanie. Przerwane wgrywanie można wznowić, wybierając ten sam plik.</p>
<label class="b">Wybierz pliki<input id="f" type="file" multiple accept=".mcm" hidden></label>
<div id="st"></div><table id="t"></table>
<script>
const CH=2*1024*1024,$=i=>document.getElementById(i),mb=b=>(b/1048576).toFixed(1)+' MB',wait=m=>new Promise(r=>setTimeout(r,m));
async function size(n){return +(await (await fetch('/size?name='+encodeURIComponent(n))).text())}
async function list(){const l=await (await fetch('/list')).json();$('t').innerHTML=l.length?l.map(x=>'<tr><td>'+x.n+'</td><td class="r">'+mb(x.s)+'</td><td class="r"><button class="s" onclick="del(\''+x.n+'\')">Usuń</button></td></tr>').join(''):'<tr><td>Na karcie nie ma jeszcze plików mapy.</td></tr>'}
async function del(n){if(confirm('Usunąć '+n+'?')){await fetch('/del?name='+encodeURIComponent(n),{method:'POST'});list()}}
async function up(file,i,n){
 if(!/^[A-Za-z0-9._-]{5,30}$/.test(file.name)||!file.name.endsWith('.mcm'))throw 'Zła nazwa pliku: '+file.name;
 let off=await size(file.name),bad=0,t0=Date.now(),o0=off;
 if(off>file.size)off=0;
 while(off<file.size){
  const fd=new FormData();fd.append('f',file.slice(off,off+CH),file.name);let ok=false;
  try{ok=(await fetch('/upload?name='+encodeURIComponent(file.name)+'&off='+off,{method:'POST',body:fd})).ok}catch(e){}
  if(ok){off=Math.min(off+CH,file.size);bad=0}else{if(++bad>8)throw 'Przerwane: '+file.name+' (sprawdź WiFi i spróbuj ponownie — wgrywanie zostanie wznowione)';await wait(2000);try{off=await size(file.name)}catch(e){}}
  const v=(off-o0)/((Date.now()-t0)/1000);
  $('st').innerHTML='Plik '+i+' z '+n+': <b>'+file.name+'</b> — '+mb(off)+' z '+mb(file.size)+', '+(v/1024).toFixed(0)+' kB/s, zostało ok. '+Math.ceil((file.size-off)/Math.max(v,1)/60)+' min<br><progress max="'+file.size+'" value="'+off+'"></progress>';
 }
 if(!(await fetch('/done?name='+encodeURIComponent(file.name)+'&size='+file.size,{method:'POST'})).ok)throw 'Plik '+file.name+' nie zgadza się po wgraniu — usuń go z listy i wgraj ponownie';
}
$('f').onchange=async e=>{const fs=[...e.target.files];try{for(let i=0;i<fs.length;i++){await up(fs[i],i+1,fs.length);await list()}$('st').innerHTML='<b>Gotowe.</b> Wgrano plików: '+fs.length+'.'}catch(x){$('st').innerHTML='<b>Błąd.</b> '+x}e.target.value='';list()};
list();
</script></html>)HTML";

// nazwa pliku: tylko litery, cyfry, kropka, podkreślenie i myślnik; opcjonalnie z końcówką .part
static bool safeName(const String& n, char* out, size_t sz) {
  size_t len = n.length();
  if (len < 5 || len >= sz || len > 35) return false;
  for (size_t i = 0; i < len; i++) {
    char c = n[i];
    if (!(isalnum((unsigned char)c) || c == '.' || c == '_' || c == '-')) return false;
  }
  if (!(n.endsWith(".mcm") || n.endsWith(".mcm.part"))) return false;
  strcpy(out, n.c_str());
  return true;
}

static bool flushBuf() {
  if (wlen == 0) return true;
  bool ok = up_file && up_file.write(wbuf, wlen) == wlen;
  if (ok) up_bytes += wlen;
  wlen = 0;
  return ok;
}

static void onUpload() {
  HTTPUpload& u = srv->upload();
  if (u.status == UPLOAD_FILE_START) {
    up_ok = false;
    wlen = 0;
    if (up_file) up_file.close();
    char name[40];
    if (!safeName(srv->arg("name"), name, sizeof(name)) || strstr(name, ".part")) return;
    uint32_t off = strtoul(srv->arg("off").c_str(), NULL, 10);
    char path[64];
    snprintf(path, sizeof(path), DIR "/%s.part", name);
    if (!SD.exists(DIR)) SD.mkdir(DIR);
    if (off == 0) {
      up_file = SD.open(path, FILE_WRITE);
    } else {
      up_file = SD.open(path, FILE_APPEND);
      if (up_file && up_file.size() != off) up_file.close();     // przesunięcie się nie zgadza: strona zapyta o rozmiar i wznowi
    }
    up_ok = (bool)up_file;
    strcpy(up_name, name);
    up_bytes = off;
    up_chunk_bytes = 0;
    up_chunk_ms = millis();
  } else if (u.status == UPLOAD_FILE_WRITE) {
    if (!up_ok) return;
    size_t done = 0;
    while (done < u.currentSize && up_ok) {
      size_t n = u.currentSize - done;
      if (n > WBUF_SZ - wlen) n = WBUF_SZ - wlen;
      memcpy(wbuf + wlen, u.buf + done, n);
      wlen += n; done += n;
      if (wlen == WBUF_SZ) up_ok = flushBuf();
    }
    up_chunk_bytes += u.currentSize;
  } else if (u.status == UPLOAD_FILE_END) {
    if (up_ok) up_ok = flushBuf();
    if (up_file) up_file.close();
    unsigned long dt = millis() - up_chunk_ms;
    if (dt > 0) up_speed = (uint32_t)((uint64_t)up_chunk_bytes * 1000 / 1024 / dt);
  } else if (u.status == UPLOAD_FILE_ABORTED) {
    if (up_file) up_file.close();
    up_ok = false;
  }
}

static void onUploadDone() {
  if (up_ok) srv->send(200, "text/plain", "OK");
  else srv->send(409, "text/plain", "BLAD");
}

static void onSize() {
  char name[40], path[64];
  uint32_t s = 0;
  if (safeName(srv->arg("name"), name, sizeof(name))) {
    snprintf(path, sizeof(path), DIR "/%s.part", name);
    File f = SD.open(path);
    if (f) { s = f.size(); f.close(); }
  }
  srv->send(200, "text/plain", String(s));
}

static void onDone() {
  char name[40], part[64], path[64];
  if (!safeName(srv->arg("name"), name, sizeof(name))) { srv->send(400, "text/plain", "BLAD"); return; }
  snprintf(part, sizeof(part), DIR "/%s.part", name);
  snprintf(path, sizeof(path), DIR "/%s", name);
  File f = SD.open(part);
  uint32_t s = f ? f.size() : 0;
  if (f) f.close();
  if (s == 0 || s != strtoul(srv->arg("size").c_str(), NULL, 10)) { srv->send(409, "text/plain", "BLAD"); return; }
  if (SD.exists(path)) SD.remove(path);
  bool ok = SD.rename(part, path);
  changed = true;
  srv->send(ok ? 200 : 500, "text/plain", ok ? "OK" : "BLAD");
}

static void onList() {
  String out = "[";
  File dir = SD.open(DIR);
  if (dir && dir.isDirectory()) {
    for (File f = dir.openNextFile(); f; f = dir.openNextFile()) {
      const char* name = f.name();
      const char* slash = strrchr(name, '/');
      if (slash) name = slash + 1;
      if (!f.isDirectory()) {
        if (out.length() > 1) out += ",";
        out += "{\"n\":\""; out += name; out += "\",\"s\":"; out += String((uint32_t)f.size()); out += "}";
      }
      f.close();
    }
    dir.close();
  }
  out += "]";
  srv->send(200, "application/json", out);
}

static void onDel() {
  char name[40], path[64];
  if (!safeName(srv->arg("name"), name, sizeof(name))) { srv->send(400, "text/plain", "BLAD"); return; }
  snprintf(path, sizeof(path), DIR "/%s", name);
  bool ok = SD.remove(path);
  changed = true;
  srv->send(ok ? 200 : 404, "text/plain", ok ? "OK" : "BLAD");
}

bool m9uploadStart() {
  if (srv) return true;
  if (wbuf == NULL) wbuf = (uint8_t*)ps_malloc(WBUF_SZ);
  if (wbuf == NULL) wbuf = (uint8_t*)malloc(WBUF_SZ);
  if (wbuf == NULL) return false;
  WiFi.mode(WIFI_AP);
  if (!WiFi.softAP(M9UP_SSID, M9UP_PASS)) { WiFi.mode(WIFI_OFF); return false; }
  srv = new WebServer(80);
  srv->on("/", HTTP_GET, []() { srv->send_P(200, "text/html", PAGE); });
  srv->on("/list", HTTP_GET, onList);
  srv->on("/size", HTTP_GET, onSize);
  srv->on("/upload", HTTP_POST, onUploadDone, onUpload);
  srv->on("/done", HTTP_POST, onDone);
  srv->on("/del", HTTP_POST, onDel);
  srv->begin();
  return true;
}

void m9uploadStop() {
  if (!srv) return;
  if (up_file) up_file.close();
  srv->stop();
  delete srv;
  srv = NULL;
  WiFi.softAPdisconnect(true);
  WiFi.mode(WIFI_OFF);
}

void m9uploadLoop() { if (srv) srv->handleClient(); }
bool m9uploadActive() { return srv != NULL; }
int m9uploadClients() { return srv ? WiFi.softAPgetStationNum() : 0; }
const char* m9uploadFile() { return up_name; }
uint32_t m9uploadBytes() { return up_bytes; }
uint32_t m9uploadSpeed() { return up_speed; }
bool m9uploadChanged() { bool c = changed; changed = false; return c; }
