# SPDX-License-Identifier: Apache-2.0
# Flash with STM32CubeProgrammer over ST-Link (SWD).
# STM32_Programmer_CLI must be on PATH.
board_runner_args(stm32cubeprogrammer "--port=swd" "--reset-mode=hw")
include(${ZEPHYR_BASE}/boards/common/stm32cubeprogrammer.board.cmake)
