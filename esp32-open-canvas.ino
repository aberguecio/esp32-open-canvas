#include <GxEPD2_7C.h>
#include <Fonts/FreeMonoBold9pt7b.h>
#include <SPI.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <SPIFFS.h>
#include "esp_sleep.h"

#define USE_HSPI_FOR_EPD
#define GxEPD2_DISPLAY_CLASS GxEPD2_7C
#define GxEPD2_DRIVER_CLASS  GxEPD2_730c_GDEY073D46

// ---- Wi-Fi credentials ----
const char* WIFI_SSID = "RED-BERGUECIO-2.4G";
const char* WIFI_PASS = "Colchagua81";
const char* API_URL   = "https://canvas.berguecio.cl/api/v1/images";

// FireBeetle 2 ESP32-C6 pinout for EPD
constexpr uint8_t PIN_CS   =  1;  // LCD_CS → D6
constexpr uint8_t PIN_DC   =  8;  // LCD_DC → D2
constexpr uint8_t PIN_RST  = 14;  // LCD_RST→ D3
constexpr uint8_t PIN_BUSY =  4;  // BUSY  → d11

// On-board LED
constexpr uint8_t LED_PIN = 15;  // Internal LED

// SPI on FireBeetle 2 ESP32-C6: use FSPI (HSPI no existe en C6)
// FSPI es equivalente a SPI2_HOST en ESP32-C6
SPIClass epdSPI(FSPI);

#if defined(ESP32)
  #define MAX_DISPLAY_BUFFER_SIZE 65536ul
  #define MAX_HEIGHT(EPD) ((EPD::HEIGHT <= (MAX_DISPLAY_BUFFER_SIZE)/(EPD::WIDTH/2)) \
                              ? EPD::HEIGHT : ((MAX_DISPLAY_BUFFER_SIZE)/(EPD::WIDTH/2)))
  GxEPD2_DISPLAY_CLASS< GxEPD2_DRIVER_CLASS,
                        MAX_HEIGHT(GxEPD2_DRIVER_CLASS) >
    display(GxEPD2_DRIVER_CLASS(PIN_CS, PIN_DC, PIN_RST, PIN_BUSY));
#endif

unsigned long sleepTimeMs = 0;

void centerText(const char* txt){
  Serial.println(">> centerText()");
  display.setFont(&FreeMonoBold9pt7b);
  display.setTextColor(GxEPD_BLACK);
  int16_t bx,by; uint16_t bw,bh;
  display.getTextBounds(txt,0,0,&bx,&by,&bw,&bh);
  display.setCursor(((display.width()-bw)/2)-bx, ((display.height()-bh)/2)-by);
  display.print(txt);
}

void showMultiline(const String& txt){
  Serial.println(">> showMultiline()");
  display.setRotation(0);
  display.setFullWindow();
  display.firstPage();
  do {
    display.fillScreen(GxEPD_WHITE);
    display.setFont(&FreeMonoBold9pt7b);
    display.setTextColor(GxEPD_BLACK);
    uint16_t lineH = FreeMonoBold9pt7b.yAdvance;
    int16_t y = lineH;
    for(size_t i=0; i<txt.length(); ){
      String line = txt.substring(i, min(i+60, txt.length()));
      display.setCursor(0,y);
      display.print(line);
      y += lineH;
      i += 60;
    }
  } while(display.nextPage());
}

String fetchImageURL(){
  Serial.println(">> fetchImageURL(): begin HTTP GET");
  HTTPClient http;
  http.begin(API_URL);
  int code = http.GET();
  Serial.printf("   HTTP GET code: %d\n", code);
  if(code != HTTP_CODE_OK){
    Serial.println("   ERROR: HTTP GET failed");
    http.end();
    return String("HTTP ")+code;
  }
  String body = http.getString();
  http.end();
  Serial.println("   HTTP GET succeeded, parsing JSON");

  // parse URL
  int idx = body.indexOf("\"url\"");
  if(idx < 0){
    Serial.println("   ERROR: \"url\" not found");
    return F("url not found");
  }
  idx = body.indexOf('"', idx+5);
  if(idx < 0){
    Serial.println("   ERROR: parse err at url start");
    return F("parse err");
  }
  int end = body.indexOf('"', idx+1);
  if(end < 0){
    Serial.println("   ERROR: parse err at url end");
    return F("parse err");
  }
  String url = body.substring(idx+1, end);
  Serial.printf("   Parsed URL: %s\n", url.c_str());

  // parse remainingMs
  idx = body.indexOf("\"remainingMs\"");
  if(idx >= 0){
    Serial.println("   Parsing remainingMs");
    idx = body.indexOf(':', idx) + 1;
    int endMs = body.indexOf(',', idx);
    if(endMs < 0) endMs = body.indexOf('}', idx);
    sleepTimeMs = body.substring(idx, endMs).toInt();
    Serial.printf("   remainingMs = %lu\n", sleepTimeMs);
  } else {
    Serial.println("   No remainingMs field");
  }

  return url;
}

bool downloadToSpiffs(const String& url){
  Serial.printf(">> downloadToSpiffs(): URL = %s\n", url.c_str());
  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  http.begin(client, url);

  Serial.println("   HTTP GET image");
  int code = http.GET();
  Serial.printf("   HTTP code %d\n", code);
  if(code != HTTP_CODE_OK){
    Serial.println("   ERROR: image download failed");
    http.end();
    return false;
  }

  Serial.println("   Opening SPIFFS file for write");
  File f = SPIFFS.open("/img", FILE_WRITE);
  if(!f){
    Serial.println("   ERROR: SPIFFS open fail");
    http.end();
    return false;
  }

  size_t total = http.writeToStream(&f);
  Serial.printf("   Downloaded %u bytes\n", total);
  f.close();
  http.end();
  return total > 0;
}

void showBMP(){
  Serial.println(">> showBMP(): open /img");
  File f = SPIFFS.open("/img", FILE_READ);
  if(!f){
    Serial.println("   ERROR: /img open fail");
    showMultiline("/img open fail");
    return;
  }

  Serial.println("   Checking BMP header");
  if(f.read()!='B' || f.read()!='M'){
    Serial.println("   ERROR: Not BMP");
    showMultiline("Not BMP");
    f.close();
    return;
  }

  f.seek(10);
  uint32_t offs = f.read() | (f.read()<<8) | (f.read()<<16) | (f.read()<<24);
  Serial.printf("   bfOffBits = %lu\n", offs);

  f.seek(18);
  int32_t w = f.read() | (f.read()<<8) | (f.read()<<16) | (f.read()<<24);
  int32_t h = f.read() | (f.read()<<8) | (f.read()<<16) | (f.read()<<24);
  Serial.printf("   Width=%ld, Height=%ld\n", w, h);
  if(w!=800 || h!=480){
    Serial.println("   ERROR: size mismatch");
    showMultiline("size mismatch");
    f.close();
    return;
  }

  f.seek(28);
  uint16_t bpp = f.read() | (f.read()<<8);
  Serial.printf("   bpp = %u\n", bpp);
  if(bpp != 4){
    Serial.println("   ERROR: not 4-bpp");
    showMultiline("not 4-bpp");
    f.close();
    return;
  }

  Serial.println("   Drawing BMP to display");

  const uint32_t rowBytes = (((w + 1) >> 1) + 3) & ~3;
  static const uint16_t pal4b[16] = {
    GxEPD_WHITE, GxEPD_BLACK, GxEPD_RED,    GxEPD_ORANGE,
    GxEPD_YELLOW, GxEPD_GREEN, GxEPD_BLUE,  GxEPD_BLACK,
    GxEPD_BLACK, GxEPD_BLACK,  GxEPD_BLACK, GxEPD_BLACK,
    GxEPD_BLACK, GxEPD_BLACK,  GxEPD_BLACK, GxEPD_BLACK
  };
  Serial.println("   Drawing BMP Ready");

  display.setRotation(0);
  display.setFullWindow();
  display.firstPage();
  do {
    for(int32_t row = h - 1; row >= 0; --row) {
      f.seek(offs + row * rowBytes);
      for(int32_t col = 0; col < w; col += 2) {
        uint8_t byte = f.read();
        uint8_t idxL =  byte & 0x0F;
        uint8_t idxR =  byte >> 4;
        int32_t xL = (w - 1) - col;
        int32_t xR = (w - 1) - (col + 1);
        display.drawPixel(xL, row, pal4b[idxL]);
        if(col + 1 < w)
          display.drawPixel(xR, row, pal4b[idxR]);
      }
    }

  } while (display.nextPage());

  Serial.println("   BMP display done");
  f.close();
}

void connectWiFi(){
  Serial.printf(">> connectWiFi(): SSID=%s\n", WIFI_SSID);
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  uint32_t start = millis();
  while(WiFi.status()!=WL_CONNECTED && millis()-start < 15000){
    Serial.print(".");
    delay(500);
  }
  if(WiFi.status()==WL_CONNECTED){
    Serial.println("\n   WiFi connected!");
    Serial.printf("   IP: %s\n", WiFi.localIP().toString().c_str());
  } else {
    Serial.println("\n   ERROR: WiFi connect failed");
  }
}

void setup(){
  Serial.begin(115200);
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, HIGH);
  pinMode(PIN_BUSY, INPUT_PULLUP);
  Serial.println("=== STARTUP ===");

  #if defined(ESP32)
    delay(100);  // Dar tiempo al hardware antes de inicializar SPI
    epdSPI.begin(23, 21, 22, PIN_CS);
    display.epd2.selectSPI(epdSPI,
      SPISettings(4000000, MSBFIRST, SPI_MODE0));
    delay(100);  // Estabilizar antes de init del display
  #endif

  Serial.println("Initializing display");
  display.init(115200);
  Serial.println("Display initialized successfully");

  Serial.println("Initializing SPIFFS");
  if(!SPIFFS.begin(true)){
    Serial.println("   ERROR: SPIFFS mount failed");
  }

  connectWiFi();
  String imgURL = fetchImageURL();

  if(imgURL.startsWith("HTTP") || imgURL.startsWith("url not")){
    Serial.println("Skipping download due to bad URL");
  } else if(downloadToSpiffs(imgURL)){
    Serial.println("Download OK, turning off LED");
    digitalWrite(LED_PIN, LOW);
    delay(500);
    showBMP();
  } else {
    Serial.println("ERROR: downloadToSpiffs() failed");
  }

  Serial.println("Putting display into hibernate");
  display.hibernate();
  Serial.printf("Deep sleep for %lu ms\n", sleepTimeMs);
  esp_sleep_enable_timer_wakeup(sleepTimeMs * 1000ULL);
  Serial.println("Entering deep sleep now");
  esp_deep_sleep_start();
}

void loop() {
  // not used
}
