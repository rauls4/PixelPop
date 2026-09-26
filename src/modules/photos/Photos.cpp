#include "Photos.h"
#include "../../core/Display.h"
#include "../../core/WebUI.h"
#include "../../core/App.h"

static const char *const FIT_NAMES[3] = {
  "Scale to fit (stretch to the screen size, proportions may change)",
  "Crop (fill the screen, trim what does not fit)",
  "Black bars (show the whole picture, keep proportions)"
};

// ---------- storage ----------
String PhotoModule::path(int slot, bool temp) const {
  return String("/photo") + slot + (temp ? ".tmp" : ".bin");
}

void PhotoModule::scan() {
  _count = 0;
  for (int i = 0; i < MAX_PHOTOS; i++) {
    _has[i] = _fsOk && LittleFS.exists(path(i, false).c_str());
    if (_has[i]) _count++;
  }
  invalidate();
}

int PhotoModule::slotOfPage(int sub) const {
  int n = 0;
  for (int i = 0; i < MAX_PHOTOS; i++) {
    if (!_has[i]) continue;
    if (n == sub) return i;
    n++;
  }
  return -1;
}

void PhotoModule::begin() {
  _fsOk = LittleFS.begin(true);                    // formats the storage the first time
  if (!_fsOk) Serial.println("Photos: no storage partition");
  for (int i = 0; i < MAX_PHOTOS; i++) LittleFS.remove(path(i, true).c_str());     // leftovers of a broken upload
  scan();
}

// ---------- settings ----------
void PhotoModule::onLoad() {
  _fit = store.getU8("fit", FIT_BARS);       if (_fit > 2) _fit = FIT_BARS;
  _bright = store.getU8("bri", 100);         if (_bright < 10) _bright = 10; if (_bright > 100) _bright = 100;
}

String PhotoModule::onSettingsHtml() {
  String h;
  h += uiSection("How pictures fit the screen");
  h += uiSelect("fit", "Fit", FIT_NAMES, 3, _fit);
  h += "<p class='m'>The panel is twice as wide as it is tall, so most photos do not match it exactly. "
       "Choose whether to stretch them, crop them, or keep the whole picture with black bars. "
       "This applies to every photo, and you can change it whenever you like.</p>";
  h += uiNumber("bri", "Photo brightness (%)", _bright, 10, 100);
  return h;
}

void PhotoModule::onSave(WebServer &server) {
  _fit = (uint8_t)uiReadLong(server, "fit", _fit, 0, 2);
  _bright = (uint8_t)uiReadLong(server, "bri", _bright, 10, 100);
  store.putU8("fit", _fit);
  store.putU8("bri", _bright);
  invalidate();
}

String PhotoModule::summary() {
  if (!_fsOk) return "Photo storage is not available";
  if (_count == 0) return "No photos yet - add some below";
  static const char *const short_names[3] = {"scaled to fit", "cropped", "black bars"};
  return String(_count) + (_count == 1 ? " photo, " : " photos, ") + short_names[_fit];
}

// The photo list and uploader sit under the settings form (a form cannot contain another form).
String PhotoModule::actionsHtml() {
  String h = "<h3 class='first'>Your photos</h3>";
  if (!_fsOk) {
    h += "<p class='m'>The board has no storage area for photos. In the Arduino IDE choose Tools &gt; Partition Scheme "
         "and pick one that includes storage, for example \"Huge APP (3MB No OTA/1MB SPIFFS)\", then upload again.</p>";
    return h;
  }
  size_t total = LittleFS.totalBytes(), used = LittleFS.usedBytes();
  h += "<p class='m'>" + String(_count) + " of " + String(MAX_PHOTOS) + " photos &middot; " +
       String((unsigned long)((total > used ? total - used : 0) / 1024)) + " KB free</p>";
  for (int i = 0; i < MAX_PHOTOS; i++) {
    if (!_has[i]) continue;
    h += "<div class='row'><canvas data-i='" + String(i) + "' width='64' height='32' "
         "style='width:96px;height:auto;background:#000;border-radius:4px;image-rendering:pixelated'></canvas>"
         "<div class='rt'>Photo " + String(i + 1) + "</div>"
         "<form method='POST' action='/photos/delete' onsubmit=\"return confirm('Delete this photo?')\">"
         "<input type='hidden' name='i' value='" + String(i) + "'>"
         "<button class='sec' type='submit' style='width:auto;margin:0;padding:8px 14px'>Delete</button></form></div>";
  }
  if (_count < MAX_PHOTOS) {
    h += "<label>Add photos</label><input type='file' id='pf' accept='image/*' multiple onchange='addPhotos(this)'>"
         "<p class='m' id='pst'>Pick one or more pictures. They are shrunk in your browser before they are sent.</p>";
  } else {
    h += "<p class='m'>The board is holding the most photos it can. Delete one to add another.</p>";
  }
  h += R"JS(<script>
var MAXS=128;
function thumbs(){
  document.querySelectorAll('canvas[data-i]').forEach(function(c){
    fetch('/photos/raw?i='+c.dataset.i).then(function(r){return r.arrayBuffer();}).then(function(b){
      var v=new DataView(b),w=v.getUint16(0,true),h=v.getUint16(2,true);
      c.width=w;c.height=h;var x=c.getContext('2d'),im=x.createImageData(w,h);
      for(var p=0;p<w*h;p++){var s=v.getUint16(4+2*p,true);
        im.data[4*p]=Math.round(((s>>11)&31)*255/31);im.data[4*p+1]=Math.round(((s>>5)&63)*255/63);
        im.data[4*p+2]=Math.round((s&31)*255/31);im.data[4*p+3]=255;}
      x.putImageData(im,0,0);
    });
  });
}
async function addPhotos(inp){
  var st=document.getElementById('pst'),files=Array.prototype.slice.call(inp.files),done=0;
  for(var f of files){
    st.textContent='Sending '+(done+1)+' of '+files.length+'...';
    try{
      var bmp=await createImageBitmap(f,{imageOrientation:'from-image'});
      var sc=Math.min(1,MAXS/Math.max(bmp.width,bmp.height));
      var w=Math.max(1,Math.round(bmp.width*sc)),h=Math.max(1,Math.round(bmp.height*sc));
      var cv=document.createElement('canvas');cv.width=w;cv.height=h;
      var cx=cv.getContext('2d');cx.fillStyle='#000';cx.fillRect(0,0,w,h);
      cx.imageSmoothingEnabled=true;cx.imageSmoothingQuality='high';cx.drawImage(bmp,0,0,w,h);
      var d=cx.getImageData(0,0,w,h).data,out=new Uint8Array(4+w*h*2);
      out[0]=w&255;out[1]=w>>8;out[2]=h&255;out[3]=h>>8;
      for(var p=0;p<w*h;p++){var v=((d[4*p]>>3)<<11)|((d[4*p+1]>>2)<<5)|(d[4*p+2]>>3);out[4+2*p]=v&255;out[5+2*p]=v>>8;}
      var fd=new FormData();fd.append('photo',new Blob([out]),'photo.bin');
      var r=await fetch('/photos/upload',{method:'POST',body:fd});
      if(!r.ok){st.textContent=await r.text();return;}
      done++;
    }catch(e){st.textContent='Could not read '+f.name+' ('+e+')';return;}
  }
  location.reload();
}
thumbs();
</script>)JS";
  return h;
}

// ---------- web routes ----------
void PhotoModule::registerRoutes(WebServer &server) {
  server.on("/photos/upload", HTTP_POST,
    [this, &server]() {
      if (_upResult) server.send(200, "text/plain", "ok");
      else server.send(400, "text/plain", "That picture could not be stored (the board is full, or the picture is not readable).");
    },
    [this, &server]() { onUpload(server); });

  server.on("/photos/delete", HTTP_POST, [this, &server]() {
    int i = (int)server.arg("i").toInt();
    if (i >= 0 && i < MAX_PHOTOS && _has[i]) LittleFS.remove(path(i, false).c_str());
    scan();
    app.requestRedraw();
    webRedirect("/photos");
  });

  server.on("/photos/raw", HTTP_GET, [this, &server]() {
    int i = (int)server.arg("i").toInt();
    if (i < 0 || i >= MAX_PHOTOS || !_has[i]) { server.send(404, "text/plain", "No such photo."); return; }
    File f = LittleFS.open(path(i, false).c_str(), "r");
    if (!f) { server.send(404, "text/plain", "No such photo."); return; }
    server.streamFile(f, "application/octet-stream");
    f.close();
  });
}

// Called for every piece of an uploaded file. The file is 4 header bytes (width and height,
// 16 bits each) followed by width * height RGB565 pixels.
void PhotoModule::onUpload(WebServer &server) {
  HTTPUpload &u = server.upload();
  if (u.status == UPLOAD_FILE_START) {
    _upOk = false; _upResult = false; _upGot = 0; _upSize = 0; _upSlot = -1;
    if (!_fsOk) return;
    for (int i = 0; i < MAX_PHOTOS; i++) if (!_has[i]) { _upSlot = i; break; }
    if (_upSlot < 0) return;
    size_t total = LittleFS.totalBytes(), used = LittleFS.usedBytes();
    if (total < used + 4 + 2 * PHOTO_MAX_SIDE * PHOTO_MAX_SIDE + 8192) return;         // not enough room for the biggest picture
    _upFile = LittleFS.open(path(_upSlot, true).c_str(), "w");
    _upOk = (bool)_upFile;
  } else if (u.status == UPLOAD_FILE_WRITE) {
    if (!_upOk) return;
    for (size_t i = 0; i < u.currentSize && _upGot < 4; i++) _upHdr[_upGot++] = u.buf[i];
    _upSize += u.currentSize;
    if (_upSize > 4 + 2 * PHOTO_MAX_SIDE * PHOTO_MAX_SIDE) { _upOk = false; return; }   // too big
    if (_upFile.write(u.buf, u.currentSize) != u.currentSize) _upOk = false;
  } else if (u.status == UPLOAD_FILE_END) {
    if (_upFile) _upFile.close();
    bool good = false;
    if (_upOk && _upGot >= 4) {
      int w = _upHdr[0] | (_upHdr[1] << 8), h = _upHdr[2] | (_upHdr[3] << 8);
      good = w >= 1 && w <= PHOTO_MAX_SIDE && h >= 1 && h <= PHOTO_MAX_SIDE && _upSize == (uint32_t)(4 + 2 * w * h);
    }
    if (good) {
      LittleFS.remove(path(_upSlot, false).c_str());
      good = LittleFS.rename(path(_upSlot, true).c_str(), path(_upSlot, false).c_str());
    }
    if (!good && _upSlot >= 0) LittleFS.remove(path(_upSlot, true).c_str());
    _upResult = good;
    if (good) { _has[_upSlot] = true; _count++; invalidate(); app.requestRedraw(); }
  } else if (u.status == UPLOAD_FILE_ABORTED) {
    if (_upFile) _upFile.close();
    if (_upSlot >= 0) LittleFS.remove(path(_upSlot, true).c_str());
    _upResult = false;
  }
}

// ---------- drawing ----------
// Reads the stored picture and fits it to a w x h screen into _frame.
bool PhotoModule::loadFrame(int slot, int w, int h) {
  File f = LittleFS.open(path(slot, false).c_str(), "r");
  if (!f) return false;
  uint8_t hdr[4];
  if (f.read(hdr, 4) != 4) { f.close(); return false; }
  int sw = hdr[0] | (hdr[1] << 8), sh = hdr[2] | (hdr[3] << 8);
  if (sw < 1 || sw > PHOTO_MAX_SIDE || sh < 1 || sh > PHOTO_MAX_SIDE) { f.close(); return false; }
  size_t bytes = (size_t)sw * sh * 2;
  uint16_t *src = (uint16_t *)malloc(bytes);
  if (!src) { f.close(); return false; }
  bool ok = f.read((uint8_t *)src, bytes) == bytes;
  f.close();
  if (ok) {
    photoRender(src, sw, sh, _frame, w, h, _fit, _bright);
    _cacheSlot = slot; _cacheW = w; _cacheH = h; _cacheFit = _fit; _cacheBright = _bright;
  }
  free(src);
  return ok;
}

void PhotoModule::drawPage(int sub) {
  gDisplay->fillScreen(0);
  const int W = gDisplay->width(), H = gDisplay->height();
  int slot = slotOfPage(sub);
  if (slot >= 0) {
    bool cached = (_cacheSlot == slot && _cacheW == W && _cacheH == H && _cacheFit == _fit && _cacheBright == _bright);
    if (cached || loadFrame(slot, W, H)) {
      for (int y = 0; y < H; y++)
        for (int x = 0; x < W; x++) {
          uint16_t c = _frame[y * W + x];
          if (c) gDisplay->drawPixel(x, y, c);
        }
    } else {
      gDisplay->setTextSize(1);
      gDisplay->setTextColor(COL_ORANGE);
      gDisplay->setCursor(2, 12);
      gDisplay->print("Photo error");
    }
  }
  gDisplay->flipDMABuffer();
}
