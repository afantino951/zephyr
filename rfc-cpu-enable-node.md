# RFC: `cpu-enable` node — a standard devicetree mechanism for AMP secondary CPU launch

## Problem

Zephyr has no standard devicetree mechanism for describing how a secondary CPU
in an AMP configuration is enabled. The result is a proliferation of ad-hoc
`/chosen` properties across vendors (e.g. `zephyr,code-cpu1-partition`,
`zephyr,code-rv32-partition`, `zephyr,code-m7-partition`).

A previous RFC proposed attaching the enable-method properties directly to the
cpu node via a second `compatible` entry. This approach is broken: edtlib
performs binding matching on the **first** matching compatible only. A cpu node
with `compatible = "vendor,cpu1", "arm,cortex-m33"` will have only the
`vendor,cpu1` binding applied; the `arm,cortex-m33` binding — and its declared
properties such as `riscv,isa-base` — are never consulted. Properties present on
the node but not declared in the matched binding cause a hard edtlib error.

This RFC proposes a different approach: a **separate `cpu-enable` node** that
carries the enable-method information, leaving the cpu node and its architecture
binding completely untouched.

## Proposed Change

### New directory: `dts/bindings/cpu_enable/`

This directory hosts the base binding and all vendor enable-method bindings.
It is analogous to `dts/bindings/pm_cpu_ops/`, which holds PSCI and FVP
bindings for the separate `/psci` node. `cpu-enable` nodes are independent
nodes, not cpu nodes, so they do not belong in `dts/bindings/cpu/`.

### Base binding: `dts/bindings/cpu_enable/cpu-enable.yaml`

No `compatible:` field — this file is never matched to a node directly. It is
included by vendor bindings, the same way `arm_psci.yaml` is included by
`arm,psci-0.2.yaml`.

```yaml
# dts/bindings/cpu_enable/cpu-enable.yaml

description: |
  Base properties for vendor-defined cpu-enable nodes.

  A cpu-enable node describes how to bring a secondary CPU out of its
  quiescent state in an AMP configuration. It carries the information
  needed to relocate the secondary CPU's image into its execution region
  (if necessary) and to start the CPU executing.

  Each vendor-specific binding should include this file and set
  compatible to match the enable-method string on the target cpu node.

  Example:

    / {
        cpu1_enable: cpu1-enable {
            compatible = "vendor,soc-cpu1";
            cpu = <&cpu1>;
            execution-memory = <&sram_cpu1>;
            source-memory = <&cpu1_flash_partition>;
            status = "okay";
        };
    };

    &cpu1 {
        status = "disabled";
        enable-method = "vendor,soc-cpu1";
    };

include: base.yaml

properties:
  cpu:
    type: phandle
    required: true
    description: |
      The /cpus/cpu* node this enable node brings up. The referenced cpu
      node should have status = "disabled" and an enable-method string
      matching this node's compatible.

  execution-memory:
    type: phandle
    required: true
    description: |
      Memory region or partition from which the secondary CPU executes.
      The CPU's entry point and initial stack pointer are read from the
      vector table at the base of this region.

  source-memory:
    type: phandle
    description: |
      Memory region or partition from which the secondary CPU's image is
      copied into execution-memory at enable time.

      Omit this property if the image is already present in
      execution-memory (e.g. flashed directly to the execution address).
      When absent, no image relocation is performed before starting the
      CPU.
```

### Vendor binding example: `dts/bindings/cpu_enable/raspberrypi,rpi-pico-cpu1.yaml`

```yaml
# dts/bindings/cpu_enable/raspberrypi,rpi-pico-cpu1.yaml

title: Raspberry Pi Pico series secondary CPU enable

description: |
  The Raspberry Pi Pico family of SoCs uses the SIO inter-processor FIFO
  to bring CPU1 out of its quiescent state. CPU0 reads CPU1's entry point
  and initial stack pointer from the vector table at the base of
  execution-memory, then sends them to CPU1 via a mailbox handshake.

  If source-memory is present, CPU0 first copies the CPU1 image from
  source-memory into execution-memory before initiating the handshake.

compatible: "raspberrypi,rpi-pico-cpu1"

include: cpu-enable.yaml
```

No additional properties are needed for the Raspberry Pi Pico family. The SIO
mailbox register address is already present in the SoC dtsi and is accessed
directly by the SoC init code.

### Changes to `dts/bindings/cpu/cpu.yaml`

Update the `enable-method` description to document the vendor node pattern:

```yaml
  enable-method:
    type: string
    description: |
      Method by which a CPU in a disabled state is enabled. Required for
      CPUs with status = "disabled". The value may be one of:
        - "psci": ARM Power State Coordination Interface.
        - "spin-table": Standard DTSpec spin-table; requires
          cpu-release-addr.
        - "[vendor],[method]": Vendor-defined method. A cpu-enable node
          with a compatible string matching this value must exist and
          have status = "okay".

      For details, see "3.8.1 General Properties of /cpus/cpu* nodes"
      in Devicetree Specification v0.4.
```

## Devicetree Usage

### cpu node

The cpu node retains only its architecture properties. No compatible change,
no additional properties:

```dts
/* SoC dtsi — unchanged from upstream */
cpus {
    cpu0: cpu@0 {
        compatible = "arm,cortex-m33";
        reg = <0>;
        /* ... */
    };

    cpu1: cpu@1 {
        compatible = "arm,cortex-m33";
        reg = <1>;
        status = "disabled";
        enable-method = "raspberrypi,rpi-pico-cpu1";
    };
};
```

For Hazard3 (RISC-V), the architecture properties are untouched because the
cpu node's compatible is never changed:

```dts
&cpu1 {
    compatible = "riscv";
    riscv,isa-base = "rv32i";
    riscv,isa-extensions = "i", "m", "a", "c", "zicsr", "zifencei",
                           "zba", "zbb", "zbkb", "zbs";
    status = "disabled";
    enable-method = "raspberrypi,rpi-pico-cpu1";
};
```

### cpu-enable node

Added by a board overlay or snippet when CPU1 is used:

```dts
/ {
    cpu1_enable: cpu1-enable {
        compatible = "raspberrypi,rpi-pico-cpu1";
        cpu = <&cpu1>;
        execution-memory = <&sram0_cpu1>;
        source-memory = <&cpu1_slot0_partition>;
        status = "okay";
    };
};
```

For boards where the secondary CPU image is flashed directly to its execution
address (no relocation needed), omit `source-memory`:

```dts
/ {
    cpu1_enable: cpu1-enable {
        compatible = "vendor,soc-cpu1";
        cpu = <&cpu1>;
        execution-memory = <&cpu1_sram>;
        status = "okay";
    };
};
```

## Kconfig

There is no driver and no `DEVICE_DT_DEFINE`. Kconfig symbols are only needed
if the SoC init code guards its CPU1 launch behind a config option.

The standard edtlib-generated symbol is sufficient to gate the build:

```kconfig
# SoC Kconfig or board Kconfig
config SOC_RPI_PICO_CPU1_ENABLE
    bool "Enable Raspberry Pi Pico CPU1 launch"
    default y
    depends on DT_HAS_RASPBERRYPI_RPI_PICO_CPU1_ENABLED
    help
      Launch CPU1 during SoC late initialization. Requires a
      cpu-enable node with compatible = "raspberrypi,rpi-pico-cpu1"
      and status = "okay" in the devicetree.
```

`DT_HAS_RASPBERRYPI_RPI_PICO_CPU1_ENABLED` is generated automatically by
edtlib when a `status = "okay"` node with that compatible exists. No manual
symbol definition is required.

## C / SoC Code Usage

The SoC init code reads from the `cpu-enable` node using standard DT macros.
There is no driver API, no device lookup, and no new subsystem header to
include — only `<zephyr/devicetree.h>`.

### Accessing the node

By node label (most common):

```c
#define CPU1_ENABLE_NODE  DT_NODELABEL(cpu1_enable)
```

By compatible (useful in generic SoC files that do not know the board's node
label):

```c
#define CPU1_ENABLE_NODE \
    DT_COMPAT_GET_ANY_STATUS_OKAY(raspberrypi_rpi_pico_cpu1)
```

### Reading properties

```c
#define HAS_SOURCE  DT_NODE_HAS_PROP(CPU1_ENABLE_NODE, source_memory)

#define CPU1_EXEC_ADDR \
    DT_REG_ADDR(DT_PHANDLE(CPU1_ENABLE_NODE, execution_memory))
#define CPU1_EXEC_SIZE \
    DT_REG_SIZE(DT_PHANDLE(CPU1_ENABLE_NODE, execution_memory))

#if HAS_SOURCE
#define CPU1_SRC_ADDR \
    DT_PARTITION_ADDR(DT_PHANDLE(CPU1_ENABLE_NODE, source_memory))
#define CPU1_SRC_SIZE \
    DT_REG_SIZE(DT_PHANDLE(CPU1_ENABLE_NODE, source_memory))
#endif
```

### Full example: `soc/raspberrypi/rpi_pico/common/soc_cpu1.c`

The existing file changes only in which node the DT macros target. The
mailbox handshake, image copy, and `soc_late_init_hook` are unchanged:

```c
#ifdef CONFIG_SOC_RPI_PICO_CPU1_ENABLE

#define CPU1_ENABLE_NODE  DT_COMPAT_GET_ANY_STATUS_OKAY(raspberrypi_rpi_pico_cpu1)

BUILD_ASSERT(DT_NODE_HAS_STATUS_OKAY(CPU1_ENABLE_NODE),
             "No okay cpu-enable node with compatible raspberrypi,rpi-pico-cpu1");

#define HAS_SOURCE  DT_NODE_HAS_PROP(CPU1_ENABLE_NODE, source_memory)

#define CPU1_EXEC_ADDR \
    DT_REG_ADDR(DT_PHANDLE(CPU1_ENABLE_NODE, execution_memory))
#define CPU1_EXEC_SIZE \
    DT_REG_SIZE(DT_PHANDLE(CPU1_ENABLE_NODE, execution_memory))

#if HAS_SOURCE
#define CPU1_SRC_ADDR \
    DT_PARTITION_ADDR(DT_PHANDLE(CPU1_ENABLE_NODE, source_memory))
#define CPU1_SRC_SIZE \
    DT_REG_SIZE(DT_PHANDLE(CPU1_ENABLE_NODE, source_memory))
#endif

/* ... rest of launch implementation unchanged ... */

#endif /* CONFIG_SOC_RPI_PICO_CPU1_ENABLE */
```

## Vendor Adoption Guide

### Step 1: Add a vendor binding

Create `dts/bindings/cpu_enable/<vendor>,<soc>-cpu1.yaml`:

```yaml
title: <Vendor> <SoC> secondary CPU enable

description: |
  Brief description of the CPU launch sequence.

compatible: "<vendor>,<soc>-cpu1"

include: cpu-enable.yaml

# Add vendor-specific properties only if truly necessary.
# Most vendors will need nothing beyond cpu, execution-memory,
# and source-memory from the base binding.
```

The `compatible` value must match the `enable-method` string used on the cpu
node in the SoC dtsi.

### Step 2: Update the cpu node

In the SoC dtsi, add `enable-method` to the secondary cpu node:

```dts
cpu1: cpu@1 {
    compatible = "<arch>,<cpu-type>";
    reg = <1>;
    status = "disabled";
    enable-method = "<vendor>,<soc>-cpu1";
};
```

Do not add properties beyond `status` and `enable-method`. All launch-related
properties go on the cpu-enable node, not the cpu node.

### Step 3: Provide a cpu-enable node in board DTS

In the board DTS file or a snippet for the CPU0 variant:

```dts
/ {
    cpu1_enable: cpu1-enable {
        compatible = "<vendor>,<soc>-cpu1";
        cpu = <&cpu1>;
        execution-memory = <&sram_cpu1>;
        source-memory = <&cpu1_flash_partition>;  /* omit if not needed */
        status = "okay";
    };
};
```

### Step 4: Read from the node in SoC init code

In the SoC or board init file:

```c
#ifdef CONFIG_<VENDOR>_SOC_CPU1_ENABLE

#define CPU1_ENABLE_NODE \
    DT_COMPAT_GET_ANY_STATUS_OKAY(<vendor>_<soc>_cpu1)

#define CPU1_EXEC_ADDR \
    DT_REG_ADDR(DT_PHANDLE(CPU1_ENABLE_NODE, execution_memory))
#define CPU1_EXEC_SIZE \
    DT_REG_SIZE(DT_PHANDLE(CPU1_ENABLE_NODE, execution_memory))

static void launch_cpu1(void)
{
    /* vendor-specific launch sequence using CPU1_EXEC_ADDR etc. */
}

void soc_late_init_hook(void)
{
    launch_cpu1();
}

#endif
```

Boards that do not need image relocation (secondary CPU image flashed directly
to its execution address) omit `source-memory` from the DTS node and omit the
`HAS_SOURCE` guard and copy step from their SoC code.

Vendors whose launch must happen before `soc_late_init_hook` use
`soc_early_init_hook` instead. Because the DT macros resolve to compile-time
constants and the SoC launch code uses direct register access, there is no
dependency on driver initialization order.

### Step 5: Add a Kconfig symbol

```kconfig
config <VENDOR>_SOC_CPU1_ENABLE
    bool "Enable <Vendor> <SoC> CPU1 launch"
    default y
    depends on DT_HAS_<VENDOR>_<SOC>_CPU1_ENABLED
```

## Migration from ad-hoc `/chosen` properties

Boards using `/chosen` properties such as `zephyr,code-cpu1-partition` or
`zephyr,sram-cpu1-partition` can migrate independently. The migration pattern
is:

```dts
/* Before */
/ {
    chosen {
        zephyr,code-cpu1-partition = &cpu1_slot0_partition;
        zephyr,sram-cpu1-partition = &sram0_cpu1;
    };
};

/* After */
/ {
    cpu1_enable: cpu1-enable {
        compatible = "<vendor>,<soc>-cpu1";
        cpu = <&cpu1>;
        execution-memory = <&sram0_cpu1>;
        source-memory = <&cpu1_slot0_partition>;
        status = "okay";
    };
};
```

```c
/* Before */
#define CPU1_EXEC_ADDR \
    DT_REG_ADDR(DT_CHOSEN(zephyr_sram_cpu1_partition))
#define CPU1_SRC_ADDR \
    DT_PARTITION_ADDR(DT_CHOSEN(zephyr_code_cpu1_partition))

/* After */
#define CPU1_ENABLE_NODE \
    DT_COMPAT_GET_ANY_STATUS_OKAY(<vendor>_<soc>_cpu1)
#define CPU1_EXEC_ADDR \
    DT_REG_ADDR(DT_PHANDLE(CPU1_ENABLE_NODE, execution_memory))
#define CPU1_SRC_ADDR \
    DT_PARTITION_ADDR(DT_PHANDLE(CPU1_ENABLE_NODE, source_memory))
```

## Summary of Changes Required

| File | Change |
|---|---|
| `dts/bindings/cpu_enable/cpu-enable.yaml` | New — base binding |
| `dts/bindings/cpu_enable/<vendor>,<soc>-cpu1.yaml` | New — vendor binding per SoC |
| `dts/bindings/cpu/cpu.yaml` | Update `enable-method` description |
| SoC dtsi (`/cpus/cpu@1`) | Add `status = "disabled"` and `enable-method` |
| Board DTS (CPU0 variant) | Add `cpu-enable` node |
| SoC init `.c` | Change DT macros to target `cpu-enable` node |
| SoC `Kconfig` | Add `depends on DT_HAS_..._ENABLED` symbol |

## What Does Not Change

- The cpu node's `compatible` string — no vendor compatible is added
- Architecture bindings (`arm,cortex-m33.yaml`, `riscv-common.yaml`) — untouched
- The SoC launch implementation (mailbox handshake, memcpy, register writes)
- No new driver subsystem, no new kernel API, no new header

## Open Questions

### `enable-method` on the cpu node is not needed

The DT spec's `enable-method` property exists for the pattern where you inspect
a disabled cpu node and need to know what mechanism to use to enable it. In this
design the relationship is inverted: the cpu-enable node declares which cpu it
controls via the `cpu` phandle. The SoC code finds the cpu-enable node by
compatible and reads everything it needs from there — it never reads
`enable-method` from the cpu node.

The cpu node therefore only needs `status = "disabled"`:

```dts
cpu1: cpu@1 {
    compatible = "arm,cortex-m33";
    reg = <1>;
    status = "disabled";
    /* enable-method not required */
};
```

The presence of a `status = "okay"` cpu-enable node with `cpu = <&cpu1>` is
what tells the SoC code to launch CPU1. If no such node exists or it is
`status = "disabled"`, CPU1 is not launched.

Keeping `enable-method` would also require maintaining an unvalidated string
coupling between the cpu node's `enable-method` value and the cpu-enable node's
`compatible` — there is no edtlib mechanism to enforce that these match.

This removes the need to update `cpu.yaml`'s `enable-method` description
entirely, and simplifies the cpu node dtsi entries across all affected SoCs.

### One cpu-enable node per secondary CPU is sufficient

A child-binding approach (one parent node with per-CPU child nodes keyed by
`reg`) was considered to avoid defining N nodes for N secondary CPUs. This adds
`#address-cells`, `#size-cells`, and a `reg` property as pure structural
overhead for the common case of exactly one secondary CPU, which covers the
majority of AMP SoCs in Zephyr's tree. The flat node-per-CPU approach is
simpler, and for the rare SoC with multiple secondary CPUs the verbosity of N
flat nodes is preferable to the complexity of the child-binding pattern.
