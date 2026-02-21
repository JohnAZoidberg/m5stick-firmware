#include <M5Unified.h>

static M5Canvas canvas(&M5.Display);
static uint8_t currentRotation = 3;
static unsigned long lastImuCheck = 0;

// Line buffer (circular)
static const int MAX_LINES = 200;
static String lines[MAX_LINES];
static int lineHead = 0;      // next write position
static int storedLines = 0;   // number of complete lines stored
static String currentLine;    // line currently being received

// Scroll state
static int linesPerScreen = 0;
static int charsPerLine = 0;
static int fontH = 0;
static bool autoScroll = true;
static int viewOffset = 0;    // 0 = showing latest, >0 = scrolled up

static void addCompleteLine(const String& line)
{
  lines[lineHead] = line;
  lineHead = (lineHead + 1) % MAX_LINES;
  if (storedLines < MAX_LINES) storedLines++;
  if (!autoScroll) viewOffset++;
}

// Get stored line by absolute index (0 = oldest)
static String getStoredLine(int absIndex)
{
  if (absIndex < 0 || absIndex >= storedLines) return "";
  return lines[(lineHead - storedLines + absIndex + MAX_LINES) % MAX_LINES];
}

static void renderScreen()
{
  canvas.fillSprite(TFT_BLACK);
  // totalLines: all complete lines + the partial currentLine
  int totalLines = storedLines + 1;

  for (int row = 0; row < linesPerScreen; row++) {
    // Map screen row to line index (0 = oldest stored, storedLines = currentLine)
    int li = totalLines - linesPerScreen + row - viewOffset;
    String text;
    if (li >= 0 && li < storedLines) {
      text = getStoredLine(li);
    } else if (li == storedLines) {
      text = currentLine;
    }
    canvas.setCursor(0, row * fontH);
    canvas.print(text);
  }

  if (!autoScroll) {
    int w = canvas.width();
    canvas.setTextColor(TFT_BLACK, TFT_ORANGE);
    canvas.setCursor(w - 12, 0);
    canvas.print("||");
    canvas.setTextColor(TFT_WHITE, TFT_BLACK);
  }

  canvas.pushSprite(0, 0);
}

static int maxViewOffset()
{
  int totalLines = storedLines + 1;
  return totalLines > linesPerScreen ? totalLines - linesPerScreen : 0;
}

void setup(void)
{
  M5.begin();
  M5.BtnPWR.setHoldThresh(1024);
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

  fontH = canvas.fontHeight();
  linesPerScreen = h / fontH;
  charsPerLine = w / canvas.textWidth("A");

  addCompleteLine("UART Display ready");
  addCompleteLine("115200 8N1 RX=G26 TX=G0");
  renderScreen();
}

void loop(void)
{
  M5.update();

  // Read UART data into buffer
  bool dirty = false;
  while (Serial2.available()) {
    char c = Serial2.read();
    Serial.print(c); // echo to USB serial for debug
    if (c == '\n') {
      addCompleteLine(currentLine);
      currentLine = "";
      dirty = true;
    } else if (c != '\r') {
      currentLine += c;
      if ((int)currentLine.length() >= charsPerLine) {
        addCompleteLine(currentLine);
        currentLine = "";
      }
      dirty = true;
    }
  }
  if (dirty && autoScroll) {
    renderScreen();
  }

  // BtnA (front) = toggle auto-scroll
  if (M5.BtnA.wasClicked()) {
    autoScroll = !autoScroll;
    if (autoScroll) {
      viewOffset = 0;
    }
    renderScreen();
  }

  // Always drain button state (BtnPWR uses AXP192 IRQ — must be read every cycle)
  // AXP192 short press maps to state_hold in M5Unified, so check both
  bool btnBClicked = M5.BtnB.wasClicked();
  bool btnPWRClicked = M5.BtnPWR.wasClicked() || M5.BtnPWR.wasHold();

  // Side buttons: scroll up/down one line (only when paused)
  // Rotation swaps which physical side is top vs bottom
  if (!autoScroll) {
    bool scrollUpPressed = (currentRotation == 3) ? btnPWRClicked : btnBClicked;
    bool scrollDownPressed = (currentRotation == 3) ? btnBClicked : btnPWRClicked;

    if (scrollUpPressed && viewOffset < maxViewOffset()) {
      viewOffset++;
      renderScreen();
    }
    if (scrollDownPressed && viewOffset > 0) {
      viewOffset--;
      renderScreen();
    }
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
      renderScreen();
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
