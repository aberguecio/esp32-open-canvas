#include <GxEPD2_7C.h>
#include <Fonts/FreeMonoBold9pt7b.h>
#include <SPI.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <SPIFFS.h>

#define USE_HSPI_FOR_EPD
#define GxEPD2_DISPLAY_CLASS GxEPD2_7C
#define GxEPD2_DRIVER_CLASS  GxEPD2_730c_GDEY073D46

// ---- Wi‑Fi credentials ----
const char* WIFI_SSID = "RED-BERGUECIO-2.4G";
const char* WIFI_PASS = "Colchagua81";
const char* API_URL   = "https://canvas.berguecio.cl/api/v1/images";
//--------------------------------------------------

// Waveshare ESP32‑Driver pins
constexpr uint8_t PIN_CS   = 15;
constexpr uint8_t PIN_DC   = 27;
constexpr uint8_t PIN_RST  = 26;
constexpr uint8_t PIN_BUSY = 25;

#if defined(ESP32)
  #define MAX_DISPLAY_BUFFER_SIZE 65536ul
  #define MAX_HEIGHT(EPD) ((EPD::HEIGHT <= (MAX_DISPLAY_BUFFER_SIZE)/(EPD::WIDTH/2)) ? EPD::HEIGHT : ((MAX_DISPLAY_BUFFER_SIZE)/(EPD::WIDTH/2)))
  SPIClass hspi(HSPI); // CLK=13, MISO=12, MOSI=14
  GxEPD2_DISPLAY_CLASS< GxEPD2_DRIVER_CLASS,
                        MAX_HEIGHT(GxEPD2_DRIVER_CLASS) > display(
      GxEPD2_DRIVER_CLASS(PIN_CS, PIN_DC, PIN_RST, PIN_BUSY));
#endif

//--------------------------------------------------
// Helper functions
//--------------------------------------------------
void centerText(const char* txt){
  display.setFont(&FreeMonoBold9pt7b);
  display.setTextColor(GxEPD_BLACK);
  int16_t bx,by; uint16_t bw,bh;
  display.getTextBounds(txt,0,0,&bx,&by,&bw,&bh);
  display.setCursor(((display.width()-bw)/2)-bx, ((display.height()-bh)/2)-by);
  display.print(txt);
}

void showMultiline(const String& txt){
  display.setRotation(0);
  display.setFullWindow();
  display.firstPage();
  do{
    display.fillScreen(GxEPD_WHITE);
    display.setFont(&FreeMonoBold9pt7b);
    display.setTextColor(GxEPD_BLACK);
    uint16_t lineH = FreeMonoBold9pt7b.yAdvance;
    int16_t y = lineH;
    for(size_t i=0;i<txt.length();){
      String line = txt.substring(i, min(i+60, txt.length()));
      display.setCursor(0,y);
      display.print(line);
      y += lineH;
      i += 60;
    }
  }while(display.nextPage());
}

String fetchImageURL(){
  HTTPClient http;
  http.begin(API_URL);
  int code = http.GET();
  if(code!=200){ http.end(); return String("HTTP ")+code; }
  String body = http.getString();
  http.end();
  int idx = body.indexOf("\"url\"");
  if(idx<0) return F("url not found");
  idx = body.indexOf('"', idx+5);
  if(idx<0) return F("parse err");
  int end = body.indexOf('"', idx+1);
  if(end<0) return F("parse err");
  return body.substring(idx+1,end);
}

bool downloadToSpiffs(const String& url){
  WiFiClientSecure *client = new WiFiClientSecure;
  client->setInsecure();                     // skip TLS cert validation for S3 signed URL
  HTTPClient http;
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  http.begin(*client, url);

  int code = http.GET();
  Serial.printf("HTTP code %d", code);
  if(code != 200){ http.end(); delete client; return false; }

  int len = http.getSize();
  if(len == 0){ Serial.println("empty body"); http.end(); delete client; return false; }

  File f = SPIFFS.open("/img", FILE_WRITE);
  if(!f){ Serial.println("SPIFFS open fail"); http.end(); delete client; return false; }

  uint8_t buf[1024]; size_t total = 0;
  WiFiClient *stream = http.getStreamPtr();
  while(http.connected() && (len > 0 || len == -1)){
    size_t avail = stream->available();
    if(!avail){ delay(1); continue; }
    int r = stream->readBytes(buf, min(avail, sizeof(buf)));
    if(r <= 0) break;
    f.write(buf, r);
    total += r;
    if(len > 0) len -= r;
  }
  f.close(); http.end(); delete client;
  Serial.printf("Downloaded %u bytes", total);
  return total > 0;
}

//--------------------------------------------------
// Render BMP (24‑bpp, 800×480) saved at /img
//--------------------------------------------------
void showBMP(){
  File f = SPIFFS.open("/img", FILE_READ);
  if(!f){ showMultiline("/img open fail"); return; }

  uint8_t b0 = f.read(), b1 = f.read();
  if(b0!='B' || b1!='M'){
    char msg[32]; sprintf(msg,"first bytes %02X %02X", b0, b1);
    showMultiline(msg); f.close(); return;
  }

  f.seek(10); uint32_t offs = f.read() | (f.read()<<8) | (f.read()<<16) | (f.read()<<24);
  f.seek(18); int32_t w = f.read() | (f.read()<<8) | (f.read()<<16) | (f.read()<<24);
  int32_t h = f.read() | (f.read()<<8) | (f.read()<<16) | (f.read()<<24);
  if(w!=800 || h!=480){ showMultiline("size mismatch"); f.close(); return; }
  uint32_t rowBytes = (w*3+3)&~3;

  display.setRotation(2);
  display.setFullWindow();
  display.firstPage();
  do{
    for(int32_t row=h-1; row>=0; --row){
      f.seek(offs + row*rowBytes);
      for(int32_t col=0; col<w; ++col){
        uint8_t b = f.read(), g = f.read(), r = f.read();
        uint16_t idx = (r>200 && g<100 && b<100)?GxEPD_RED :
                       (r>200 && g>200 && b<100)?GxEPD_YELLOW :
                       (r<100 && g<100 && b>200)?GxEPD_BLUE :
                       (r<100 && g>150 && b<100)?GxEPD_GREEN :
                       (r>200 && g>150 && b>50)?GxEPD_ORANGE :
                       (r>200 && g>200 && b>200)?GxEPD_WHITE : GxEPD_BLACK;
        display.drawPixel(col, row, idx);
      }
    }
  }while(display.nextPage());
  f.close();
}

//--------------------------------------------------
void connectWiFi(){
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  uint32_t start = millis();
  while(WiFi.status()!=WL_CONNECTED && millis()-start < 15000){ delay(500); }
}

void setup(){
  Serial.begin(115200);
  #if defined(ESP32)
    hspi.begin(13,12,14,PIN_CS);
    display.epd2.selectSPI(hspi, SPISettings(4000000, MSBFIRST, SPI_MODE0));
  #endif
  display.init(115200);
  SPIFFS.begin(true);

  connectWiFi();
  String imgURL = fetchImageURL();
  //showMultiline(imgURL);

  if(downloadToSpiffs(imgURL)){
    delay(500);
    showBMP();
  }
  display.hibernate();
}

void loop(){}
