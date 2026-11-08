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

## Keys (live)
| Key | Main screen | In the menu |
|---|---|---|
| UP | next chart zoom (5 s, 5 min, 2 h, 12 h per division) | previous item / change value |
| DOWN | next chart quantity (CO2, RH, temperature) | next item / change value |
| MENU short | open menu | next item, after the last one leave; cancel an edit |
| MENU long (6 s) | calibrate temperature and humidity offsets | - |
| ENTER | - | open item / store the edited value |
| ENTER long (6 s) | factory reset (defaults) | in CALI: start the 250 s CO2 calibration countdown |
| UP + DOWN, 1 s | toggle dark mode (LEDs dimmed by software PWM, duty in `src/leds.c`) | |

Menu items: 1 ALARM (UP/DOWN on/off), 2 PLANT (preset 0..9), 3 LOG (UP/DOWN select a sample, blinking in the chart, ENTER next quantity),
4 CALI, 5 degC/degF, 6 ADV (low/high CO2 threshold of preset 0 and 1, steps of 10 ppm).
The first key on a dark screen only wakes the backlight. 60 s without a key leaves the menu.

## Storage
Settings and the chart history of the 5 min, 2 h and 12 h levels are kept with NVS in the last 32 KB of flash (`storage_partition`, 0x08038000, board dts).
Needed in `prj.conf`: `CONFIG_FLASH`, `CONFIG_FLASH_MAP`, `CONFIG_NVS`, `CONFIG_MPU_ALLOW_FLASH_WRITE`.

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
