#include <M5Unified.h>

static M5Canvas canvas(&M5.Display);
static uint8_t currentRotation = 3;
static unsigned long lastImuCheck = 0;

void setup(void)
{
  M5.begin();
  Serial.begin(115200);

  // HAT UART: RX=G26, TX=G0
  Serial2.begin(115200, SERIAL_8N1, 26, 0);

  M5.Display.setRotation(currentRotation);

  int w = M5.Display.width();
  int h = M5.Display.height();

  canvas.createSprite(w, h);
  canvas.setFont(&fonts::Font0);
  canvas.setTextSize(1);
  canvas.setTextColor(TFT_WHITE, TFT_BLACK);
  canvas.setTextScroll(true);
  canvas.println("UART Display ready");
  canvas.println("115200 8N1 RX=G26 TX=G0");
  canvas.pushSprite(0, 0);
}

void loop(void)
{
  M5.update();

  // Read UART data and display
  bool dirty = false;
  while (Serial2.available()) {
    char c = Serial2.read();
    canvas.print(c);
    Serial.print(c); // echo to USB serial for debug
    dirty = true;
  }
  if (dirty) {
    canvas.pushSprite(0, 0);
  }

  // IMU-based rotation check every 500ms
  unsigned long now = millis();
  if (now - lastImuCheck >= 500) {
    lastImuCheck = now;

    M5.Imu.update();
    auto data = M5.Imu.getImuData();

    // ~0.7g threshold ≈ 45 degrees from vertical; ignore when near horizontal
    uint8_t newRotation = currentRotation;
    if (data.accel.x > 0.7f) {
      newRotation = 1;
    } else if (data.accel.x < -0.7f) {
      newRotation = 3;
    }
    if (newRotation != currentRotation) {
      currentRotation = newRotation;
      M5.Display.setRotation(currentRotation);
      canvas.pushSprite(0, 0);
    }
  }

  M5.delay(1);
}

#if !defined(ARDUINO) && defined(ESP_PLATFORM)
extern "C" {
  void loopTask(void*)
  {
    setup();
    for (;;) {
      loop();
    }
    vTaskDelete(NULL);
  }

  void app_main()
  {
    xTaskCreatePinnedToCore(loopTask, "loopTask", 8192, NULL, 1, NULL, 1);
  }
}
#endif
