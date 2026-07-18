# Zephyr Multi-CPU Board Survey

Summary of every in-tree multi-core board: how the secondary CPU is configured
in devicetree and how it is launched at runtime.

---

## 1. NXP LPC55xxx -- Cortex-M33 + Cortex-M0+

**Boards:** lpcxpresso55s69, lpcxpresso54114

**DT config:** `zephyr,code-cpu1-partition` (+ `zephyr,sram-cpu1-partition` on
lpc55s69) chosen nodes on the primary CPU's DTS. The `cpu@1` node has no
`enable-method` and no vendor-specific compatible string.

**Launch mechanism** (`soc/nxp/lpc/lpc55xxx/soc.c:530`): Writes
`DT_REG_ADDR(DT_CHOSEN(zephyr_code_cpu1_partition))` into the
`SYSCON->CPUBOOT` register, then manipulates `SYSCON->CPUCTRL` to clock and
deassert reset on CPU1. Registered as `SYS_INIT(PRE_KERNEL_2)`.

---

## 2. NXP MCXNxx -- dual Cortex-M33

**Boards:** frdm_mcxn947, mcx_nx4x_evk

**DT config:** `zephyr,code-cpu1-partition` chosen node on the primary CPU's
DTSI.

**Launch mechanism** (`soc/nxp/mcx/mcxn/soc.c:48`): Identical pattern to
LPC55 -- sets `SYSCON->CPBOOT` from the chosen partition address, configures
TrustZone access, then releases CPU1 reset via `SYSCON->CPUCTRL`.
`SYS_INIT(PRE_KERNEL_2)`.

---

## 3. NXP IMXRT11xx -- Cortex-M7 + Cortex-M4

**Boards:** mimxrt1170_evk, mimxrt1160_evk

**DT config:** `zephyr,cpu1-region` chosen node pointing to `&ocram`. This
names the SRAM region where the M4 image will execute, not the flash partition
it is stored in.

**Launch mechanism** (`soc/nxp/imxrt/imxrt11xx/soc.c:828`): Two-phase. During
`imxrt_init` (PRE_KERNEL_1): copies the M4 image to
`DT_CHOSEN(zephyr_cpu1_region)` and programs the M4 VTOR via
`IOMUXC_LPSR_GPR0/1`. Then in `second_core_boot` (PRE_KERNEL_2): releases M4
reset via `SRC->CTRL_M4CORE` and `SRC->SCR`. Optionally waits for M4 to signal
readiness via a Messaging Unit (MU) flag.

---

## 4. NXP IMXRT118x -- Cortex-M33 + Cortex-M7

**Boards:** frdm_imxrt1186, mimxrt1180_evk

**DT config:** `zephyr,code-m7-partition` chosen node on the CM33 (primary)
DTS.

**Launch mechanism** (`soc/nxp/imxrt/imxrt118x/soc.c:880`):
`second_core_boot()` releases the CM7 from reset. `SYS_INIT(PRE_KERNEL_2)`.

---

## 5. ADI MAX32 family -- Cortex-M4F + RISC-V

**Boards:** max78002evkit, max32690evkit, max32655evkit, apard32690

**DT config:** `zephyr,code-rv32-partition` chosen node on the M4F (primary)
DTS. The `cpu@1` node has `compatible = "adi,max32-rv32", "riscv"` and
`status = "disabled"` in the SoC DTSI but no `enable-method`.

**Launch mechanism** (`soc/adi/max32/soc.c:24`): Writes
`DT_REG_ADDR(DT_CHOSEN(zephyr_code_rv32_partition))` directly into the
`MXC_FCR->urvbootaddr` hardware register, then releases the core. A
`BUILD_ASSERT` enforces that the chosen property is set. The same file also
handles the dual-M4F variant on max32666evkit via `zephyr,code-cpu1-partition`
into `MXC_GCR->gp0`.

---

## 6. ADI MAX32666 -- dual Cortex-M4F

**Boards:** max32666evkit

**DT config:** `zephyr,code-cpu1-partition` chosen node on the primary DTS.

**Launch mechanism** (`soc/adi/max32/soc.c`): Writes
`DT_REG_ADDR(DT_CHOSEN(zephyr_code_cpu1_partition))` into `MXC_GCR->gp0`.

---

## 7. STM32H7 dual-core -- Cortex-M7 + Cortex-M4

**Boards:** stm32h745i_disco, stm32h747i_disco, stm32h757i_eval,
nucleo_h745zi_q, and others

**DT config:** No ad-hoc chosen nodes. Both cores have their own completely
independent DTS files (each is a full Zephyr target). The `cpu@1` node (M4)
does not appear in the primary CPU's DTS at all.

**Launch mechanism** (`soc/st/stm32/stm32h7x/soc_m7.c:46`): The M4 either
starts from hardware reset or is started by the M7 via `LL_RCC_ForceCM4Boot()`.
Either way, the M4 stalls in its own `soc_early_init_hook()` waiting for the M7
to lock a specific Hardware Semaphore (`CFG_HW_ENTRY_STOP_MODE_SEMID`), which
signals that M7 has completed system initialization.

This is a **synchronization-at-boot** model, not a launch model: both cores run
independent full Zephyr images and neither has knowledge of the other's image
location in devicetree.

---

## 8. Espressif ESP32 / ESP32-S3 -- dual Xtensa LX6/LX7

**Boards:** esp32_devkitc, esp32s3_devkitc, esp32s3_eye, esp_wrover_kit,
esp_threadbr, esp32s3_box3, and others

**DT config:** The app CPU has its own separate DTS target (e.g.,
`esp32s3_devkitc_appcpu.dts`) that includes `esp32s3_appcpu.dtsi`. The appcpu
DTS sets `zephyr,code-partition = &slot0_appcpu_partition` for its own image.
This is a standard code-partition chosen node on the appcpu target itself, not a
special secondary-CPU property on the primary target.

**Launch mechanism** (`soc/espressif/esp32s3/esp32s3-mp.c`): The procpu SoC
code loads the appcpu image from a hardcoded DT label (`slot0_appcpu_partition`),
calls `esp_rom_ets_set_appcpu_boot_addr()` ROM API, then `esp_appcpu_start()`.
`SYS_INIT(POST_KERNEL)`.

---

## 9. Nordic nRF54L / nRF54H / nRF7120 / nRF9280 -- Cortex-M33 + RISC-V VPR (cpuflpr)

**DT config:** The secondary RISC-V VPR core appears as `cpu@1` with
`compatible = "nordic,vpr", "riscv"` in the `cpus` node. Separately, a
`cpuflpr_vpr` peripheral node carries `compatible = "nordic,nrf-vpr-coprocessor"`
with `execution-memory` and `source-memory` phandles. No `enable-method` on the
cpu node itself.

**Launch mechanism:** The `nordic_vpr_launcher` driver binds to the
`nordic,nrf-vpr-coprocessor` peripheral node, reads `execution-memory` and
`source-memory`, copies the image if needed, and writes the entry point into the
VPR's memory-mapped `INITPC` register. This works because the nRF54L15 VPR has
a dedicated hardware peripheral block at a fixed address -- the `cpuflpr_vpr`
node represents that device. The `nordic,nrf-vpr-coprocessor` binding was
rejected for the RP2350 because no equivalent hardware peripheral exists there
(see [#93379](https://github.com/zephyrproject-rtos/zephyr/pull/93379)).

---

## 10. Raspberry Pi RP2350 -- dual Cortex-M33

**Boards:** rpi_pico2 (this PR: [#109953](https://github.com/zephyrproject-rtos/zephyr/pull/109953))

**DT config (old, being replaced):** `zephyr,code-cpu1-partition` +
`zephyr,sram-cpu1-partition` chosen nodes, same ad-hoc pattern as NXP LPC55.

**DT config (proposed):** `enable-method = "raspberrypi,rpi-pico-cpu1"` with
`execution-memory` and `source-memory` phandles directly on the `cpu@1` node,
per DTSpec section 3.8.1 vendor-defined enable-method convention.

**Launch mechanism:** CPU0 sends CPU1's stack pointer and program counter over
the SIO inter-processor FIFO to CPU1's BootROM loader, which is waiting for
this handshake sequence. There is no memory-mapped launch peripheral.

---

## Patterns that do NOT require an RFC fix

| Pattern | Examples | Reason |
|---|---|---|
| ARM64 SMP via PSCI | NXP i.MX8/9, TI AM62, Rockchip, Allwinner | `enable-method = "psci"` is already standard in DTSpec and documented in `cpu.yaml` |
| RISC-V SMP | Intel ADSP, SiFive FU540/FU740, Microchip PolarFire | Secondary CPUs have `status = "okay"` and are started by the SMP infrastructure; no per-image launch info needed in DT |
| Nordic VPR | nRF54L, nRF54H, nRF7120, nRF9280 | Uses a dedicated hardware peripheral node (`nordic,nrf-vpr-coprocessor`) -- correct pattern for hardware that has such a peripheral |
| STM32H7 dual-core | stm32h745, stm32h747, stm32h757, nucleo_h745 | Independent-image synchronization model, not a primary-launches-secondary model; no image location needed in DT |

---

## Summary: ad-hoc chosen node users (RFC migration candidates)

| SoC family | Property name(s) | Launch register | Notes |
|---|---|---|---|
| NXP LPC55xxx | `zephyr,code-cpu1-partition`, `zephyr,sram-cpu1-partition` | `SYSCON->CPUBOOT` | lpc55s69 uses both; lpc54114 uses only code |
| NXP MCXNxx | `zephyr,code-cpu1-partition` | `SYSCON->CPBOOT` | |
| NXP IMXRT11xx | `zephyr,cpu1-region` | `IOMUXC_LPSR_GPR0/1` | Names execution region, not source partition |
| NXP IMXRT118x | `zephyr,code-m7-partition` | SRC reset registers | |
| ADI MAX32 (RV32) | `zephyr,code-rv32-partition` | `MXC_FCR->urvbootaddr` | |
| ADI MAX32 (M4F) | `zephyr,code-cpu1-partition` | `MXC_GCR->gp0` | max32666evkit only |
| RP2xxx (proposed) | replaced by `enable-method` on cpu node | SIO FIFO (BootROM) | This PR replaces chosen nodes with the new pattern |
