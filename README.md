# Firmware Exploration for M5Stack

- https://docs.m5stack.com/en/core/m5stickc_plus
  - ESP32-PICO-D4 (No USB)
  - Flash via esptool

One time

```
arduino-cli core list
arduino-cli config init --overwrite && arduino-cli config add board_manager.additional_urls "https://espressif.github.io/arduino-esp32/package_esp32_index.json"
arduino-cli core update-index
arduino-cli core install esp32:esp32
arduino-cli lib install M5Unified
```

Building

```
arduino-cli compile --fqbn esp32:esp32:esp32 --export-binaries ButtonExample
arduino-cli compile --fqbn esp32:esp32:esp32 --export-binaries UartDisplay

# With local library
arduino-cli compile --fqbn esp32:esp32:esp32 --export-binaries --library ./M5Unified --library ./M5GFX ButtonExample
```

## UartDisplay

Scrolling UART console for M5StickC Plus. Reads text from a serial device connected to the HAT pins (RX=G26, TX=G0) at 115200 baud and displays it on screen as a scrolling log.

- Auto-rotate: Landscape orientation that flips 180 degrees based on IMU, with a 45-degree deadzone to prevent wobble when held flat.
- Scroll control: BtnA (front) pauses/resumes auto-scroll. When paused, BtnB and BtnPWR (side buttons) scroll up/down one line. Button mapping swaps with screen rotation so top always scrolls up. Resuming jumps back to the latest output.
- Line wrapping: Long lines without newlines are automatically wrapped at screen width.
- PORT80 badge: Lines matching `PORT80: XXXX` are parsed and the latest hex code is shown as a white-on-blue badge in the bottom-right corner.

Flashing

```
arduino-cli board list
esptool --chip esp32 --port /dev/ttyUSB0 --baud 115200 write-flash 0x0 ButtonExample/build/esp32.esp32.esp32/ButtonExample.ino.merged.bin
esptool --chip esp32 --port /dev/ttyUSB0 --baud 115200 write-flash 0x0 UartDisplay/build/esp32.esp32.esp32/UartDisplay.ino.merged.bin
```
