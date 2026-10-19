# Setup and hardware notes

This document describes the **configuration used in the included sketch**, not a verified wiring diagram of the photographed prototype.

## Main components

- ESP32 development board with BLE support
- MPU6050 six-axis accelerometer / gyroscope module
- Battery holder, appropriate power regulator, half-size breadboard
- Glove and jumper wires

## I²C signal connections

The firmware defines the following ESP32 pins:

| MPU6050 module signal | ESP32 pin in sketch |
| --- | --- |
| SDA | GPIO 21 |
| SCL | GPIO 22 |

The sketch checks MPU6050 I²C addresses **0x68** and **0x69**. The sensor and ESP32 must share ground. **Verify your MPU6050 breakout board's voltage requirements and the regulator output before connecting power.** Battery wiring and regulator model are not established from the code.

## Arduino IDE dependencies

Install/select:

1. **ESP32 Arduino board support** for your ESP32 variant.
2. **Adafruit MPU6050** library (`Adafruit_MPU6050.h`).
3. **Adafruit Unified Sensor** (`Adafruit_Sensor.h`), plus any dependencies requested by the library manager.
4. **ESP32 BLE Mouse** library providing `BleMouse.h` (check compatibility with your ESP32 board-core version).
5. `Wire.h` is provided by the Arduino platform.

## Firmware configuration

Editable constants near the top of `Wireless_Mouse_Glove.ino`:

| Constant | Default | Meaning |
| --- | --- | --- |
| `CLICK_AXIS` | `0` | Gyroscope X for click gesture |
| `SCROLL_AXIS` | `1` | Gyroscope Y for scroll gesture |
| `SNAP_TRIGGER_DEG` | `70.0` | Minimum rapid rotation angle in degrees |
| `SNAP_START_RATE_DPS` | `155.0` | Angular speed required to start a quick gesture (degrees/sec) |
| `SNAP_MAX_DURATION_MS` | `630` | Maximum gesture detection window (ms) |
| `REARM_NEUTRAL_DEG` | `23.0` | Tilt threshold to allow re-arming |
| `CURSOR_SENSITIVITY` | `2.2` | Relative pointer movement multiplier |
| `POINTER_SWAP_XY` | `false` | Swap horizontal/vertical cursor axes |
| `POINTER_X_DIRECTION` / `POINTER_Y_DIRECTION` | `+1` | Reverse a cursor axis by setting `-1` |
| `POSITIVE_CLICK_IS_LEFT` | `true` | Choose which click corresponds to positive rotation |
| `POSITIVE_SCROLL_IS_UP` | `true` | Choose which scroll direction corresponds to positive rotation |

**To try the originally described ~90° quick-rotation threshold**, set `SNAP_TRIGGER_DEG` to `90.0f`, but check that the duration and speed thresholds still allow comfortable detection. This has not been verified on hardware.

## Troubleshooting

- **MPU6050 not found:** check SDA/SCL pins, I²C address, module power, and common ground. Monitor Serial at **115200 baud**.
- **No Bluetooth mouse:** confirm the ESP32 variant supports BLE and that the BLE Mouse library is compatible with the installed Arduino ESP32 board package.
- **Wrong cursor direction:** adjust pointer direction and swap constants.
- **Incorrect click/scroll direction:** change `POSITIVE_CLICK_IS_LEFT` or `POSITIVE_SCROLL_IS_UP`.
- **Missed quick gestures:** check sensor orientation and adjust gesture rate/angle thresholds cautiously.
- **Unexpected pointer drift:** start with the glove still during calibration; try tuning the dead zone and sensor mounting.
