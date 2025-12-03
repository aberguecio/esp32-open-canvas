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
constexpr uint8_t PIN_BUSY =  17;  // BUSY  → d11

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

//=============================================================================
// CONFIGURATION - Adjust these values for your environment
//=============================================================================

// WiFi Configuration
const unsigned long WIFI_INIT_DELAY_MS = 500;        // Delay before WiFi init
const unsigned long WIFI_CONNECT_TIMEOUT_MS = 120000; // 120s - INCREASED from 60s for weak WiFi
const unsigned long WIFI_STABILIZE_DELAY_MS = 1000;  // Delay after WiFi connects

// HTTP/API Configuration
const unsigned long API_HTTP_TIMEOUT_MS = 60000;     // 60s - INCREASED from 20s
const int API_MAX_RETRIES = 5;                       // API fetch retry attempts
const unsigned long API_RETRY_DELAY_MS = 3000;       // 3s - INCREASED from 2s

// Download Configuration
const unsigned long DOWNLOAD_HTTP_TIMEOUT_MS = 60000;   // 60s - INCREASED from 30s
const int DOWNLOAD_MAX_RETRIES = 3;                     // Download retry attempts
const unsigned long DOWNLOAD_RETRY_DELAY_MS = 5000;     // 5s - INCREASED from 3s
const unsigned long DOWNLOAD_STREAM_TIMEOUT_MS = 120000; // 120s - INCREASED from 60s
const unsigned long DOWNLOAD_PROGRESS_INTERVAL_MS = 2000; // Progress print interval

// Sleep Configuration
const unsigned long DEFAULT_SLEEP_MS = 300000;        // 5 minutes default
const unsigned long MIN_ERROR_SLEEP_MS = 300000;      // Start at 5 minutes on error
const unsigned long MAX_ERROR_SLEEP_MS = 12000000;     // Max 200 minutes

// Hardware Configuration
const unsigned long SPI_INIT_DELAY_MS = 100;          // SPI initialization delays

//=============================================================================

unsigned long sleepTimeMs = DEFAULT_SLEEP_MS;

// Error tracking with exponential backoff
bool hasError = false;
String errorMessage = "";
RTC_DATA_ATTR unsigned long errorSleepMs = MIN_ERROR_SLEEP_MS;  // Persists across deep sleep
RTC_DATA_ATTR int consecutiveErrors = 0;  // Track error streak

//=============================================================================
// Helper Functions
//=============================================================================

// Helper to log HTTP error codes
void logHTTPError(int code) {
  if(code == -1) Serial.println("   (Connection error)");
  else if(code == -5) Serial.println("   (Connection refused/timeout)");
  else if(code == -11) Serial.println("   (Request timeout)");
  else Serial.printf("   (HTTP error code: %d)\n", code);
}

// Helper to format error messages for display
String getErrorDetails() {
  String details = errorMessage;

  if(WiFi.status() != WL_CONNECTED && errorMessage == "WiFi failed") {
    details += "\nStatus: ";
    switch(WiFi.status()) {
      case 1: details += "No SSID"; break;
      case 4: details += "Connection failed"; break;
      case 6: details += "Disconnected"; break;
      default: details += String(WiFi.status()); break;
    }
  }

  return details;
}

String fetchImageURL(){
  Serial.println(">> fetchImageURL(): begin HTTP GET");
  Serial.printf("   API URL: %s\n", API_URL);

  HTTPClient http;
  http.setTimeout(API_HTTP_TIMEOUT_MS);

  int code = -1;

  for(int retry = 0; retry < API_MAX_RETRIES; retry++) {
    if(retry > 0) {
      Serial.printf("   Retry attempt %d/%d\n", retry + 1, API_MAX_RETRIES);
      delay(API_RETRY_DELAY_MS);
    }

    Serial.println("   Starting HTTP connection...");
    bool begun = http.begin(API_URL);

    if(!begun){
      Serial.println("   ERROR: http.begin() failed");
      continue;  // Try again
    }

    Serial.println("   Sending GET request...");
    code = http.GET();
    Serial.printf("   HTTP GET code: %d\n", code);

    if(code == HTTP_CODE_OK) {
      break;  // Success!
    }

    logHTTPError(code);
    http.end();
  }

  if(code != HTTP_CODE_OK){
    Serial.println("   ERROR: HTTP GET failed after all retries");
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
  http.setTimeout(DOWNLOAD_HTTP_TIMEOUT_MS);

  int code = -1;

  for(int retry = 0; retry < DOWNLOAD_MAX_RETRIES; retry++) {
    if(retry > 0) {
      Serial.printf("   Retry %d/%d\n", retry + 1, DOWNLOAD_MAX_RETRIES);
      delay(DOWNLOAD_RETRY_DELAY_MS);
      http.end();
    }

    if(!http.begin(client, url)) {
      Serial.println("   ERROR: http.begin() failed");
      continue;
    }

    Serial.println("   HTTP GET image");
    code = http.GET();
    Serial.printf("   HTTP code %d\n", code);

    if(code == HTTP_CODE_OK) break;

    logHTTPError(code);
  }

  if(code != HTTP_CODE_OK){
    Serial.println("   ERROR: Download failed after all retries");
    http.end();
    return false;
  }

  // Now we have a successful connection - proceed with download

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

  // Download with progress and timeout protection
  uint8_t buff[512];
  size_t total = 0;
  unsigned long lastPrint = millis();
  unsigned long downloadStart = millis();

  while(contentLength > 0 || contentLength == -1) {
    // Check timeout
    if(millis() - downloadStart > DOWNLOAD_STREAM_TIMEOUT_MS) {
      Serial.println("   ERROR: Download timeout");
      f.close();
      http.end();
      SPIFFS.remove("/img");
      return false;
    }

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

        // Print progress
        if(millis() - lastPrint > DOWNLOAD_PROGRESS_INTERVAL_MS) {
          Serial.printf("   Downloaded: %u bytes\n", total);
          lastPrint = millis();
        }

        // Reset timeout on successful read
        downloadStart = millis();
      }
    } else if(!http.connected()) {
      // Only exit if both: no data available AND disconnected
      Serial.println("   Connection closed and no data available");
      break;
    } else {
      // No data available but still connected - wait a bit
      delay(10);
    }
  }

  Serial.printf("   Downloaded %u bytes total\n", total);

  // Validate size matches expected from HTTP headers
  int expectedSize = http.getSize();
  if(expectedSize > 0 && total != (size_t)expectedSize) {
    Serial.printf("   ERROR: Size mismatch! Expected %d, got %u\n", expectedSize, total);
    f.close();
    http.end();
    SPIFFS.remove("/img");  // Remove partial file
    return false;
  }

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
    return;
  }

  Serial.println("   Checking BMP header");
  if(f.read()!='B' || f.read()!='M'){
    Serial.println("   ERROR: Not BMP");
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
    f.close();
    return;
  }

  f.seek(28);
  uint16_t bpp = f.read() | (f.read()<<8);
  Serial.printf("   bpp = %u\n", bpp);
  if(bpp != 4){
    Serial.println("   ERROR: not 4-bpp");
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
  delay(WIFI_INIT_DELAY_MS);

  // Disconnect any previous connection
  WiFi.disconnect(true);
  delay(100);

  // Set WiFi mode and wait for it to stabilize
  WiFi.mode(WIFI_STA);
  // Set max TX power to improve range
  WiFi.setTxPower(WIFI_POWER_19_5dBm);
  delay(100);

  WiFi.begin(WIFI_SSID, WIFI_PASS);

  Serial.println("   Connecting to WiFi...");
  uint32_t start = millis();
  int attempts = 0;

  while(WiFi.status() != WL_CONNECTED && millis() - start < WIFI_CONNECT_TIMEOUT_MS){
    Serial.print(".");
    delay(500);
    attempts++;

    // Show WiFi status every 20 attempts
    if(attempts % 20 == 0){
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
    delay(SPI_INIT_DELAY_MS);  // Hardware initialization delay
    epdSPI.begin(23, 21, 22, PIN_CS);
    display.epd2.selectSPI(epdSPI,
      SPISettings(4000000, MSBFIRST, SPI_MODE0));
    delay(SPI_INIT_DELAY_MS);  // Stabilization delay
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
    delay(WIFI_STABILIZE_DELAY_MS);

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

      // Success! Reset error counter and backoff
      consecutiveErrors = 0;
      errorSleepMs = MIN_ERROR_SLEEP_MS;
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
    consecutiveErrors++;
    Serial.printf("ERROR occurred: %s (consecutive errors: %d)\n", errorMessage.c_str(), consecutiveErrors);

    // Exponential backoff: double sleep time on each error, capped at MAX
    sleepTimeMs = errorSleepMs;
    Serial.printf("Retry sleep time: %lu ms (%.1f min)\n", sleepTimeMs, sleepTimeMs / 60000.0);

    // Double for next time, but cap at maximum
    errorSleepMs = min(errorSleepMs * 2, MAX_ERROR_SLEEP_MS);

    display.setRotation(2);
    display.setFullWindow();
    display.firstPage();
    do {
      display.fillScreen(GxEPD_WHITE);
      display.setFont(&FreeMonoBold9pt7b);

      // Error title
      display.setTextColor(GxEPD_RED);
      display.setCursor(10, 30);
      display.print("ERROR");

      // Error details with word wrap
      display.setTextColor(GxEPD_BLACK);
      String details = getErrorDetails();

      int y = 70;
      int maxChars = 35;  // ~35 chars fit on 800px width
      for(int i = 0; i < details.length(); i += maxChars) {
        String line = details.substring(i, min(i + maxChars, (int)details.length()));
        display.setCursor(10, y);
        display.print(line);
        y += 25;
      }

      // Retry info
      display.setCursor(10, y + 20);
      display.printf("Retry: %.0f min", sleepTimeMs / 60000.0);
      display.setCursor(10, y + 45);
      display.printf("Attempt: %d", consecutiveErrors);

      // WiFi signal if still connected
      if(WiFi.status() == WL_CONNECTED) {
        display.setCursor(10, y + 70);
        display.printf("RSSI: %d dBm", WiFi.RSSI());
      }
    } while(display.nextPage());
  } else {
    // Success! Use normal sleep time from API (or default)
    Serial.printf("Success! Next update in %lu ms (%.1f min)\n", sleepTimeMs, sleepTimeMs / 60000.0);
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
