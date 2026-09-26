#include "../../src/core/Display.h"
#include "../../src/core/Display.cpp"

void setup() {
  Serial.begin(115200);
  delay(250);
  Serial.println("Starting PixelPop Canvas test");

  displayBegin(255, 0);
  displaySetRotation(0);
}

void loop() {
  const uint16_t colors[] = {
    gDisplay->color565(255, 0, 0),
    gDisplay->color565(0, 255, 0),
    gDisplay->color565(0, 0, 255),
    gDisplay->color565(255, 255, 255)
  };

  for (uint16_t color : colors) {
    gDisplay->fillScreen(color);
    delay(1500);
  }
}
