# SPDX-License-Identifier: Apache-2.0

if("${RPI_PICO_DEBUG_ADAPTER}" STREQUAL "")
  set(RPI_PICO_DEBUG_ADAPTER "cmsis-dap")
endif()

board_runner_args(openocd --cmd-pre-init "source [find interface/${RPI_PICO_DEBUG_ADAPTER}.cfg]")
if(CONFIG_ARM)
  board_runner_args(openocd --cmd-pre-init "source [find target/rp2350.cfg]")
else()
  board_runner_args(openocd --cmd-pre-init "source [find target/rp2350-riscv.cfg]")
endif()

# The adapter speed is expected to be set by interface configuration.
# The Raspberry Pi's OpenOCD fork doesn't, so match their documentation at
# https://www.raspberrypi.com/documentation/microcontrollers/debug-probe.html#debugging-with-swd
board_runner_args(openocd --cmd-pre-init "set_adapter_speed_if_not_set 5000")

# HACK: For some reason, the CPU1 will be flashed, but will get be deadlocked
# during boot. The "cold_reset" OpenOCD command is specific to the rp2350
# target in Raspberry Pi's downstream fork of OpenOCD and is equivalent to a
# rescue reset from Section 3.5.9 of the rp2350 datasheet or grounding the RUN
# pin.
if(CONFIG_BOARD_RPI_PICO2_RP2350A_M33_CPU1)
  board_runner_args(openocd --cmd-post-verify "cold_reset")
  board_runner_args(openocd --cmd-post-verify "shutdown")
endif()

board_runner_args(probe-rs "--chip=RP235x")

board_runner_args(jlink "--device=RP2350_M33_0")
board_runner_args(uf2 "--board-id=RP2350")

include(${ZEPHYR_BASE}/boards/common/openocd.board.cmake)
include(${ZEPHYR_BASE}/boards/common/probe-rs.board.cmake)
include(${ZEPHYR_BASE}/boards/common/jlink.board.cmake)
include(${ZEPHYR_BASE}/boards/common/uf2.board.cmake)
