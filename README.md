# CO-110 Zephyr firmware

Freestanding application with an out-of-tree board (`boards/voltcraft/co110`).
`CMakeLists.txt` adds the board root, so no change to the Zephyr tree is needed.
Target: Zephyr 4.5.99, Windows, ST-Link + STM32CubeProgrammer.

## Build and flash

    cd C:\path\to\zephyrproject            (workspace, venv activated)
    west build -b co110 C:\path\to\co110-zephyr -p auto
    set PATH=%PATH%;C:\Program Files\STMicroelectronics\STM32Cube\STM32CubeProgrammer\bin
    west flash --runner stm32cubeprogrammer

Restore the original firmware at any time (keep `co110_flash_full.bin`!):

    STM32_Programmer_CLI -c port=SWD -d co110_flash_full.bin 0x08000000 -v -rst

## Applications (Kconfig choice, default = live)
* **live** (`src/live.c`, `src/ui.c`): the firmware.
  CM1106S CO2 (USART1) and SHT30 (I2C2, standard Zephyr `sensirion,sht3xd` driver) on the LCD,
  17-sample chart (5 s steps), zone LEDs, repeating alarm beep, key click, 63 s backlight time-out,
  37 s start-up sequence (30 s countdown), menu and settings in flash.
* **demo** (`src/demo.c`): synthetic screen to check the drawing layer: `-- -DCONFIG_APP_DEMO=y`
* **bit-map test** (`src/bitmap_test.c`): lights buffer bits one by one: `-- -DCONFIG_APP_BITMAP_TEST=y`
  (use `-p always` when switching the application).

## Architecture (live)
* UI thread = main (`src/live.c`, `src/ui.c`): waits for messages (`k_msgq`, `src/app.h`) with a 100 ms time-out;
  owns the display, the UI state, the settings and the history; redraws the LCD only on change.
* State machines (Zephyr SMF): `src/ui.c` (hierarchical: all screens have the parent `S_SCREEN` that handles
  MENU = leave and the 60 s time-out) and `src/startup.c` (the 37 s start-up sequence, one state per step).
* Sensor threads (`src/sensor_tasks.c`): SHT30 every 4 s, CM1106S every 5 s through the Zephyr sensor API
  (power cycle after 40 failed polls with `pm_device_action_run()` suspend/resume of the driver,
  400 ppm calibration on request of the UI with `SENSOR_ATTR_CALIBRATION`).
* Keys (`src/input.c`): Zephyr input subsystem (`gpio-keys` + `zephyr,input-longpress`); callbacks post `MSG_KEY` / `MSG_COMBO`, UP/DOWN auto repeat and the UP+DOWN chord use delayable work items.
* Timers: 63 s backlight, 1.1 s alarm beep period; both post messages to the UI thread.

## Source files (live)
| File | Content |
|---|---|
| `live.c`, `app.h` | UI thread (main), message queue and messages, timers |
| `ui.c/.h`, `startup.c/.h` | SMF state machines: screens, menu, edit flows; 37 s start-up sequence |
| `sensor_tasks.c`, `sht30.c`, `cm1106s.c/.h` | sensor threads, SHT3xD wrapper, CM1106S driver (binding `dts/bindings/cubic,cm1106s.yaml`) |
| `input.c` | touch keys: input subsystem callbacks, auto repeat, UP+DOWN chord |
| `alarm.c/.h`, `leds.c/.h`, `led_swpwm.c`, `backlight.h` | alarm zones and thresholds, LED logic, software-PWM LED driver, backlight |
| `display.c/.h`, `lcd.c/.h`, `lcd_map.h` | drawing layer, LCD bit-bang driver, measured segment map |
| `settings.c/.h`, `history.c/.h` | settings subsystem (NVS), chart history |
| `watchdog.c/.h` | task watchdog helpers |
| `tools/check_names.py` | name clash audit, see below |

## Keys (live)
| Key | Main screen | In the menu |
|---|---|---|
| UP | next chart zoom (5 s, 5 min, 2 h, 12 h per division) | previous item / change value |
| DOWN | next chart quantity (CO2, RH, temperature) | next item / change value |
| MENU short | open menu | next item, after the last one leave; cancel an edit |
| MENU long (6 s) | calibrate temperature and humidity offsets | - |
| ENTER | - | open item / store the edited value |
| ENTER long (6 s) | factory reset (defaults) | in CALI: start the 250 s CO2 calibration countdown |
| UP + DOWN, 1 s | toggle dark mode (LEDs dimmed, key click and start-up beep off, the alarm beep stays; software PWM, duty in `src/leds.c`; the setting is stored in flash) | |

Menu items: 1 ALARM (UP/DOWN on/off), 2 PLANT (preset 0..9), 3 LOG (UP/DOWN select a sample, blinking in the chart, ENTER next quantity),
4 CALI, 5 degC/degF, 6 ADV (low/high CO2 threshold of preset 0 and 1, steps of 10 ppm).
The first key on a dark screen only wakes the backlight. 60 s without a key leaves the menu.

## Hardware abstraction
* LEDs: Zephyr LED API (`led_set_brightness`), driver `src/led_swpwm.c` (software PWM, binding in
  `dts/bindings/`, node `leds` in the board dts). Dark mode = 12 % brightness (`DARK_PCT` in `src/leds.c`).
* Buzzer: Zephyr buzzer API with the `gpio-buzzer` node `buzzer` (active buzzer on PC1).
* LCD: `lcd_flush_if_changed()` sends the frame buffer only when it changed (plus a refresh every 10 s).
* `prj.conf` needs `CONFIG_LED=y` and `CONFIG_BUZZER=y`.
* `dts/bindings/vendor-prefixes.txt` registers the vendor prefix `voltcraft` (silences the devicetree warning).
* The CMake warning "No SOURCES given to Zephyr library: drivers__led" is harmless: the LED driver is in the application, not in Zephyr's drivers/led.

## Storage
Settings and the chart history of the 5 min, 2 h and 12 h levels are kept with the Zephyr settings subsystem (NVS backend, one key per item under `co110/`) in the last 32 KB of flash (`storage_partition`, 0x08038000, board dts).
Needed in `prj.conf`: `CONFIG_FLASH`, `CONFIG_FLASH_MAP`, `CONFIG_NVS`, `CONFIG_SETTINGS`, `CONFIG_SETTINGS_NVS`, `CONFIG_MPU_ALLOW_FLASH_WRITE`.

Board peripherals as standard Zephyr devices: LEDs (LED API, `led_swpwm.c`), buzzer (buzzer API), LCD backlight (`gpio-leds` node, `backlight.h`), CM1106S (own sensor driver `src/cm1106s.c` + binding `dts/bindings/cubic,cm1106s.yaml`: sensor API `SENSOR_CHAN_CO2`, calibration via `SENSOR_ATTR_CALIBRATION` (400 to 1500 ppm), ABC settings, serial number and software version via `src/cm1106s.h`, protocol from the datasheet CM1106S-W V0.1; its `vin-supply` is the `regulator-fixed` node, switched by PM resume/suspend), PC13 and PA2 (`gpio-hog` nodes in the board dts, purpose unknown). The touch keys use the input subsystem (`gpio-keys` node `keys` + `zephyr,input-longpress` node `longpress` in the board dts, callbacks in `src/input.c`; UP/DOWN auto repeat and the UP+DOWN chord are application code). The LCD bit-bang pins are still plain GPIOs in the `zephyr,user` node.

## Project rule: no name clashes with Zephyr
Zephyr headers declare many global names (for example `settings_save`, `wdt_feed`). Public application functions and
globals therefore get the prefix `co110_` (or the module name `cm1106s_`, `sht30_`), and every new name is checked
before it is used. `tools/check_names.py` lists all public application names that also appear in the Zephyr tree
or the STM32 HAL headers (no output = no hit):

    python tools/check_names.py                       (uses %ZEPHYR_BASE%)
    python tools/check_names.py C:\path\to\zephyr C:\path\to\modules\hal\stm32

Run it after adding names. Names that are only `static` inside a `.c` file are not a problem.

## Project rule: debug in sleep
Every firmware on this board must allow debugging in sleep, because NRST is not reachable and
a locked-out chip can only be recovered via BOOT0. Verified on the device: `CONFIG_DEBUG=y` and
`CONFIG_PM_DEVICE=y` in `prj.conf`. Never remove these two lines.

## If the build complains
* include `st/l4/stm32l431r(b-c)tx-pinctrl.dtsi` not found: list the names with
  `dir %ZEPHYR_BASE%\dts\arm\st\l4\stm32l431*` and fix the include in `co110.dts`.
* anything else: send the first error lines.

## If the ST-Link cannot connect any more ("Unable to get core ID")
Nothing is damaged; the original firmware is in `co110_flash_full.bin`.
1. Check the wires (SWDIO PA13, SWCLK PA14, GND, 3V3) and power-cycle the device.
2. Tie the BOOT0 pin (pin 60) to 3V3, power-cycle, then
   `STM32_Programmer_CLI -c port=SWD freq=480 -e all`, remove the wire, power-cycle and flash again.
3. If NRST were ever wired to the ST-Link (pin 7): `STM32_Programmer_CLI -c port=SWD mode=UR reset=HWrst freq=480 -e all`.

## Watchdog
Zephyr task watchdog (`src/watchdog.c`) on the IWDG (`&iwdg` enabled in the board dts, `CONFIG_WATCHDOG`,
`CONFIG_TASK_WDT`, `CONFIG_TASK_WDT_HW_FALLBACK`). The UI thread (3 s), the SHT30 thread (15 s) and the
CM1106S thread (20 s) each feed their own channel once per loop; a stuck thread or a hung system resets the device.

## Unit tests (`tests/logic`)
Alarm zones, the UI state machine and the start-up sequence run without hardware (the display, settings,
history and LED modules are replaced by stubs). native_sim needs Linux, so use WSL:

    west build -b native_sim tests/logic -p auto -t run

or run them on the nucleo board: `west build -b nucleo_h723zg tests/logic -p auto` and `west flash`
(the results are printed on its serial console).
