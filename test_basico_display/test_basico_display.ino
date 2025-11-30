// Test MÍNIMO para pantalla e-ink Waveshare 7.3" ACeP en FireBeetle 2 ESP32-C6
// Solo muestra pantalla blanca con texto negro "HELLO WORLD"

#include <GxEPD2_7C.h>
#include <Fonts/FreeMonoBold9pt7b.h>
#include <SPI.h>

// Configuración de la pantalla
#define GxEPD2_DISPLAY_CLASS GxEPD2_7C
#define GxEPD2_DRIVER_CLASS  GxEPD2_730c_GDEY073D46

// Pines FireBeetle 2 ESP32-C6
constexpr uint8_t PIN_CS   =  1;
constexpr uint8_t PIN_DC   =  8;
constexpr uint8_t PIN_RST  = 14;
constexpr uint8_t PIN_BUSY =  4;
constexpr uint8_t LED_PIN  = 15;

// SPI para ESP32-C6
// En ESP32-C6, FSPI es equivalente a SPI2_HOST
SPIClass epdSPI(FSPI);

// Display
#if defined(ESP32)
  #define MAX_DISPLAY_BUFFER_SIZE 65536ul
  #define MAX_HEIGHT(EPD) ((EPD::HEIGHT <= (MAX_DISPLAY_BUFFER_SIZE)/(EPD::WIDTH/2)) \
                              ? EPD::HEIGHT : ((MAX_DISPLAY_BUFFER_SIZE)/(EPD::WIDTH/2)))
  GxEPD2_DISPLAY_CLASS< GxEPD2_DRIVER_CLASS,
                        MAX_HEIGHT(GxEPD2_DRIVER_CLASS) >
    display(GxEPD2_DRIVER_CLASS(PIN_CS, PIN_DC, PIN_RST, PIN_BUSY));
#endif

void setup() {
  Serial.begin(115200);
  delay(1000);

  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, HIGH);  // LED ON durante prueba
  pinMode(PIN_BUSY, INPUT_PULLUP);

  Serial.println("\n\n=== TEST BASICO E-INK ===");
  Serial.println("FireBeetle 2 ESP32-C6");
  Serial.println("Waveshare 7.3\" ACeP");

  // Inicializar SPI
  Serial.println("\n1. Inicializando SPI...");
  delay(100);
  epdSPI.begin(23, 21, 22, PIN_CS);  // SCK, MISO, MOSI, CS
  display.epd2.selectSPI(epdSPI, SPISettings(4000000, MSBFIRST, SPI_MODE0));
  delay(100);
  Serial.println("   SPI OK");

  // Inicializar display
  Serial.println("\n2. Inicializando display...");
  display.init(115200);
  Serial.println("   Display OK");

  // Dibujar pantalla blanca con texto
  Serial.println("\n3. Dibujando en pantalla...");
  display.setRotation(0);
  display.setFullWindow();
  display.firstPage();

  do {
    display.fillScreen(GxEPD_WHITE);  // Fondo blanco

    // Texto centrado
    display.setFont(&FreeMonoBold9pt7b);
    display.setTextColor(GxEPD_BLACK);

    // Calcular centro
    const char* msg = "HELLO WORLD";
    int16_t bx, by;
    uint16_t bw, bh;
    display.getTextBounds(msg, 0, 0, &bx, &by, &bw, &bh);

    int x = (display.width() - bw) / 2 - bx;
    int y = (display.height() - bh) / 2 - by;

    display.setCursor(x, y);
    display.print(msg);

    // Indicador de versión en esquina
    display.setTextColor(GxEPD_RED);
    display.setCursor(10, 30);
    display.print("ESP32-C6 Test");

  } while (display.nextPage());

  Serial.println("   Dibujo completado!");

  // Apagar LED
  digitalWrite(LED_PIN, LOW);

  Serial.println("\n=== TEST COMPLETADO ===");
  Serial.println("Si ves 'HELLO WORLD' en la pantalla, el hardware funciona correctamente.");
  Serial.println("\nPantalla en modo hibernate - reinicia para volver a ejecutar.");

  // Poner display en hibernación
  display.hibernate();
}

void loop() {
  // Nada - solo ejecuta setup() una vez
}
