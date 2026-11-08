# CO-110 Zephyr project (step 2: display layer + demo; step 1 bit-map test still available)

Layout: freestanding application with an out-of-tree board (`boards/voltcraft/co110`).
`CMakeLists.txt` adds the board root, so no change to your Zephyr tree is needed.

## Build and flash (Windows, existing Zephyr 4.5.99 environment)

    cd C:\path\to\zephyrproject            (your workspace; activate its venv)
    west build -b co110 C:\path\to\co110-zephyr -p auto
    set PATH=%PATH%;C:\Program Files\STMicroelectronics\STM32Cube\STM32CubeProgrammer\bin
    west flash --runner stm32cubeprogrammer

Restore the original firmware at any time (keep co110_flash_full.bin!):

    STM32_Programmer_CLI -c port=SWD mode=UR -d co110_flash_full.bin 0x08000000 -v -rst

## If the build complains
* include `st/l4/stm32l431r(b-c)tx-pinctrl.dtsi` not found: list the names with
  `dir %ZEPHYR_BASE%\dts\arm\st\l4\stm32l431*` and fix the include in `co110.dts`.
* anything else: send me the first error lines.

## Applications in one project (Kconfig choice, default = live)
* default: **live display** (`src/live.c`): CM1106S CO2 (USART1) and SHT30 temperature/humidity (I2C2) on the LCD,
  chart of the last 17 samples (5 s steps), zone LEDs, 37 s start-up sequence (30 s countdown),
  dark mode (UP + DOWN held for 1 s, software PWM `src/leds.c`, duty in `DARK_DUTY`).
  DOWN: next chart quantity, MENU: degC/degF.
* demo: build with `-- -DCONFIG_APP_DEMO=y`; bit-map test: `-- -DCONFIG_APP_BITMAP_TEST=y` (use `-p always` when switching).

SHT30 uses Zephyr's standard `sensirion,sht3xd` driver (node `sht30` in `co110.dts`, `CONFIG_SENSOR=y`).

## Older notes (demo / test)
* default: **display demo** (`src/demo.c`): draws a main screen with synthetic data using the
  drawing layer `src/display.c` and the measured segment map `src/lcd_map.h`.
  UP zoom, DOWN quantity (CO2/RH/temp), MENU next underline, ENTER mute icon, ENTER long preset.
* LCD bit-map test (`src/bitmap_test.c`): build with `-- -DCONFIG_APP_BITMAP_TEST=y`
  (add `-p always` when switching):

      west build -b co110 C:\path\to\co110-zephyr -p always -- -DCONFIG_APP_BITMAP_TEST=y

## Using the bit-map test
See the comment at the top of `src/main.c` (keys, LED coding, beeps).
Result to record: for every lit segment on the glass, the (byte, bit) shown by the LEDs.

## Project rule: debug in sleep
Every firmware on this board must allow debugging in sleep. Verified working on the device:
`CONFIG_DEBUG=y` and `CONFIG_PM_DEVICE=y` in `prj.conf` (the ST-Link can flash again). `board.c` and
`co110_defconfig` carry additional safety nets. Never remove it: NRST is not reachable on the CO-110.

The board also keeps the CPU out of WFI with a lowest-priority spinning thread (`board.c`) and sets
`CONFIG_STM32_ENABLE_DEBUG_SLEEP_STOP=y` in `co110_defconfig`.

## If the ST-Link cannot connect any more ("Unable to get core ID")
Cause: the running Zephyr image sleeps (WFI) in its idle thread and the debug port is
not kept alive in sleep. The fixed firmware (`src/dbg.h`) enables debug in low-power modes,
but the image that is already on the chip has to be removed first. Nothing is damaged;
the original firmware is in co110_flash_full.bin.

1. Check wires (SWDIO PA13, SWCLK PA14, GND, 3V3) and power-cycle the device.
2. Only if NRST can be wired to the ST-Link (pin 7 of the MCU, not possible on the CO-110 so far):

       STM32_Programmer_CLI -c port=SWD mode=UR reset=HWrst freq=480 -e all

   then flash again (`west flash`).
3. If NRST is not accessible: tie the BOOT0 pin (pin 60) to 3V3, power-cycle, then

       STM32_Programmer_CLI -c port=SWD freq=480 -e all

   remove the BOOT0 wire, power-cycle and flash again.
