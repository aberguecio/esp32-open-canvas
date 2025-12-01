#include <GxEPD2_7C.h>
#include <Fonts/FreeMonoBold9pt7b.h>
#include <SPI.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <SPIFFS.h>
#include "esp_sleep.h"
#include "config.h"

#define GxEPD2_DISPLAY_CLASS GxEPD2_7C
#define GxEPD2_DRIVER_CLASS  GxEPD2_730c_GDEY073D46

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

// Sleep configuration
const unsigned long DEFAULT_SLEEP_MS = 300000;  // 5 minutes default
const unsigned long ERROR_SLEEP_MS = 60000;     // 1 minute on error
unsigned long sleepTimeMs = DEFAULT_SLEEP_MS;

// Error tracking
bool hasError = false;
String errorMessage = "";

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
  Serial.printf("   API URL: %s\n", API_URL);

  HTTPClient http;
  http.setTimeout(10000);  // 10 second timeout

  Serial.println("   Starting HTTP connection...");
  bool begun = http.begin(API_URL);

  if(!begun){
    Serial.println("   ERROR: http.begin() failed");
    return F("Connection failed");
  }

  Serial.println("   Sending GET request...");
  int code = http.GET();
  Serial.printf("   HTTP GET code: %d\n", code);

  if(code != HTTP_CODE_OK){
    Serial.println("   ERROR: HTTP GET failed");
    if(code == -1) Serial.println("   (Connection error - check DNS, firewall, or server)");
    if(code == -11) Serial.println("   (Timeout - server too slow or unreachable)");
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
  http.setTimeout(30000);  // 30 second timeout
  http.begin(client, url);

  Serial.println("   HTTP GET image");
  int code = http.GET();
  Serial.printf("   HTTP code %d\n", code);
  if(code != HTTP_CODE_OK){
    Serial.println("   ERROR: image download failed");
    http.end();
    return false;
  }

  // Check SPIFFS space
  size_t totalBytes = SPIFFS.totalBytes();
  size_t usedBytes = SPIFFS.usedBytes();
  Serial.printf("   SPIFFS: %u/%u bytes used\n", usedBytes, totalBytes);

  // Remove old file if exists
  if(SPIFFS.exists("/img")) {
    Serial.println("   Removing old /img");
    SPIFFS.remove("/img");
  }

  Serial.println("   Opening SPIFFS file for write");
  File f = SPIFFS.open("/img", FILE_WRITE);
  if(!f){
    Serial.println("   ERROR: SPIFFS open fail");
    http.end();
    return false;
  }

  Serial.println("   File opened, getting WiFiClient stream...");

  // Get content length
  int contentLength = http.getSize();
  Serial.printf("   Expected content length: %d bytes\n", contentLength);

  // Get the stream
  WiFiClient * stream = http.getStreamPtr();
  if(!stream) {
    Serial.println("   ERROR: Failed to get stream");
    f.close();
    http.end();
    return false;
  }

  // Download with progress
  uint8_t buff[512];
  size_t total = 0;
  unsigned long lastPrint = millis();

  while(http.connected() && (contentLength > 0 || contentLength == -1)) {
    size_t available = stream->available();
    if(available) {
      int c = stream->readBytes(buff, min(available, sizeof(buff)));
      if(c > 0) {
        size_t written = f.write(buff, c);
        if(written != c) {
          Serial.printf("   ERROR: SPIFFS write failed (%u/%d bytes)\n", written, c);
          f.close();
          http.end();
          SPIFFS.remove("/img");
          return false;
        }
        total += c;
        if(contentLength > 0) {
          contentLength -= c;
        }

        // Print progress every 2 seconds
        if(millis() - lastPrint > 2000) {
          Serial.printf("   Downloaded: %u bytes\n", total);
          lastPrint = millis();
        }
      }
    }
    delay(1);
  }

  Serial.printf("   Downloaded %u bytes total\n", total);

  // Validate reasonable size (800x480x4bpp/2 + header ≈ 192KB)
  if(total < 1000 || total > 500000) {
    Serial.printf("   ERROR: Invalid file size %u bytes\n", total);
    f.close();
    http.end();
    SPIFFS.remove("/img");  // Remove corrupted file
    return false;
  }

  f.close();
  http.end();
  return true;
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
  if(w != display.width() || h != display.height()){
    Serial.printf("   ERROR: size mismatch. Expected %dx%d, got %ldx%ld\n",
                  display.width(), display.height(), w, h);
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

  // ESP32-C6 needs time to initialize WiFi after deep sleep
  delay(500);

  // Disconnect any previous connection
  WiFi.disconnect(true);
  delay(100);

  // Set WiFi mode and wait for it to stabilize
  WiFi.mode(WIFI_STA);
  delay(100);

  WiFi.begin(WIFI_SSID, WIFI_PASS);

  Serial.println("   Connecting to WiFi...");
  uint32_t start = millis();
  int attempts = 0;

  while(WiFi.status() != WL_CONNECTED && millis() - start < 60000){  // Increased to 1 minute
    Serial.print(".");
    delay(500);
    attempts++;

    // Show WiFi status every 10 attempts
    if(attempts % 10 == 0){
      Serial.printf("\n   WiFi status: %d (attempts: %d)\n", WiFi.status(), attempts);
    }
  }

  if(WiFi.status() == WL_CONNECTED){
    Serial.println("\n   WiFi connected!");
    Serial.printf("   IP: %s\n", WiFi.localIP().toString().c_str());
    Serial.printf("   RSSI: %d dBm\n", WiFi.RSSI());
  } else {
    Serial.println("\n   ERROR: WiFi connect failed");
    Serial.printf("   Final status: %d\n", WiFi.status());
    Serial.println("   Status codes: 0=IDLE, 1=NO_SSID, 3=CONNECTED, 4=FAIL, 6=DISCONNECTED");
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

  // Check WiFi connection
  if(WiFi.status() != WL_CONNECTED){
    hasError = true;
    errorMessage = "WiFi failed";
  } else {
    // Wait for WiFi to fully stabilize before making requests
    Serial.println("WiFi connected, waiting for stack to stabilize...");
    delay(1000);

    // Test DNS resolution
    Serial.println("Testing DNS resolution...");
    IPAddress serverIP;
    if(WiFi.hostByName("canvas.berguecio.cl", serverIP)){
      Serial.printf("   DNS OK: canvas.berguecio.cl -> %s\n", serverIP.toString().c_str());
    } else {
      Serial.println("   WARNING: DNS resolution failed!");
    }

    String imgURL = fetchImageURL();

    if(imgURL.startsWith("HTTP") || imgURL.startsWith("url not") || imgURL.startsWith("parse err")){
      Serial.println("Skipping download due to bad URL");
      hasError = true;
      errorMessage = "API error";
    } else if(downloadToSpiffs(imgURL)){
      Serial.println("Download OK, turning off LED");
      digitalWrite(LED_PIN, LOW);
      delay(500);
      showBMP();
    } else {
      Serial.println("ERROR: downloadToSpiffs() failed");
      hasError = true;
      errorMessage = "Download failed";
    }
  }

  // Disconnect WiFi to save power
  Serial.println("Disconnecting WiFi to save power");
  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);

  // Show error on display if any occurred
  if(hasError){
    Serial.printf("ERROR occurred: %s\n", errorMessage.c_str());
    display.setRotation(0);
    display.setFullWindow();
    display.firstPage();
    do {
      display.fillScreen(GxEPD_WHITE);
      display.setFont(&FreeMonoBold9pt7b);
      display.setTextColor(GxEPD_RED);
      display.setCursor(10, 50);
      display.print("ERROR:");
      display.setCursor(10, 100);
      display.print(errorMessage);
      display.setTextColor(GxEPD_BLACK);
      display.setCursor(10, 150);
      display.print("Retry in 1 min");
    } while(display.nextPage());

    sleepTimeMs = ERROR_SLEEP_MS;  // Retry in 1 minute
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
