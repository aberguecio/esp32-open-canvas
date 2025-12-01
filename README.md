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
| BUSY        | GPIO4         | Busy Signal |
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
3. **Connect** to WiFi (15-second timeout)
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

## Error Handling

The system displays errors on the e-ink screen and retries after 1 minute.

**Error types:**

- ❌ **WiFi failed** - Cannot connect to network
- ❌ **API error** - Cannot fetch image URL
- ❌ **Download failed** - Image download timeout or error

All errors are also logged to Serial Monitor (115200 baud).

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

### Security
- ✅ Credentials moved to separate `config.h` file
- ✅ `.gitignore` prevents credential leaks

### Power Efficiency
- ✅ WiFi auto-disconnect after download (~50-80mA savings)
- ✅ Optimized sleep times (5 min normal, 1 min on error)

### Robustness
- ✅ 30-second HTTP timeout
- ✅ Error display on screen
- ✅ Automatic retry on errors
- ✅ Dynamic dimension validation

### Code Quality
- ✅ Removed dead code (USE_HSPI_FOR_EPD, centerText())
- ✅ Better error messages
- ✅ Cleaner structure

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
