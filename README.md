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
arduino-cli compile --fqbn esp32:esp32:esp32 --export-binaries /home/zoid/clone/m5stack/ButtonExample

# With local library
arduino-cli compile --fqbn esp32:esp32:esp32 --export-binaries --library ./M5Unified --library ./M5GFX /home/zoid/clone/m5stack/ButtonExample

esptool --chip esp32 --port /dev/ttyUSB0 --baud 115200 write-flash 0x0 /home/zoid/clone/m5stack/ButtonExample/build/esp32.esp32.esp32/ButtonExample.ino.merged.bin
```

Flashing

```
arduino-cli board list
```
