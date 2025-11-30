// Hello World ULTRA SIMPLE - ESP32-C6
// Sin librerías externas - Solo Serial y LED

constexpr uint8_t LED_PIN = 15;  // LED interno FireBeetle 2 ESP32-C6

void setup() {
  delay(2000);
  Serial.begin(115200);
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, HIGH);

  Serial.println("=== HELLO WORLD TEST ===");
  Serial.println("ESP32-C6 FireBeetle 2");
  Serial.println("LED blink starting...");

  // Parpadear LED 5 veces
  for(int i = 0; i < 5; i++) {
    Serial.println("Blink ");
    Serial.println(i + 1);
    digitalWrite(LED_PIN, HIGH);
    delay(200);
    digitalWrite(LED_PIN, LOW);
    delay(200);
  }

  Serial.println("Test completado!");
}

void loop() {
  // Nada
}
