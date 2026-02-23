#include <M5Unified.h>

static M5Canvas canvas(&M5.Display);
static uint8_t currentRotation = 1;
static unsigned long lastImuCheck = 0;
static unsigned long lastUartRx = 0;
static const unsigned long IDLE_TIMEOUT_MS = 60000;

// Line buffer (circular)
static const int MAX_LINES = 200;
static String lines[MAX_LINES];
static int lineHead = 0;      // next write position
static int storedLines = 0;   // number of complete lines stored
static String currentLine;    // line currently being received

// PORT80 tracking
static String lastPort80;

// Scroll state
static int linesPerScreen = 0;
static int charsPerLine = 0;
static int fontH = 0;
static bool autoScroll = true;
static int viewOffset = 0;    // 0 = showing latest, >0 = scrolled up

// ANSI escape sequence parsing state
static bool inEscape = false;     // currently inside ESC sequence
static bool inCSI = false;        // inside CSI (ESC [) sequence
static char csiCmd = 0;           // stores CSI command letter for interpretation

// Check if character is a CSI final byte (cursor/erase commands)
static bool isCSIFinalByte(char c)
{
  // Common CSI final bytes: A(up) B(down) C(forward) D(back) J(erase) K(erase line) H(position)
  return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z');
}

// Clean up line by removing prompts and broken escape sequences
static String cleanLine(const String& line)
{
  String s = line;

  // Remove EC prompt (may appear multiple times due to corruption)
  while (s.indexOf("ec:~> ") >= 0) {
    s.replace("ec:~> ", "");
  }
  while (s.indexOf("ec:~>") >= 0) {
    s.replace("ec:~>", "");
  }

  // Remove broken escape sequences (ESC byte lost, left with [nX patterns)
  // Common: [6D [7D [J [1A [nC etc.
  int i = 0;
  String clean;
  while (i < (int)s.length()) {
    // Check for stray CSI final byte followed by [ (from split sequences like "D[J...")
    // But preserve "![" as it's a valid message prefix
    if (i == 0 && isCSIFinalByte(s[i]) && i + 1 < (int)s.length() && s[i + 1] == '[') {
      i++;  // Skip the stray final byte
      continue;
    }

    // Check for broken CSI sequence starting with [
    if (s[i] == '[' && i + 1 < (int)s.length()) {
      int j = i + 1;
      // Skip digits and semicolons (CSI parameters)
      while (j < (int)s.length() && ((s[j] >= '0' && s[j] <= '9') || s[j] == ';')) {
        j++;
      }
      if (j >= (int)s.length()) {
        // Incomplete sequence at end of string (e.g., "[6") - remove it
        i = j;
        continue;
      }
      char finalChar = s[j];

      // [digits. = timestamp like [56752.021700...], keep it
      if (finalChar == '.') {
        clean += s[i];
        i++;
        continue;
      }

      // [digits letter = broken CSI (e.g., [6D, [J, [1A), remove it
      if (isCSIFinalByte(finalChar)) {
        i = j + 1;
        continue;
      }

      // [digits then other garbage (e.g., "[55984+") - remove [digits portion
      if (j > i + 1) {
        i = j;
        continue;
      }
    }
    clean += s[i];
    i++;
  }

  // Remove stray CSI final bytes that appear after cleanup
  // Pattern: single letter at start followed by [digit (from split [6D sequences)
  while (clean.length() >= 2 && isCSIFinalByte(clean[0]) && clean[1] == '[') {
    clean = clean.substring(1);
  }

  clean.trim();
  return clean;
}

// Check if character is a hex digit
static bool isHexDigit(char c)
{
  return (c >= '0' && c <= '9') || (c >= 'A' && c <= 'F') || (c >= 'a' && c <= 'f');
}

// Extract PORT80 value from raw line (before sanitization can damage it)
// Returns true if found, value will be 4 or 8 hex chars
static bool extractPort80(const String& line, String& value)
{
  int idx = line.indexOf("PORT80:");
  if (idx < 0) return false;

  // Skip "PORT80:" and any whitespace
  int start = idx + 7;
  while (start < (int)line.length() && (line[start] == ' ' || line[start] == '\t')) {
    start++;
  }

  // Collect hex digits (expect 4 or 8)
  String hex;
  for (int i = start; i < (int)line.length() && isHexDigit(line[i]); i++) {
    hex += line[i];
    if (hex.length() >= 8) break;  // max 4 bytes = 8 hex chars
  }

  // Valid if 4 hex chars (2 bytes) or 8 hex chars (4 bytes)
  if (hex.length() == 4 || hex.length() == 8) {
    value = hex;
    return true;
  }
  return false;
}

static void addCompleteLine(const String& line)
{
  // Extract PORT80 from raw line FIRST (before sanitization can damage it)
  String port80Value;
  if (extractPort80(line, port80Value)) {
    lastPort80 = port80Value;
  }

  String cleaned = cleanLine(line);
  if (cleaned.length() == 0) return;  // skip empty lines

  lines[lineHead] = cleaned;
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

  // PORT80 badge in bottom-right corner
  if (lastPort80.length() > 0) {
    int w = canvas.width();
    int h = canvas.height();
    int textW = lastPort80.length() * canvas.textWidth("A");
    int pad = 2;
    int boxW = textW + pad * 2;
    int boxH = fontH + pad * 2;
    canvas.fillRect(w - boxW, h - boxH, boxW, boxH, TFT_BLUE);
    canvas.setTextColor(TFT_WHITE, TFT_BLUE);
    canvas.setCursor(w - boxW + pad, h - boxH + pad);
    canvas.print(lastPort80);
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

  // HAT UART: RX=G26, TX=G0 (4 KiB RX buffer to avoid drops during rendering)
  Serial2.begin(115200, SERIAL_8N1, 26, 0, false, 4096);

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

  lastUartRx = millis();
  addCompleteLine("UART Display ready");
  addCompleteLine("115200 8N1 RX=G26 TX=G0");
  renderScreen();
}

void loop(void)
{
  M5.update();

  // Read UART data into buffer, stripping ANSI escape sequences
  bool dirty = false;
  while (Serial2.available()) {
    lastUartRx = millis();
    char c = Serial2.read();

    // ANSI escape sequence state machine
    if (inEscape) {
      if (inCSI) {
        // Inside CSI sequence: wait for final byte (0x40-0x7E)
        if (c >= 0x40 && c <= 0x7E) {
          csiCmd = c;
          inEscape = false;
          inCSI = false;
          // Interpret [J (erase display/line) as "new message"
          // EC pattern: ec:~> [6D[J[message]
          // Commit current content (cleanup happens in addCompleteLine)
          if (csiCmd == 'J') {
            if (currentLine.length() > 0) {
              addCompleteLine(currentLine);
              dirty = true;
            }
            currentLine = "";
          }
        }
        // Otherwise keep consuming CSI parameter bytes
      } else {
        // Just saw ESC, check for CSI introducer '['
        if (c == '[') {
          inCSI = true;
        } else {
          // Non-CSI escape (e.g., ESC followed by single char)
          inEscape = false;
        }
      }
      continue;
    }

    // Start of escape sequence
    if (c == '\x1b') {
      inEscape = true;
      inCSI = false;
      continue;
    }

    // Normal character handling
    if (c == '\n' || c == '\r') {
      if (currentLine.length() > 0) {
        addCompleteLine(currentLine);
        currentLine = "";
      }
      dirty = true;
    } else if (c >= ' ' && c <= '~') {
      // Filter only printable ASCII characters (0x20–0x7E)
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

  // Power off after 1 minute of no UART activity (but not if paused)
  if (autoScroll && millis() - lastUartRx >= IDLE_TIMEOUT_MS) {
    M5.Display.clear();
    M5.Display.setCursor(0, 0);
    M5.Display.print("No UART — powering off");
    M5.delay(1000);
    M5.Power.powerOff();
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
