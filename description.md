# Voltcraft CO-110 firmware – functional description

Source: `co110_disasm.c` (Ghidra export, HAL functions renamed, see `rename_map.txt`) plus the earlier software/hardware summaries.
Confidence: **[V]** read directly from code, **[I]** inferred, **[?]** unknown. Literal pool values (`DAT_xxxx`) are not in the export, so RAM/peripheral base addresses come from the earlier summary.

Status: **all 6 analysis stages done, gap-filling pass done (section 13), LCD segment map measured on the device (section 14)** (platform/boot/main loop, sensors and sampling, buttons/UI state machine, LCD driver, alarms/backlight/start-up/calibration, flash storage). Literal values were resolved from `co110_flash_full.bin`.

## 1. Platform and clocks

- STM32L431RCT6, Cortex-M4F, FPU enabled in `SystemInit` [V]. Standard CubeMX/HAL structure (HAL for L4) [V].
- `SystemClock_Config` [V]: oscillators LSI + MSI, MSI range value 0xA0 = **range 10 = 32 MHz**, SYSCLK = MSI, HCLK/APB1/APB2 dividers 1, flash latency 1, voltage scale 1. No PLL, no HSE, no LSE.
  RTC clock = LSI (value 0x200) [V].
- HAL tick: SysTick 1 kHz [V].
- Independent watchdog `MX_IWDG_Init` [V]: prescaler 4 (/64), reload and window 0xFFF → ≈ 8 s timeout (LSI ≈ 32 kHz). Refreshed many times per main-loop pass.

## 2. Peripherals configured at boot (CubeMX-style `MX_*_Init`)

| Init | Setup | Role |
|---|---|---|
| `MX_GPIO_Init` | GPIOA/B/C clocks | see 3 |
| `MX_USART1_UART_Init` | 9600 baud, 8N1, TX+RX (mode 0xC) | CM1106S CO2 sensor [V] |
| `MX_I2C2_Init` | I2C2 on PB10/PB11 (AF4, open drain, pull-up), analog filter on, digital filter 0 | SHT30 (reads use address byte 0x88 = 0x44 <<1, 6 bytes, timeout 0x3FF) [V] |
| `MX_RTC_Init` | async prescaler 0x7F, sync 0xFF (LSI/128/256 ≈ 1 Hz), 24 h | RTC only initialised (LSI), never used afterwards [V] |
| `MX_TIM1_Init` | PSC 31999, ARR 999, auto-reload preload → **1 Hz** update interrupt (TIM1_UP_TIM16, IRQ 25) | seconds time base: 60/30/300-s countdowns [V]/[I] |
| `MX_TIM2_Init` | PSC 31999, ARR 4 → **5 ms** update interrupt (IRQ 28) | button timing and short soft timers [V] |

Correction to the old summary: with MSI = 32 MHz the TIM2 tick is **5 ms**, not 3.3 ms (checked: MSI range field 0xA, flash latency 1, MSI table in the dump = 32 MHz). The 1200-tick long press is therefore **6 s**; this was **measured on the device** (ENTER hold until the reset beep = 6 s), which confirms the 32 MHz clock and the 5 ms tick; the manual's 4 s is wrong or simplified. Everything below is written in ticks, with times for the 32 MHz case.

## 3. GPIO configuration [V]

Clocks: AHB2ENR bits for GPIOA, GPIOB, GPIOC.

- Port C outputs (push-pull): PC1 (buzzer via S9013), PC2, PC3, PC7 (LCD DATA), PC8 (LCD WR), PC9 (LCD RD), PC13. Start low.
- Port A outputs: PA2 (start high), PA8 (LCD CS, start low).
- Port A inputs PA4-PA7: EXTI, falling edge (touch controller outputs: PA4 UP, PA5 DOWN, PA6 MENU, PA7 ENTER).
- Port B outputs: PB4-PB7 (start low) and PB12-PB15 (start high = LEDs off; active low: PB12 LD4 green, PB13 LD3 yellow, PB14 LD2 red, PB15 LD1 green/power).
- PB3: plain input, no pull.
- NVIC: EXTI4 (IRQ 10) and EXTI9_5 (IRQ 23) enabled, priority 0.
- Button ISRs: `EXTI4_IRQHandler` (PA4) and `EXTI9_5_IRQHandler` (PA5-PA7) only set "press started" flags; the HAL EXTI handler then clears the pending bits [V].

## 4. Boot sequence (`main`, 0x0800946C) [V]

1. `HAL_Init`, `SystemClock_Config`, `MX_GPIO_Init`, busy-wait ≈ 40 units (`Delay_Busy_ms(0x28)`; software loop, not HAL_Delay).
2. `MX_IWDG_Init` + refresh, `MX_USART1`, `MX_RTC`, `MX_TIM1`, `MX_TIM2`, `MX_I2C2`.
3. PC13 high. `FUN_08000930(1)`: PC2 high. `FUN_080090ac(1)`: PA2 low. Short delay. These are enable/power lines of the display or sensors (exact role unknown) [?].
4. `FUN_080090f8`: LCD bus initialisation: CS, RD, WR, DATA high, then a short command sequence through `FUN_08009258` (values 1, 3, 0x80, 4, 5 …) – the display chip's init commands [I]; decoded in stage 4.
5. `FUN_08000988` (application init): sets default variables (UI mode 5, state 2, 0x3C = 60 (s?), several flags), loads settings (`FUN_08006bfc`), sets mode byte 5, turns all four LEDs on (PB12-15 low), creates three data objects with `FUN_08007cb8(0..2)` (probably the three trend logs CO2/RH/TEMP) [I].
   - Settings (`FUN_08006bfc`): record id 0x7E, marker 0x8000; defaults 800 / 1200 ppm (human low/high alarm), 600 / 900 ppm (plant), rest 0; a flag byte is copied from settings offset 8 [V].

## 5. Main loop [V structure, I semantics]

```
for (;;) {
    refresh IWDG
    if (mode != 5) {                       // 5 = start-up countdown/animation?
        if (flag_1s_sensor) { flag=0; FUN_080047dc(); }   // sensor poll (SHT30 + CM1106S)
        refresh IWDG
        if (flag_B) { flag_B=0; if (!flag_C) FUN_08007bcc();   // periodic work, set by TIM1 ISR
                      refresh IWDG
                      if (mode != 5 && !flag_D) FUN_08006c64(); }
    }
    refresh IWDG
    FUN_08005480();   // button dispatcher / UI state machine
    FUN_080091a8();   // LCD refresh (stage 4)
    FUN_08005a28();
    refresh IWDG
    FUN_08005f74();
    FUN_08006b6c();   // status LEDs
    FUN_08006898();   // chart / log handling (writes flash)
}
```

- Flags are set by the TIM1 (1 Hz) and TIM2 (5 ms) interrupt routines (`FUN_0800833c`, `FUN_0800851c`) [V].
- Status LEDs (`FUN_08006b6c`) [V]: when the start-up flag is 0 and level = 0 → LD4 (green) on; level 1 → LD3 (yellow); level 2 → LD2 (red); the others off. LD1 (green/power) is on whenever mode ≠ 5.

## 6. Notes and open items for later stages

- `FUN_0800833c` / `FUN_0800851c` are the timer interrupt bodies without HAL_TIM_IRQHandler (the vector stubs are not separate functions in the export).
- USART1 receive is interrupt driven: a handler stores bytes into a 21-byte buffer (`FUN_08001aec`) and re-arms `HAL_UART_Receive_IT`.
- Flash is used as emulated EEPROM through `FUN_08001b2c/08001b88` (page erase + 64-bit programming) and `FUN_08007f9e/08007fb6` (record read/write).
- Not yet analysed: `FUN_08005480` and handlers, `FUN_080091a8`, `FUN_08005a28`, `FUN_08005f74`, `FUN_08006898`, `FUN_08007cb8`.

## 7. Time base and sampling schedule (stage 2)

`FUN_0800833c` (TIM1, **1 Hz**) [V]: four free-running counters set flags every 1, 2, 5 and 9 s; plus 60 s and 30 s countdowns, a 300 s timeout and a 3-state cycler advancing every 10 s while enabled (UI/animation use, stage 3).
Flag-to-task assignment (resolved from the literals in the dump) [V]: the **2 s flag** (0x200000DB) triggers the SHT30 task `FUN_080047dc`, the **5 s flag** (0x200000DC) triggers the CO2 poll `FUN_08007bcc` and the trend log `FUN_08006c64`. The 1 s flag (0x200000DA) and the 9 s flag (0x200000DD) are set but never read (dead). Because the SHT30 task alternates "start measurement" and "read" on successive calls, T/RH are refreshed every **4 s**; CO2 every **5 s**; the CO2 timeout is 40 polls = **200 s**.

### 7.1 SHT30 temperature/humidity (`FUN_080047dc`, I2C2, address 0x44) [V]

Two-phase state machine, one phase per call:
1. Phase 0: `HAL_I2C_Master_Transmit(0x88, cmd, 2 bytes)`; on failure retry up to 6 times with 1-unit busy delay. Success → phase 1. The two command bytes are **0x2C 0x06** (single shot, high repeatability, clock stretching enabled; initialised RAM data copied from flash 0x0800970C) [V].
2. Phase 1 (next call): `HAL_I2C_Master_Receive(0x88, buf, 6 bytes)`, same retry rule. Success → decode:
   - temperature raw = buf[0]·256 + buf[1]; converted with double-precision maths to an integer **tenths of °C: (raw/65535·175 − 45)·10** (constants 65535, 175, 45, 10 read from the dump) [V]; **a flag is set when the value is below −99** (−9.9 °C = implausible/sensor error) [I].
   - humidity raw = buf[3]·256 + buf[4] → **raw/65535·100** → integer % RH [V].
   - The two CRC bytes (buf[2], buf[5]) are **not checked**.
3. Bus-busy recovery: if I2C2 ISR BUSY bit (0x8000) is set at entry, a counter counts 5 calls, then `HAL_I2C_MspDeInit`, short delays, and `FUN_08004440` re-initialises PB10/PB11 (AF4 open-drain, pull-up) and I2C2 [I].
- Display value: `FUN_08008964(value, mode)` adds the stored temperature calibration offset (settings word at +0x18) and, for modes 1 and 2, converts °C→°F with double maths (mode 2 without offset) [I].

### 7.2 CM1106S CO2 sensor (`FUN_08007bcc` + `FUN_08000958`, USART1 9600 8N1) [V]

- Request (`FUN_08000958`, sent at the end of every call): re-arm `HAL_UART_Receive_IT` (1 byte), then blocking `HAL_UART_Transmit` of 4 bytes = `11 01 01 ED` (read CO2).
- Receive: each byte goes to a 21-byte buffer (`FUN_08001aec`, index wraps at 21) and the receive is re-armed.
- Parse (next call): scan the buffer for header `16 05 01` (response length 5, command 1); checksum = `0x100 − (sum of the first 7 bytes & 0xFF)` must equal byte 7. Valid frame → **CO2 ppm = byte3·256 + byte4**, timeout counter and error flag cleared. (A frame whose byte sum is 0 mod 256 would be rejected – negligible.)
- Timeout: counter incremented on each call; above 39 (≈ 40 polls ≈ 200 s if polled every 5 s) the error flag is set, a "sensor lost" flag is raised and **PC2 is driven low = CM1106S power/enable off** (`FUN_08000930(0)`); PC2 was set high at boot. What powers it back on and clears the error flag is in the UI code (stage 3/5) [?].
- The scan reads up to 7 bytes beyond the start index, so a frame wrapping the end of the buffer is not handled; the next request is simply used instead [I].

### 7.3 Trend log hierarchy (`FUN_08006c64`, `FUN_08006d68`) [V arithmetic, I meaning]

Called once per flag-B tick (5 s) while mode ≠ 5. It pushes the current sample (three values: CO2 ppm, humidity + humidity offset from settings +0x1C, temperature + temperature offset from settings +0x18, clamped at 0) into ring buffers of 17 columns × 3 values (`0x11, 3` = the 17 chart columns × CO2/temperature/RH) at four resolutions through nested counters:

| Counter | Wraps at | Then |
|---|---|---|
| base | 12 | next counter +1 (12 × 5 s = 1 min)
| c2 | 5 | **5 min** level (log index 2) written
| c3 | 12 | (1 h)
| c4 | 2 | **2 h** level (log index 4) written
| c5 | 6 | **12 h = ½ day** level (log index 5) written

and level 0 (**5 s**) written on every call. This reproduces the manual's zoom steps 5 s / 5 min / 2 h / ½ day per division [I]. `FUN_08008074(kind, …)` after the 5 min, 2 h and 12 h writes updates a per-level result (MIN/MAX of the visible chart is the likely use) [?]. Decompiler quirk: the base counter appears to be incremented twice per call; check the raw assembly before trusting the 1 min step.

## 8. Buttons and UI state machine (stage 3)

Variable names are my own labels for RAM addresses. Times assume TIM2 = 5 ms (see section 2).

### 8.1 Button capture (`EXTI*_IRQHandler` + TIM2 ISR `FUN_0800851c`) [V]

Touch controller outputs are active low. A falling edge (EXTI) arms a per-key "pressed" flag and clears the hold counter `held` (0x200000E0); the TIM2 tick then polls the pin while the flag is armed:

| Key (pin) | While held | On release | Events |
|---|---|---|---|
| UP (PA4), DOWN (PA5) | `held`++; after **120 ticks (0.6 s)** fire one event and start auto-repeat: one event every **8 ticks (40 ms)** | if `held` > 7 (>35 ms) fire one event (short press) | UP flag 0xCC, DOWN flag 0xCE; repeat-active flags 0xCD/0xCF |
| MENU (PA6), ENTER (PA7) | `held`++; at 1200 ticks (**6 s**, measured on the device) fire the "long" event | `held` > 7 → "short" event | MENU long 0xC8 / short 0xC9, ENTER long 0xCA / short 0xCB |

Other TIM2 duties: beep timing (below), 0.5 s blink flags (0x200000E2, 0x20000034), 50 ms and 105 ms flags (0xB0, 0xBE), a 100-tick (0.5 s) one-shot that **powers the CM1106S (PC2 high)** after boot, and a 50-tick (250 ms) window for the UP+DOWN gesture.
Beep: `FUN_080055f8` sets PC1 high (buzzer on) and arms a state machine; after 10 ticks (**50 ms**) PC1 goes low, then 30 ticks (150 ms) lockout. Every accepted key press calls it: one 50 ms beep per key.

### 8.2 Dispatcher `FUN_08005480` [V]

Runs each loop pass. For every event flag: clear it; if `awake` (0x200000BC) is 0 the key only wakes the UI (`FUN_08005a10`: `awake`=2, and 0x200000A4=0) and does nothing else; otherwise call the handler for the current UI state (byte **0x2000005A**). UP+DOWN together in state 0 sets 0x20000057 for 250 ms (alternate display field, harmless). Afterwards `FUN_0800800c` / `FUN_08007fe2` run (settings save, see 8.5).

### 8.3 UI states (0x2000005A) [V transitions, I names]

| State | Screen (drawn by) | Entered by | Keys |
|---|---|---|---|
| 0 | main screen (`FUN_08007658`) | boot, exit of any state | UP: next zoom level 0-3 (5 s/5 min/2 h/½ day), DOWN: next quantity (CO2/RH/TEMP) and sub-state; MENU short: open menu (state 6, item 1); MENU long: state 0x16; ENTER long: factory reset; ENTER short: nothing |
| 5 | start-up animation/countdown (`FUN_080076a8`) | boot | none; sensors not polled; LD1 off |
| 6 | main menu (`FUN_08006f9c`) | MENU short from 0 | item index 0x20000043 = 1..6; DOWN/MENU short next (MENU after item 6 exits to 0); UP previous (wraps 1→6); ENTER opens item |
| 0x0A | ALARM (`FUN_0800700c`) | menu item 1 | UP/DOWN toggle alarm on/off, stored at settings +0x20 and saved immediately; MENU/ENTER back to 0 |
| 0x15 | PLANT (`FUN_08006fb0`) | menu item 2 | UP/DOWN value 0-9 (wraps), ENTER stores to settings +0x24 and saves. Presets 0 and 1 use the user-editable thresholds (defaults 800/1200 and 600/900 ppm), presets 2-9 use fixed thresholds (section 10.1) |
| 0x0B | LOG (`FUN_08006f14`) | menu item 3 | UP/DOWN scroll 0-16 (17 columns), ENTER cycles 0-2 (quantity) |
| 2 | CALI (`FUN_08006ed4`) | menu item 4 | ENTER long starts the CO2 zero-point calibration (flag 0x20000065 = 1, a **250 s countdown** shown in the big digits, decremented by TIM1); at 0 the firmware sends the CM1106S calibration command (section 10.4) and returns to the main screen; idle value shown is 400 |
| 3 | °C/°F (`FUN_08007874`) | menu item 5 | UP/DOWN toggle 0/1, ENTER stores to settings +0x14 |
| 1 | ADV, alarm thresholds (`FUN_08006e24`) | menu item 6 | UP/DOWN select Low/High then change the value in steps of 10 ppm (0 … 5000, wraps); ENTER stores; editing is only allowed for modes 0/1 (`settings+0x24` < 2) because presets are fixed |
| 0x16 | sensor offset calibration (`FUN_080077e8`) | **MENU long** from 0 | ENTER first press loads the current reading, UP/DOWN edit it, ENTER second press stores **offset = entered value − raw measurement**: temperature page → settings +0x18 (converted back from °F if unit = 1), **humidity** page (flag 0x20000060 = 1) → settings +0x1C. It calibrates temperature and humidity only, not CO2 |
| 4 | date/time editor code (month 1-12, day by month length incl. leap years, year 0-99, hour 0-23, minute 0-59) | **no entry found** | dormant code in the UP/DOWN handlers, left over from a model with a clock |

Settings struct (RAM pointer at 0x20000338, 10 words): +0 marker 0x8000; +4/+8 human low/high ppm (800/1200); +0xC/+0x10 custom plant low/high (600/900); +0x14 unit (°C/°F); +0x18 temperature offset; +0x1C humidity offset; +0x20 alarm on; +0x24 preset index. Preset thresholds are read/written with `FUN_080007fc(preset, high?)` / `FUN_080008b0(preset, high?, value)`.
Menu order matches the manual legends: ALARM, PLANT, LOG, CALI, °C/°F, ADV.

### 8.4 Menu inactivity [V]
`FUN_08007fe4(1)` (called on key presses inside the menu screens) starts a **60 s** countdown (0x20000046, flag 0x20000048 = 1, decremented by TIM1); `FUN_08007fe4(0)` re-arms/stops it. When it expires (flag = 2) `FUN_0800800c` returns the UI to state 0 and redraws. TIM1 also runs a 30 s countdown (0x20000049/4A) and a 300 s timeout (0x2000003E/40) which are never started (no code sets their start flags 0x20000049 / 0x2000003E): dead code.

### 8.5 Settings save requests [V]
Flag 0x2000006A: 1 = save settings page, 2 = factory reset (set by ENTER long in state 0). Executed by `FUN_08006898` in the main loop; see section 11.

### 8.6 Resolved
`FUN_08007fe4` is the menu-inactivity timer (8.4). Alarm, CALI and sensor-timeout questions are answered in section 10.

## 9. LCD driver and display buffer (stage 4)

### 9.1 Bus protocol [V]
HT1621/HT1625-style 3-wire serial LCD controller (the board part is a VK1625), bit-banged on **CS = PA8 (active low), WR = PC8, DATA = PC7, RD = PC9** (RD is only set high at start-up; the bus is write-only). Each bit: set DATA, then WR low → busy-wait → WR high → busy-wait (`FUN_0800922c`; data is latched on the rising edge). Idle state: CS, WR, DATA, RD all high. Levels are shifted from 3 V to 4 V by the BSS138 stages.
- **Command frame** (`FUN_08009258(cmd)`): CS low; bits `1 0 0`; 8 command bits MSB first; one extra 0 bit; CS high.
- **Write frame** (`FUN_080092f8(addr, buf, n)`): CS low; bits `1 0 1`; 7 address bits MSB first (address is always 0); then n bytes, **each byte sent LSB first**, one after another (auto-increment); CS high.
- Start-up commands (`FUN_080090f8`), in this order: `0x01` SYS EN, `0x03` LCD ON, `0x08` TONE OFF, `0x80` IRQ DIS, `0x04` TIMER DIS, `0x05` WDT DIS (the HT1621/HT1625-family command set that the VK1625 follows; a typical init also adds `0x18` RC 32K, which this firmware leaves at the default) [V + datasheet]. The firmware also stores 8 in RAM 0x20000032 at that point (not used by the display code found).
- The frame buffer is **39 bytes (0x27) at RAM 0x20000310**; the whole buffer is sent at address 0. Helpers: `FUN_08000d00` push buffer, `FUN_080090d4(v)` fill with v and push, `FUN_08000d14` all segments on (0xFF), `FUN_08001292` clear. The buffer is pushed at the end of every UI-drawing pass (`FUN_08005a28` → `FUN_08000d00`).

### 9.2 Primitives [V]
- `FUN_08000cd4(bit, byte, on)` set/clear one bit in buffer byte `byte`. Everything below is built from it.
- Three bit-copy digit routines, one per font table (below), each splits a 7-segment glyph byte across **two neighbouring buffer bytes** (the other bits of those bytes belong to other digits/icons, so always modify bits, never write whole bytes).
- The UI-state drawing functions (`FUN_080091a8` calls the one for the current state; all call `FUN_08006414` begin / `FUN_0800648c` end for the common elements).

### 9.3 Fonts [V]
Segment bit order in a glyph byte (derived from the glyph shapes): **bit7 = a, bit6 = b, bit5 = c, bit4 = d, bit2 = f, bit1 = g, bit0 = e, bit3 unused** (a top, b upper right, c lower right, d bottom, e lower left, f upper left, g middle) – this holds for all four tables up to a bit permutation noted per table.

| Table (flash) | Glyphs 0-9 | Extra glyphs | Used for |
|---|---|---|---|
| 0x080095C0 (B) | F5 60 D3 F2 66 B6 B7 E0 F7 F6 | 10 = blank (00), 11 = `L` (15), 12 = `H` (67), 13 = `i` (05), 14 = `E` (97), 15 = `-` (02) | big CO2 digits and the 2-digit humidity |
| 0x080095D0 (C) | FA 60 D6 F4 6C BC BE E0 FE FC | 10 = blank (00), 11 = `-` (04) | 3-digit temperature (tenths) |
| 0x080095B4 (A) | AF 06 6D 4F C6 CB EB 0E EF CF | 10 = blank (00), 11 = `-`? (40) | MAX / MIN readout |
| 0x080095F1 | EE 48 D6 DA 78 BA BE C8 FE FA | – | **not referenced by any code** (dead data, probably a font for another LCD variant) |
| 0x080095DC | 00 01 03 07 0F 1F 3F 7F FF | – | bar-graph columns: level 0-8 segments (`FUN_08000e40` writes the 8 bits of the byte into one buffer byte) |

Table B bit mapping into the buffer (`FUN_080013f8(byte, glyph)`): glyph bits 7..4 → bits 7..4 of buffer byte `byte`; glyph bits 2..0 → bits 6..4 of buffer byte `byte+1` (bit 4 = e, bit 5 = g, bit 6 = f). Tables C and A use the low nibble / other bit groups of the same bytes (`FUN_080014e8`: glyph bits 7..4 → bits 3..0 of byte, glyph bits 3..1 → bits 3..1 of byte+1; `FUN_0800144c(variant 0/1)`: two variants, low or high half), which is why one byte carries halves of two different digits.

### 9.4 Buffer map of the main screen [V for positions, I for meaning]
| Buffer bytes | Content | Drawn by |
|---|---|---|
| 0x1F, 0x21, 0x23, 0x25 (+ neighbours) | CO2 ppm, ones … thousands, table B, leading zeros blanked; value `E1` (glyph 14 + 1) when the sensor error flag 0x20000091 ≠ 0; `----` (glyph 15) while the "no valid reading" flag 0x200000FF = 1 | `FUN_08000ab0(mode, 0x20000036)` |
| 0x1B, 0x1D (+0x1C) | relative humidity, 2 digits, table B; **bit 7 of 0x1C = "%"**; `Lo`/`Hi`/`--` as special values | `FUN_080012dc(mode, 0x2000003A)` |
| 0x1B, 0x1D, 0x1F (+0x1C) | temperature, 3 digits in tenths with sign (glyph 11 = minus), table C; **bit 0 of 0x1C = decimal point**; °C/°F marker = bits 1/2 and degree sign bit 3 of **byte 0x13** (`FUN_0800153c`) | `FUN_080015a0(mode, value, unit, …, enable)` (mode 1/0 normal, 2 = `---`, 4 = off) |
| 0x0D, 0x0F, 0x11 (high nibbles) | **MAX readout of the chart** (3 digits, table A variant 1; leading "1" = bit 6 of byte 0x0C when value ≥ 1000, "÷10" marker = bit 4 of byte 0x0D when ≥ 1999, decimal point = bit 4 of byte 0x11) | `FUN_08006940` |
| 0x0D, 0x0F, 0x11 (low nibbles) | **MIN readout of the chart** (table A variant 0; leading "1" = bit 0 of byte 0x0D, "÷10" marker = bit 0 of byte 0x0F, decimal point = bit 0 of byte 0x11) | `FUN_08006a56` |
| bytes ≈ 2 … 0x1A | chart columns (one byte per column, bits = bar height) | `FUN_08000d20` (scales min…max to 8 levels), `FUN_08000e40` |
| 0x1E bit 7 | PPM unit icon (on during normal drawing) | `FUN_08000a90` |
| 0x21 bits 2, 3 | two more fixed icons set together | `FUN_08000ca0` |
| 0x22 bit 2, 0x23 bit 1 | two icons on whenever a screen is shown, individually selectable | `FUN_08000a3a` |
| 0x23/0x24 bits 2, 3 | human / plant pictograms depending on preset (`settings+0x24`: 0 = human, other = plant variants) | `FUN_08000c16` |
| 0x20/0x22/0x24/0x25/0x26 (bits 0-3, 7) | time-scale (zoom) icons of the chart, one of 4 states + off, selected by the zoom level 0x2000004D | `FUN_08000e6c` (called from `FUN_08006374`, which draws the whole chart from the log array of that zoom level: arrays at 0x20000384 / 0x200003EA / 0x20000450 / 0x200004B6, each 17 columns × 3 quantities × 16 bit = 102 bytes) |
| 0x1E, 0x20, 0x22, 0x23, 0x24 bit 0/1 and 0x21 bit 1 | the six menu legends ALARM, PLANT, LOG, CALI, °C/°F, ADV – exactly one bit set for menu item 1…6 | `FUN_080010ba(item)` |

The segment-to-glass relation is not needed for a port: reproduce the 39-byte buffer by copying the bit positions above. Positions not listed (labels such as MAX/MIN text, GOOD ZONE text, the tail bits of chart columns) are the next thing to fill in by stepping through the draw functions `FUN_08007658`, `FUN_08006e24`, `FUN_08006f14`, `FUN_08007570`, `FUN_080077e8`, `FUN_08007874` when the UI is rebuilt on Zephyr; a faster trick is to light each bit one at a time with `FUN_08000cd4` on the real device.

### 9.5 Corrections made in this stage
State 0x16 (MENU long) calibrates **temperature and humidity** offsets (not CO2); the trend log stores CO2, offset-corrected RH and offset-corrected temperature; settings +0x1C is the humidity offset.

## 10. Alarms, buzzer, backlight, start-up, calibration (stage 5)

### 10.1 Zone evaluation (`FUN_08005e3c(co2, preset)`) [V]
Thresholds (low, high ppm): preset 0 = settings +4/+8 (default 800/1200), preset 1 = settings +0xC/+0x10 (default 600/900), fixed: 2 = 600/900, 3 = 800/1000, 4 = 1000/1500, 5 = 800/1400, 6 = 800/1400, 7 = 1200/1800, 8 = 800/1200, 9 = 800/1200 (same table in `FUN_080007fc`).
Result is the zone level 0x200000B4 (drives the LEDs in `FUN_08006b6c`: 0 = LD4 green, 1 = LD3 yellow, 2 = LD2 red) and the buzzer request 0x200000AE:
- **Preset 0 (human)**: CO2 < low → level 0; low ≤ CO2 < high → level 1; CO2 ≥ high → level 2 and buzzer request 3.
- **Presets 1-9 (plants)**: low < CO2 < high → level 0; CO2 ≤ low → level 2 and buzzer request 3; CO2 ≥ high → level 1 (no buzzer).
- Request 3 is dropped to 0 as soon as the zone is left; a pending request 4 is never overwritten by 3.

### 10.2 Buzzer (`FUN_08005f74`, `FUN_080061f4`, PC1) [V]
Request value 0x200000AE: 3/2/1 = repeating alarm beep (only if the ALARM setting is on, i.e. 0x2000005B = 0): **100 ms on, 1 s off** (50 ms ticks: 2 on / 20 off); 5 = single **200 ms** beep (issued once at start-up by `FUN_08008040`); 4 = "silenced by key press": every accepted key sets 4 (`FUN_080055f8`), which ends the current beep and cancels the alarm until the next zone evaluation sets 3 again; anything else = buzzer off. The pattern stepper runs on the 50 ms flag and only while the 50 ms key-beep state machine (8.1) is idle.
Requests 2/3/5 also wake the backlight (below).

### 10.3 Backlight (PC3, `FUN_0800619c`, 105 ms flag) [V]
State 0x200000BC: 0 = off; 1 = on while an alarm is active (cleared back to 0 when the alarm ends); 2 = on for **600 × 105 ms ≈ 63 s** after any key press, start-up or one-shot beep, then off. In the start-up state the backlight is forced on.

### 10.4 CO2 calibration command (`FUN_080062a0(400)`) [V]
CM1106S zero-point calibration frame, 6 bytes: `11 03 03 01 90 CS` with CS = 0x100 − (sum of the first 5 bytes) = 0x58 (400 ppm = 0x0190). Expected reply `16 01 03 E6`; up to 10 attempts with a 50-unit delay between them. The function blocks the main loop while it runs. The countdown before it (250 s) is intended for the sensor to sit in fresh air.

### 10.5 Start-up sequence (UI state 5, `FUN_08007360` / `FUN_080076a8`) [V]
Step counter 0x20000064 (1…9), seconds counter 0x20000062 decremented by TIM1:
1. 2 s: all LCD segments on (blinking via flag 0x20000069).
2-5. 1 s each: LED chase PB15 → PB14 → PB13 → PB12 (LD1, LD2, LD3, LD4); from step 3 the big digits show the preset's **low threshold with `Lo`** and from step 5 the **high threshold with `Hi`**.
6. all LEDs on 1 s; 7. all off, 30 s counter starts and is shown in the two middle digits; the first CM1106S request is issued from step 3 on, the first SHT30 measurement when the counter reaches 2.
8. at 0: state 0 (main screen), values computed, first CO2 read parsed. Total ≈ 38 s. Backlight is on and sensor polling is stopped (main loop mode 5) during this time.

### 10.6 Sensor-lost handling [I]
After ≈ 40 polls without a valid CO2 frame: error flag 0x20000091 = 1, PC2 low (CM1106S unpowered); the TIM2 100-tick (0.5 s) one-shot then switches PC2 high again (power cycle) and the flag stays non-zero until a valid frame arrives, so the display shows `E1` meanwhile.


## 11. Flash storage (stage 6)

Flash: 256 KB, 2 KB pages (page n at 0x08000000 + n·0x800). All writes are 64-bit double-word programs through HAL (`FUN_08001b88` = program N words, each stored in the low half of one double word, upper half 0; `FUN_08001b2c(page, n)` = erase n pages, retried until it succeeds, IWDG refreshed). Reads are plain memory reads (`FUN_08007f9e`, `FUN_08007fb6` which reads one word per 8 bytes).

### 11.1 Settings (page 0x7E = 0x0803F000) [V]
10 words, each in its own 8-byte slot (80 bytes used): +0 marker 0x8000 (valid), then the settings struct of 8.3. The valid check is only "first word == 0x8000". If invalid: defaults (800/1200, 600/900, unit 0, offsets 0, alarm 0, preset 0) are put in RAM and a save is requested. Saving = erase page 0x7E, program 10 double words (done in the main loop, `FUN_08006898`, on request flag 1). Saves are triggered by: ALARM toggle, PLANT, °C/°F, ADV thresholds, offset calibration.
**Alarm flag load (checked, no bug):** the decompiler shows the load as `*(undefined1 *)(DAT + 8)`, but `DAT` is a `undefined4 *`, so the offset is in words: +8 words = **+0x20 bytes**, the same place where the flag is saved. Confirmed on the device: ALARM on/off survives a reboot and the alarm beep behaves accordingly. Caveat for reading the decompiled code: offsets added to typed pointers are element-sized, so always check the pointer type.

### 11.2 Trend logs (pages 0x77-0x7C, 0x0803B800-0x0803E7FF) [V]
Three logs, two pages each, 64 records of 32 bytes per page:

| Log | Zoom step | Page A | Page B |
|---|---|---|---|
| 0 | 5 min | 0x0803B800 | 0x0803C000 |
| 1 | 2 h | 0x0803C800 | 0x0803D000 |
| 2 | 12 h | 0x0803D800 | 0x0803E000 |

The 5 s level is RAM only (not stored). RAM arrays: zoom 0 → 0x20000384, then 0x200003EA, 0x20000450, 0x200004B6 for the stored levels (17 columns × {CO2, RH, temperature} × u16 = 102 bytes) [I for the exact array-to-zoom order].
Record (`FUN_08008074(log, address, values)`): four double words: +0 marker (0xFFA0 / 0xFFB0 / 0xFFC0 for log 0/1/2), +8 CO2, +0x10 RH, +0x18 temperature, 16-bit values (a stored 0 is replaced by 400 / 1 / 1, so that an unwritten slot is distinguishable). The function returns the next free address; write pointers are RAM words 0x20000108 / 0x2000010C / 0x20000110 (reset to the A pages on factory reset).
Ring behaviour: when the next address enters page B (or wraps from the end of B to A) **that page is erased first**, so at least 64 and up to 128 records are always present. Wear: log 0 writes one record per 5 min, i.e. one page erase per 5.3 h (≈ 12 years for 10 k cycles); logs 1-2 are far lower.
Boot (`FUN_08007cb8(log, buffer)`, called three times from application init): count the records with the right marker in page A and page B and rebuild the last 17 columns, newest first, handling the page wrap; if the logs are empty the buffers stay zero.

### 11.3 Factory reset (flag 2) [V]
Erase pages 0x7E and 0x77-0x7C, reload defaults (`FUN_08006bfc`, which queues a save), zero the RAM log arrays (`FUN_08001878`), reset the three write pointers.

## 12. Summary for the Zephyr re-implementation (task 3)

Modules and sources: display (9) with the 39-byte buffer and font tables, sensors (7), UI/buttons (8), alarms/backlight/start-up/calibration (10), storage (11), clock tree (1-3).
Remaining gaps after the gap-filling pass (section 13): the glass meaning of the icon bits (names are inferred from menu order), the unlisted low bits of a few shared bytes, and what the leftover `0x2000006C` main-loop flag means.

## 13. Gap-filling pass (flash dump + exact call tracing)

Method: the decompiler dropped the third argument of many `FUN_08000cd4(bit, byte, on)` calls; the values were recovered by tracing the Thumb code (register `r2` is not reloaded between calls, so a missing argument = value of the previous call). All bit positions below are exact [V]; names are inferred from the manual/menu order [I].

### 13.1 Task schedule (see section 7) [V]
2 s flag → SHT30 (full T/RH refresh every 4 s); 5 s flag → CM1106S poll + trend log (5 s); 1 s and 9 s flags dead; the 30 s and 300 s TIM1 counters are never started (dead). `0x2000006C` ≠ 0 suppresses the CO2 poll (probably "calibration/command in progress") [I]. The RTC is initialised but never read or written afterwards (CubeMX leftover).

### 13.2 Display controller commands [V + datasheet]
`01` SYS EN, `03` LCD ON, `08` TONE OFF, `80` IRQ DIS, `04` TIMER DIS, `05` WDT DIS. No further commands are ever sent, so bias/duty stay at the power-on default.

### 13.3 Buffer byte/bit map of the icons (byte, bit)
| Element | Bits |
|---|---|
| Menu legends (`FUN_080010ba(item)`) | always on: (0x21,1). Item 1 ALARM (0x24,1); 2 PLANT (0x24,0); 3 LOG (0x23,0); 4 CALI (0x22,0); 5 °C/°F (0x20,0); 6 ADV (0x1E,0); only the selected item's bit is set |
| Alarm icon (`FUN_08000a3a(1, muted, blink)`) | if blink flag (0x20000034) = 1: bell (0x22,2) on and (0x23,1) = (alarm flag 0x2000005B == 1); else both off |
| Pictograms (`FUN_08000c16(preset ≠ 0, on)`) | off: (0x24,3),(0x24,2),(0x23,3),(0x23,2) all 0. On, preset 0: (0x24,3)=1,(0x24,2)=0,(0x23,3)=1,(0x23,2)=1. On, preset ≠ 0: (0x24,3)=1,(0x24,2)=1,(0x23,3)=1,(0x23,2)=0 |
| PPM label | (0x1E,7) |
| Icon pair | (0x21,2) and (0x21,3) set together (`FUN_08000ca0`) |
| Temperature decimal point | (0x1C,0); RH "%" = (0x1C,7); °C marker (0x13,2) / °F marker (0x13,1), degree sign (0x13,3) |
| Chart quantity markers | CO2: (0x00,1)=1, (0x00,3)=0, (0x00,5)=0, (0x13,4..7)=0, (0x0F,4)=1. RH: (0x00,3)=1, (0x13,4)=1, others 0, (0x0F,4)=0. Temperature: (0x00,5)=1, (0x13,5)=1, (0x0F,4)=0, and (0x13,7)=1,(0x13,6)=0 for °C or (0x13,6)=1,(0x13,7)=0 for °F |
| MIN / MAX numbers | see 9.4: 3 digits in bytes 0x0D/0x0F/0x11 (table A), MIN in the low nibbles, MAX in the high nibbles; leading "1", ÷10 marker and decimal point bits as listed there; sign = glyph 11 in the hundreds position |
| Zoom (time-scale) icon (`FUN_08000e6c(zoom)`) | bits A=(0x20,7) B=(0x26,3) C=(0x25,2) D=(0x26,2) E=(0x22,7) F=(0x25,0) G=(0x25,3) H=(0x26,1) I=(0x25,1) J=(0x24,7) K=(0x26,0). Zoom 0 (5 s): A,D,E,K on. Zoom 1 (5 min): A,H,I,J,K. Zoom 2 (2 h): A,F,G,H,K. Zoom 3 (½ day): A,B,C. Off state: all 0 |

### 13.4 Chart columns [V]
`FUN_080064c8(quantity, …, array, unit)` takes min and max over the 17 columns (stopping at the first all-zero column), converts to °F first when the temperature unit is °F, and draws every column with `FUN_08000d20(min, max, column, value, visible)`: the value is scaled linearly between min and max into **1…8 bar segments** (a value above the lower step of max reaches 8; an empty/hidden column gives 0). The selected column blinks (visible = 0 on the blink phase). One buffer byte per column, bit 0 = lowest segment, byte = table 0x095DC[level] (00 01 03 07 0F 1F 3F 7F FF):
- columns 0 … 10 → bytes 0x0B … 0x01 (byte = 0x0B − column),
- columns 11 … 16 → bytes 0x14 … 0x19 (byte = column + 9).
The MIN/MAX readouts are drawn with the same call (min and max of the same column set).

### 13.5 Still unknown
Glass meaning of the individual icon bits beyond the menu-order names; segments of any byte not listed here (they were never written by the code that was traced); (the long-press duration is settled: 6 s).

## 14. Measured LCD segment map (from the device; supersedes 9.4 and 13.3 where they differ)

The full per-bit list is in `co110-zephyr/LED_mapping.txt` and, as C tables, in `co110-zephyr/src/lcd_map.h`. The measured map confirms almost every position found by code analysis; differences and additions:

**Layout**
- Frame buffer 39 bytes; byte 0 and 12 hold the top line and battery symbols; bytes 1-11 and 20-25 the 17 × 8 dot field; bytes 13-18 the MAX (top right) and MIN (second) numbers; byte 19 unit markers; byte 26 eight row arrows left of the dot field; bytes 27-32 the temperature (3 digits, xx.x) and RH (2 digits) sections; bytes 30-38 the CO2 section (4 digits), legends, alarm and mode symbols and the zoom labels.
- Digit halves: low nibble + bits 1-3 of the next byte = lower number of a pair, high nibble + bits 5-7 (or 4-6) = upper number (confirms the three font tables of 9.3: MAX/MIN = table A, RH and CO2 = table B, temperature = table C).
- 7-segment bit order and digit positions are exactly those of 9.3/9.4 (for example CO2 digits at bytes 31/33/35/37 right to left, temperature at 27/29/31 low nibbles, RH at 27/29 high nibbles).

**Chart dot field**: x = 0 (left) … 16 (right), y = 0 (bottom) … 7. Bytes 1…11 hold x = 6…16 (byte 1 → x 6), bytes 20…25 hold x = 5…0 (byte 20 → x 5); bit n = row n. With the firmware's column numbering (column 0 = newest) this gives x = 16 − column, i.e. **the newest sample is the right-most column**, the oldest the left-most.

**Corrections to earlier tables**
- (0x13,1) is the **d segment of the "C"** and (0x13,2) the **g segment of the "F"**, (0x13,3) the common "°" part (also part of the TEMP label): °C = bits 1 + 3, °F = bits 2 + 3 (13.3 had 1 and 2 swapped).
- (0x1E,7) = the **"CO2" label** of the CO2 section; the "PPM" label is the pair (0x21,2) "M" + (0x21,3) "PP" (set together by `FUN_08000ca0`); (0x22,1) "%VOL" is not used by the traced code.
- Top line (byte 0): bit 1 CO2, bit 2 CHART, bit 3 RH, bit 4 cycle arrows (U+1F501), bit 5 TEMP are the quantity markers of the chart row; bits 1/3/5 are the ones the firmware switches (9.4/13.3), bits 2 and 4 are not written by the traced code.
- Battery symbol (byte 0 bit 0 and bit 7, byte 12 bits 0 and 7) exists on the glass but was **not seen in any traced drawing function** (mains-powered model; probably unused).
- Byte 12 bit 5 "← MIN and ← MAX" arrows are set together with the MIN/MAX numbers; (0x13,4) "%" and (0x13,5..7) form the unit mark next to MIN/MAX (13.3 "chart quantity markers"); (0xF,4) "PPM" next to MIN/MAX.
- Zoom labels (13.3) are now named: A "/div" (32,7), E "5s" (34,7), J "5m" (36,7), G "2h" (37,3), C "0.5d" (37,2), D "-1m" (38,2), H "-1" (38,1), I "h" (37,1), F "d" (37,0), B "-1w + triangle" (38,3), K small triangle (38,0). So the four screens read: 5s/div & −1m, 5m/div & −1h, 2h/div & −1d, 0.5d/div & −1w. Bit (38,7) "↔" and the eight row arrows (byte 26) are not written by the traced code (probably used by the LOG browse screen, not yet traced).
- Mode pictograms (35,2..3) human + brackets and (36,2..3) tree + brackets: **the brackets mark the active mode** (preset 0: human in brackets, tree plain; presets ≥ 1: tree in brackets).
- Bell (34,2) and "cross on the bell" (35,1) as in 13.3.

**Port status**: `co110-zephyr/src/display.c` already renders the CO2/RH/temperature numbers, MIN/MAX numbers, chart, zoom labels, menu underlines, alarm and mode symbols from this map (demo application, step 2 of the Zephyr plan).
