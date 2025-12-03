# ESP32 Open Canvas - E-ink Display Controller

Remote display system for Waveshare 7.3" ACeP e-ink display (7 colors) using FireBeetle 2 ESP32-C6 with deep sleep for battery optimization.

## Features

- 🖼️ Automatic image download from REST API
- 🎨 Support for 4-bpp BMP images (16 colors)
- 🔋 Ultra-low power consumption with deep sleep
- 📡 Dynamic sleep time based on API response
- ⚡ WiFi auto-disconnect after download to save power
- 🛡️ Error handling with on-screen display
- 🔄 Automatic retry on errors

## Hardware Requirements

### Components

- **FireBeetle 2 ESP32-C6** (DFRobot DFR1075)
- **Waveshare 7.3" ACeP E-Ink Display** (800x480, 7 colors)
- Compatible connection cable

### Pinout (ESP32-C6)

| Display Pin | ESP32-C6 GPIO | Function |
|-------------|---------------|----------|
| CS          | GPIO1         | Chip Select |
| DC          | GPIO8         | Data/Command |
| RST         | GPIO14        | Reset |
| BUSY        | GPIO17        | Busy Signal |
| SCK         | GPIO23        | SPI Clock |
| MOSI        | GPIO22        | SPI Data Out |
| MISO        | GPIO21        | SPI Data In |
| VCC         | 3.3V          | Power |
| GND         | GND           | Ground |

## Installation

### 1. Software Requirements

- **Arduino IDE 2.x** or higher
- **ESP32 Board Support** >= 2.0.14

### 2. Required Libraries

Install via Arduino IDE Library Manager:

- **GxEPD2** - E-ink display driver
- **SPIFFS** - File system (included with ESP32 core)

### 3. Arduino IDE Configuration

**CRITICAL:** For Serial Monitor to work on ESP32-C6:

1. Open Arduino IDE
2. Go to **Tools** → **USB CDC on Boot** → **Enabled**
3. Select **Tools** → **Board** → **DFRobot FireBeetle 2 ESP32-C6**
4. Select **Tools** → **Port** → (your COM port)

### 4. Project Configuration

1. Create a `config.h` file in the project folder:

```cpp
#ifndef CONFIG_H
#define CONFIG_H

// WiFi Configuration
const char* WIFI_SSID = "YOUR_SSID";
const char* WIFI_PASS = "YOUR_PASSWORD";

// API Configuration
const char* API_URL = "https://your-server.com/api/v1/images";

#endif
```

2. Compile and upload the sketch

> **Note:** The `config.h` file is already in `.gitignore` to keep your credentials safe.

## API Format

### Endpoint: GET /api/v1/images

**Expected JSON response:**

```json
{
  "url": "https://example.com/image.bmp",
  "remainingMs": 300000
}
```

**Fields:**

- `url` (required): Direct URL to the BMP file
- `remainingMs` (optional): Milliseconds until next update. Default: 5 minutes (300000ms)

### Image Format

- **Type:** BMP (Bitmap)
- **Dimensions:** 800x480 pixels
- **Bits per pixel:** 4 (16 colors)
- **Color palette** (indices 0-15):
  - 0: White
  - 1: Black
  - 2: Red
  - 3: Orange
  - 4: Yellow
  - 5: Green
  - 6: Blue
  - 7-15: Black (reserved)

## How It Works

### Operation Cycle

1. **Wake up** from deep sleep
2. **Initialize** hardware (SPI, display, SPIFFS)
3. **Connect** to WiFi (90-second timeout)
4. **Download** metadata from API
5. **Download** BMP image to SPIFFS
6. **Render** image on e-ink display
7. **Disconnect** WiFi to save power
8. **Enter deep sleep** for specified time
9. **Repeat** from step 1

### Power Consumption Estimates

- **Active** (download + render): ~150mA for 10-30 seconds
- **Deep sleep**: <1mA
- **Battery life** (1000mAh, 5-min updates): ~30-60 days

## Architecture

### State Machine Design

The code uses a **state machine pattern** within `setup()` to organize the wake cycle into clear phases:

```
INIT → WIFI → API → DOWNLOAD → DISPLAY → SLEEP
  ↓      ↓     ↓       ↓          ↓
  └──────┴─────┴───────┴──────────→ ERROR → SLEEP
```

**Benefits:**
- **Clear separation** of operational phases (hardware init, networking, download, display)
- **Centralized error handling** - all errors go through a single `handleError()` function
- **Easy debugging** - phase logging shows exactly where failures occur
- **Simple to extend** - add new phases (e.g., OTA updates, sensor readings) easily
- **Consistent error display** - unified error messages on e-ink screen with retry info

**Why not use loop()?**

The traditional Arduino `loop()` pattern would keep the ESP32 powered continuously (~150mA), wasting battery. Instead, we use `esp_deep_sleep_start()` at the end of `setup()`, which:
- Reduces power consumption from ~150mA to <1mA during sleep
- Provides automatic crash recovery via watchdog timer
- Prevents memory leaks with fresh restart each cycle
- Allows e-ink display to retain image without power

Each wake cycle is independent and follows the state machine, then enters deep sleep until the next update.

## Error Handling

The system displays errors on the e-ink screen with **exponential backoff** retry strategy.

**Error types:**

- ❌ **WiFi failed** - Cannot connect to network
- ❌ **API error** - Cannot fetch image URL
- ❌ **Download failed** - Image download timeout or error

**Retry behavior:**

- 1st error: Retry in 5 minutes
- 2nd error: Retry in 10 minutes (2x)
- 3rd error: Retry in 20 minutes (2x)
- 4th error: Retry in 40 minutes (2x)
- 5th+ error: Retry in 80 minutes up to max 200 minutes
- Success: Reset to normal 5-minute interval

This prevents battery drain from rapid retry attempts while ensuring eventual recovery.

All errors are also logged to Serial Monitor (115200 baud) with detailed diagnostics.

## Configuration & Timeouts

All timeouts and retry settings can be adjusted at the top of `esp32-open-canvas.ino`:

### Default Configuration

| Setting | Default Value | Description |
|---------|---------------|-------------|
| WiFi Init Delay | 500ms | Delay before WiFi initialization |
| WiFi Connect Timeout | 90s | Maximum time to wait for WiFi connection |
| WiFi Stabilize Delay | 1000ms | Delay after WiFi connects |
| API HTTP Timeout | 30s | Timeout for API requests |
| API Max Retries | 5 | Number of API fetch retry attempts |
| API Retry Delay | 3s | Delay between API retries |
| Download HTTP Timeout | 45s | Timeout for image download requests |
| Download Max Retries | 3 | Number of download retry attempts |
| Download Retry Delay | 5s | Delay between download retries |
| Download Stream Timeout | 90s | Timeout for streaming image data |
| SPI Init Delay | 100ms | Hardware initialization delays |

### Recommended Settings for Weak WiFi Signal

If you experience frequent connection issues (RSSI < -80 dBm), increase these values:

| Setting | Default | Weak Signal |
|---------|---------|-------------|
| WiFi Connect Timeout | 90s | 120s |
| API HTTP Timeout | 30s | 45s |
| Download HTTP Timeout | 45s | 60s |
| Download Stream Timeout | 90s | 120s |
| API Retry Delay | 3s | 5s |
| Download Retry Delay | 5s | 8s |

**To adjust:** Edit the configuration section at the top of `esp32-open-canvas.ino` (after line 36).

## Troubleshooting

### Display doesn't update

1. ✅ Check physical connections (especially BUSY, CS, DC, RST)
2. ✅ Confirm `USB CDC on Boot` is **Enabled**
3. ✅ Check Serial Monitor logs at 115200 baud
4. ✅ Try `test_basico_display.ino` first

### WiFi won't connect

1. ✅ Verify SSID and password in `config.h`
2. ✅ Ensure using 2.4GHz network (not 5GHz)
3. ✅ Check signal strength
4. ✅ Verify network allows new devices

### Serial Monitor shows nothing

**Solution:** Tools → USB CDC on Boot → **Enabled**

The ESP32-C6 uses native USB (CDC), not an external UART chip.

### Image looks distorted

1. ✅ Verify BMP is exactly 800x480, 4-bpp
2. ✅ Check color palette matches expected format
3. ✅ Test with a known-good sample image

### Deep sleep doesn't work

1. ✅ Check that `esp_deep_sleep_start()` is being called
2. ✅ Verify `remainingMs` value in API response
3. ✅ USB CDC may prevent deep sleep - disconnect USB after testing

## Project Files

```
esp32-open-canvas/
├── esp32-open-canvas.ino    # Main program
├── test_basico_display.ino  # Basic display test (no WiFi)
├── hello_world_simple/
│   └── hello_world_simple.ino  # Hardware test
├── config.h                 # WiFi credentials (create this)
├── .gitignore              # Git ignore file
└── README.md               # This file
```

## ESP32 vs ESP32-C6 Differences

| Feature | ESP32 Classic | ESP32-C6 |
|---------|---------------|----------|
| SPI Buses | HSPI + VSPI | FSPI only |
| SPI Constant | `HSPI` / `VSPI` | `FSPI` |
| USB Serial | External chip (CH340, CP2102) | Native USB (CDC) |
| CDC Config | Not required | **USB CDC on Boot: Enabled** |
| Deep sleep | ~10μA | ~7μA (better) |
| GPIO Count | 40 pins | 19 pins |

## Code Improvements

This version includes several optimizations over the original:

### Configuration
- ✅ **All timeouts configurable** - Single location at top of file
- ✅ **Increased timeouts** - Better reliability for weak WiFi (90s WiFi, 45s download)
- ✅ **Easy customization** - Adjust values without hunting through code

### Security
- ✅ Credentials moved to separate `config.h` file
- ✅ `.gitignore` prevents credential leaks

### Power Efficiency
- ✅ WiFi auto-disconnect after download (~50-80mA savings)
- ✅ Exponential backoff on errors (5→10→20→40→80→200 min max)

### Robustness
- ✅ **Improved download logic** - Removed duplicate HTTP connections
- ✅ **Size validation** - Detects partial downloads and retries
- ✅ **Centralized error handling** - `logHTTPError()` and `getErrorDetails()` helpers
- ✅ **Enhanced error display** - Shows detailed error info with WiFi signal strength
- ✅ Multiple retry attempts (5x API, 3x download)
- ✅ Dynamic dimension validation

### Code Quality
- ✅ **Cleaner code** - Removed ~50 lines of redundant/dead code
- ✅ **No duplicate logic** - Error logging centralized
- ✅ **Better structure** - Configuration section, helper functions
- ✅ Removed obsolete `showMultiline()` function (19 lines)
- ✅ Removed unnecessary WiFi reconnect logic
- ✅ Better error messages throughout

## Contributing

Contributions are welcome! Please:

1. Fork the repository
2. Create a feature branch
3. Make your changes
4. Submit a pull request

## License

MIT License - See LICENSE file for details

## Credits

- **Display:** Waveshare 7.3" ACeP E-Ink Display
- **Library:** GxEPD2 by ZinggJM
- **Hardware:** DFRobot FireBeetle 2 ESP32-C6
- **Author:** [Your Name]

## Support

For issues and questions:
- 📝 Open an issue on GitHub
- 📧 Contact: [your-email]

---

**Last Updated:** 2025
**Version:** 2.0
