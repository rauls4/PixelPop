#include <ESP32-HUB75-MatrixPanel-I2S-DMA.h>

MatrixPanel_I2S_DMA *panel = nullptr;

void setup() {
  Serial.begin(115200);
  delay(250);
  Serial.println("Starting HUB75 color test");

  HUB75_I2S_CFG config(64, 32, 1);
  panel = new MatrixPanel_I2S_DMA(config);
  if (!panel || !panel->begin()) {
    Serial.println("HUB75 initialization failed");
    while (true) delay(1000);
  }
  panel->setBrightness8(255);
}

void loop() {
  const uint16_t colors[] = {
    panel->color565(255, 0, 0),
    panel->color565(0, 255, 0),
    panel->color565(0, 0, 255),
    panel->color565(255, 255, 255)
  };

  for (uint16_t color : colors) {
    panel->fillScreen(color);
    delay(1500);
  }
}
