#include <Arduino_LSM9DS1.h>

unsigned long previousMillis = 0;
const long interval = 10; // 100Hz cravado

void setup() {
  Serial.begin(115200);
  
  // Aguarda a porta serial conectar (ideal para estabilidade inicial no PC)
  while (!Serial);

  if (!IMU.begin()) {
    Serial.println("Falha ao inicializar o IMU!");
    while (1);
  }
  
  Serial.println("Arduino pronto e enviando dados!");
}

void loop() {
  unsigned long currentMillis = millis();

  if (currentMillis - previousMillis >= interval) {
    previousMillis += interval; // Mantém a cadência perfeita sem atrasos

    float ax, ay, az, gx, gy, gz;

    if (IMU.accelerationAvailable() && IMU.gyroscopeAvailable()) {
      IMU.readAcceleration(ax, ay, az);
      IMU.readGyroscope(gx, gy, gz);

      // Formata: timestamp,ax,ay,az,gx,gy,gz
      char buffer[80];
      snprintf(buffer, sizeof(buffer), "%lu,%.2f,%.2f,%.2f,%.2f,%.2f,%.2f", currentMillis, ax, ay, az, gx, gy, gz);
      
      // Envia direto pelo cabo USB
      Serial.println(buffer);
    }
  }
}