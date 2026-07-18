# RFC: Add vendor-defined `enable-method` support to `/cpus/cpu*` DT bindings

## Problem Description

Zephyr currently supports heterogeneous, multi-core SoCs to run in an AMP configuration through the [hardware model V2](https://docs.zephyrproject.org/latest/hardware/porting/board_porting.html#transition-to-the-current-hardware-model).

On some multi-core SoCs, the primary CPU is responsible for moving the secondary
CPUs image from flash into RAM before launching the secondary CPU.  On others,
the primary CPU still needs to know about the secondary CPU's image to know
where to point the secondary CPU to begin executing from. 

Thus boards have taken to using custom properties in `/chosen` nodes in their
devicetree to give the primary CPU's board variant knowledge of the secondary
CPU's flash and SRAM.

Below is a nonexhaustive table of the boards that use `/chosen` nodes for this purpose.

| Property name | Boards using it |
|---|---|
| `zephyr,code-cpu1-partition` | lpcxpresso55s69, lpcxpresso54114, frdm_mcxn947, mcx_nx4x_evk, max32666evkit |
| `zephyr,sram-cpu1-partition` | lpcxpresso55s69 |
| `zephyr,code-m7-partition` | frdm_imxrt1186, mimxrt1180_evk |
| `zephyr,code-rv32-partition` | max78002evkit, max32690evkit, max32655evkit, apard32690 |
| `zephyr,cpu1-region` | mimxrt1170_evk, mimxrt1160_evk |

These ad-hoc `/chosen` properties are unsustainable, but **Zephyr does not have a
standard devicetree-based mechanism to describe the information required by a primary CPU to launch a secondary CPU**.

This RFC is motivated by discussion in https://github.com/zephyrproject-rtos/zephyr/pull/109953

## Proposed Change (Summary)

Originally, this RFC proposed to use an additional `enable-method` binding added to the `compatible` stringlist of the of the secondary `/cpus/cpu*` nodes to include the extra properties needed to launch the secondary CPU in an attempt to fit the [Devicetree Specification (section 3.8.1)](https://devicetree-specification.readthedocs.io/en/latest/chapter3-devicenodes.html#general-properties-of-cpus-cpu-nodes).

**The above proposal did not work** for cpus that have additional properties to the base `cpu.yaml` such as `riscv.yaml` because of how edtlib.py matches bindings to compatibles. For example:

```
&cpu1 {
    compatible = "raspberrypi,rpi-pico-cpu1", "riscv"; 
    enable-method = "raspberrypi,rpi-pico-cpu1";
    /* Props from riscv.yaml */
    /* Props from raspberypi,rpi-pico-cpu1.yaml */
};
```

Edtlib matches to the first binding that is available, and does not include the properties from the rest compatibles. Thus, it outputs an error because there are properties `/cpus/cpu*` node that do not match a property definitions in the `raspberrypi,rpi-pico-cpu1.yaml` binding which would have match the `riscv.yaml` binding.

### Updated Proposal

Create a **separate `cpu-enable` node** that has the required information to launch the secondary CPU board's application. Leaving the `/cpus/cpu*` as is. 

```
/ {
	cpu1_en: cpu1-en {
		compatible = "raspberrypi,pico-pcbs";
		cpu = <&cpu1>;
		execution-memory = <&sram0_cpu1>;
		source-memory = <&cpu1_slot0_partition>;
		status = "okay";
	};
};
```

1. Create `dts/bindings/cpu_enable/` directory 






The [Devicetree Specification (section 3.8.1)](https://devicetree-specification.readthedocs.io/en/latest/chapter3-devicenodes.html#general-properties-of-cpus-cpu-nodes)
documents the `[vendor],[method]` 

value for the `enable-method` property of a `/cpus/cpu*` node to describe
vendor-specific CPU enable methods, but Zephyr's `cpu.yaml` does not currently
document this as an option.

1. Update `dts/bindings/cpu/cpu.yaml` to document the `[vendor],[method]`
   value format for `enable-method`.

2. Create `dts/bindings/cpu/enable-method/` as the home for
   vendor-specific CPU enable-method bindings. Each binding in this directory:
   - `include`s `cpu.yaml`
   - sets `compatible` to `"[vendor],[method]"`
   - const's `enable-method` to the same value as the `compatible`
   - declares any vendor-specific enable-method properties

3. Add the first vendor enable-method binding,
   `dts/bindings/cpu/enable-method/raspberrypi,rpi-pico-cpu1.yaml`, as a
   reference implementation for the Raspberry Pi Pico family.
  
4. Migrate other boards to use `enable-method` standard over vendor defined
   properties `/chosen` node.

5. Clarify `status` property values for `/cpus/cpu` nodes and zephyr's semantics
   around it.

Example usage:

```
/* CPU 1 disabled */
&cpu1 {
    compatible = "raspberrypi,rpi-pico-cpu1", "arm,cortex-m33"; 
    enable-method = "raspberrypi,rpi-pico-cpu1";
    status = "disabled";
};

/* CPU 1 enabled */
&cpu1 {
    compatible = "raspberrypi,rpi-pico-cpu1", "arm,cortex-m33"; 
    enable-method = "raspberrypi,rpi-pico-cpu1";
    execution-memory = <&sram0_cpu1>;
    source-memory = <&cpu1_slot0_partition>;
    status = "okay";
};
```

This provides a standardized devicetree method to give the primary CPU's board
the necessary information without using the custom `/chosen` nodes. This takes
advantage of how multiple compatibles are parsed and that there are not drivers
to match to cpu compatibles.

## Proposed Change (Detailed)

### `/cpus/cpu*` status property

The [Devicetree Specification (section 3.8.1)](https://devicetree-specification.readthedocs.io/en/latest/chapter3-devicenodes.html#general-properties-of-cpus-cpu-nodes) also describes the status property values for CPU nodes in an SMP configuration as follows

| Value | Description | 
| --- | ------- |
| `"okay"` | The CPU is running |
| `"disabled"` | The CPU is in a quiescent state |
| `"fail"` | The CPU is not operational/does not exist |

A CPU with status `"disabled"` must have an `enabled-method` property.

This RFC proposes to use similar status values for AMP configuration. 

| Value | Description | 
| --- | ------- |
| `"okay"` | The CPU is running on entry to `main` |
| `"disabled"` | The CPU is in a quiescent state on entry to `main` |
| `"fail"`| The CPU is not operational/does not exist and there is no way to enable it |

A CPU with status `"disabled"` may be enabled by the application with the
information from the `enable-method`.

### Changes to `dts/bindings/cpu/cpu.yaml`

Update `cpu.yaml`'s `enable-method` description.

```yaml
  enable-method:
    type: string
    description: |
      Method by which a CPU in a disabled state is enabled. This property is
      required for CPUs with a status property with a value of disabled. The
      value may be one of the following:
      - "psci": ARM Power State Coordination Interface.
      - "spin-table": Standard DTSpec spin-table; requires cpu-release-addr.
      - "[vendor],[method]": Vendor-defined method

      For details, see "3.8.1 General Properties of /cpus/cpu* nodes" in
      Devicetree Specification v0.4.
  cpu-release-addr:
    type: int
    description: |
      Specifies the physical address of a spin table entry that releases a
      secondary CPU from its spin loop. Required if enable-method has the
      property value of "spin-table".
```

### New directory: `dts/bindings/cpu/enable-method/`

This new directory is created inside of the `cpu/` directory, which I believe is
a first for `dts/bindings/`, because the enable-method is not a cpu, but a
property of the cpu.

Each vendor specific enable-method binding should follow the template:

```yaml
# dts/bindings/cpu/enable-method/[vendor],[method].yaml

title: <Human-readable title>
description: |
  Description of the CPU launch sequence.

compatible: "[vendor],[method]"

include: cpu.yaml

properties:
  enable-method:
    const: "[vendor],[method]"

  # Vendor-specific launch properties

```

For example, the Raspberry Pi Pico family's enable-method binding for its
secondary CPU is summarized as:

```yaml
title: Raspberry Pi Pico series secondary CPU enable-method

description: # See linked draft PR

compatible: "raspberrypi,rpi-pico-cpu1"

include: cpu.yaml

properties:
  enable-method:
    const: "raspberrypi,rpi-pico-cpu1"

  execution-memory:
    type: phandle
    required: true
    description: |
      Phandle to the memory region or partition from which CPU1 will execute.

  source-memory:
    type: phandle
    description: |
      Phandle to the memory region or partition from which the CPU1 code will be
      loaded into execution-memory.
```

A draft PR with the enable-method binding and the pico2 CPU launch:
https://github.com/zephyrproject-rtos/zephyr/pull/110204

To enable CPU1, the primary CPU's devicetree should be updated:

```dts
/* Before: cpu1 node no launch info */
/ {
    chosen { 
        zephyr,code-cpu1-partition = &cpu1_slot0_partition;
        zephyr,sram-cpu1-partition = &sram0_cpu1;
    }; 
};

&cpu1 {
    compatible = "arm,cortex-m33";
    reg = <1>;
};

/* After: cpu1 node has enable-method with memory phandles */
&cpu1 {
    compatible = "raspberrypi,rpi-pico-cpu1", "arm,cortex-m33";
    enable-method = "raspberrypi,rpi-pico-cpu1";
    execution-memory = <&sram0_cpu1>;
    source-memory = <&cpu1_slot0_partition>;
    status = "okay";
};
```

In Kconfig:

```Kconfig
# Before:
$(dt_chosen_reg_addr_hex,$(DT_CHOSEN_Z_CODE_CPU1_PARTITION))

# After:
$(dt_nodelabel_enabled_with_compat,cpu1,$(DT_COMPAT_RASPBERRYPI_RPI_PICO_CPU1))
```

In C files: 

```c
/* Before */
DT_CHOSEN(zephyr_code_cpu1_partition)

/* After */
DT_PHANDLE(DT_NODE_HAS_STATUS_OKAY(DT_NODELABEL(cpu1)), source_memory)
```

See [#109953](https://github.com/zephyrproject-rtos/zephyr/pull/109953) for
snippets implementation.

## Dependencies

- **`cpu.yaml`**: The description update is additive. No existing DTS, binding,
  or code file needs to change as a result of the `cpu.yaml` modification alone.
- **PR dependency**: The `raspberrypi,rpi-pico-cpu1` binding is part of
  [#109953](https://github.com/zephyrproject-rtos/zephyr/pull/109953) / the
  split-off [#110204](https://github.com/zephyrproject-rtos/zephyr/pull/110204).
  The `cpu.yaml` description update can be made a separate PR.

### Migration table for existing in-tree implementations

Each board can be migrated independently in follow-up PRs before the next
release window if this RFC is accepted.

The following boards use ad-hoc `/chosen` properties that could be replaced by
this convention in follow-up PRs:

| Board DTS | Current chosen property |
|---|---|
| [`nxp/lpcxpresso55s69/.../cpu0.dts:42`](https://github.com/zephyrproject-rtos/zephyr/blob/main/boards/nxp/lpcxpresso55s69/lpcxpresso55s69_lpc55s69_cpu0.dts#L42) | `zephyr,code-cpu1-partition`, `zephyr,sram-cpu1-partition` |
| [`nxp/lpcxpresso54114/.../m0.dts:19`](https://github.com/zephyrproject-rtos/zephyr/blob/main/boards/nxp/lpcxpresso54114/lpcxpresso54114_lpc54114_m0.dts#L19) | `zephyr,code-cpu1-partition` |
| [`nxp/frdm_mcxn947/.../cpu0.dtsi:30`](https://github.com/zephyrproject-rtos/zephyr/blob/main/boards/nxp/frdm_mcxn947/frdm_mcxn947_mcxn947_cpu0.dtsi#L30) | `zephyr,code-cpu1-partition` |
| [`nxp/mcx_nx4x_evk/.../cpu0.dtsi:25`](https://github.com/zephyrproject-rtos/zephyr/blob/main/boards/nxp/mcx_nx4x_evk/mcx_nx4x_evk_cpu0.dtsi#L25) | `zephyr,code-cpu1-partition` |
| [`nxp/frdm_imxrt1186/.../cm33.dts:25`](https://github.com/zephyrproject-rtos/zephyr/blob/main/boards/nxp/frdm_imxrt1186/frdm_imxrt1186_mimxrt1186_cm33.dts#L25) | `zephyr,code-m7-partition` |
| [`nxp/mimxrt1180_evk/.../cm33.dts:24`](https://github.com/zephyrproject-rtos/zephyr/blob/main/boards/nxp/mimxrt1180_evk/mimxrt1180_evk_mimxrt1189_cm33.dts#L24) | `zephyr,code-m7-partition` |
| [`adi/max32666evkit/.../cpu0.dts:15`](https://github.com/zephyrproject-rtos/zephyr/blob/main/boards/adi/max32666evkit/max32666evkit_max32666_cpu0.dts#L15) | `zephyr,code-cpu1-partition` |
| [`adi/max32690evkit/.../m4.dts:27`](https://github.com/zephyrproject-rtos/zephyr/blob/main/boards/adi/max32690evkit/max32690evkit_max32690_m4.dts#L27) | `zephyr,code-rv32-partition` |
| [`adi/max32655evkit/.../m4.dts:22`](https://github.com/zephyrproject-rtos/zephyr/blob/main/boards/adi/max32655evkit/max32655evkit_max32655_m4.dts#L22) | `zephyr,code-rv32-partition` |
| [`adi/apard32690/.../m4.dts:28`](https://github.com/zephyrproject-rtos/zephyr/blob/main/boards/adi/apard32690/apard32690_max32690_m4.dts#L28) | `zephyr,code-rv32-partition` |
| [`adi/max78002evkit/.../m4.dts:26`](https://github.com/zephyrproject-rtos/zephyr/blob/main/boards/adi/max78002evkit/max78002evkit_max78002_m4.dts#L26) | `zephyr,code-rv32-partition` |
| [`nxp/mimxrt1170_evk/.../cm7.dts:32`](https://github.com/zephyrproject-rtos/zephyr/blob/main/boards/nxp/mimxrt1170_evk/mimxrt1170_evk_mimxrt1176_cm7.dts#L32) | `zephyr,cpu1-region` |
| [`nxp/mimxrt1160_evk/.../cm7.dts:27`](https://github.com/zephyrproject-rtos/zephyr/blob/main/boards/nxp/mimxrt1160_evk/mimxrt1160_evk_mimxrt1166_cm7.dts#L27) | `zephyr,cpu1-region` |

There may be more, but this is what AI tools and I could find.

## Concerns and Unresolved Questions

1. **Binding location**: Should vendor enable-method bindings live in
`dts/bindings/cpu/enable-method/` (proposed) or in a separate
`dts/bindings/cpu_enable_method/`?

2. **To follow disabled semantics or not**: According the the DT spec, a cpu can
have a `status = "disabled"`, but a cpu with the status of
disabled requires an `enable-method` property. 

    Currently, edtlib does not verify `required: true` on `status = "disabled"`
    nodes, so required properties added by the `enable-method` bindings are not
    checked. This works now, but may not in the future if edtlib is updated.

3. **enable-method as stringlist**: The DT spec defines `enable-method` as a
`stringlist`. Should the cpu.yaml binding change the `enable-method` type to
`string-array`? This would be a breaking change for SoCs that use the value from
`enable-method`, I do not think there currently is one.

## Alternatives Considered

### Vendor-specific `zephyr,` chosen nodes (current state)

`zephyr,code-cpu1-partition`, `zephyr,code-rv32-partition`,
`zephyr,code-m7-partition`, and `zephyr,sram-cpu1-partition` are already in-tree
but have no shared naming convention, are not attached to the CPU node. This
approach does not scale. The discussion in
[#109953](https://github.com/zephyrproject-rtos/zephyr/pull/109953) explicitly
flagged these as non-sustainable.

### Coprocessor driver/binding in `dts/bindings/misc/`

Modeled after `nordic,nrf-vpr-coprocessor`, which represents an actual
memory-mapped peripheral at a fixed address on the nRF54 series. For most other
Cortex-M multi-core SoCs, there is no such hardware device. This approach was
proposed and explicitly rejected in
[#93379](https://github.com/zephyrproject-rtos/zephyr/pull/93379).

### Secondary CPU-based compatible string

Instead of adding the `dts/bindings/cpu/enable-method/` directory, write binding
files for secondary CPU's of SoCs that add the necessary enable-method
properties by include or otherwise to the cpu's binding. 

This was considered, but the number of bindings files in `dts/bindings/cpu/`
will quickly balloon. For only the Rasberry Pi Pico family, we would need to add
a minimum of 3 new binding files:

- `raspberrypi,m33-cpu1.yaml`
- `raspberrypi,hazard3-cpu1.yaml`
- `raspberrypi,m0+-cpu1.yaml`
- `raspberrypi,cpu1.yaml` (optional for reuse)

### enable-method as string match to separate node's compatible

Instead of adding compatible to cpu node to add required properties for
enable-method, match the enable-method string to a separate dts node. This is
similar to the `psci` node for Cortex-A SoCs.

```dts
&cpu1 {
    compatible = "arm,cortex-m33";
    enable-method = "raspberrypi,rpi-pico-cpu1";
};
  
/ {
    cpu_launcher {
        compatible = "raspberrypi,rpi-pico-cpu1";
        execution-memory = <&sram0_cpu1>;
        source-memory = <&cpu1_slot0_partition>;
        status = "okay";
    };
};
```

- This is nice looking, but there is currently no way to ensure the
enable-method matches the correct compat string of the other node. Would need
changes to edtlib.py?
- Should `enable-methods` then be required to be a `pm_cpu_ops` implementation
like `psci`? The `pm_cpu_ops` driver class is currently only used for SMP, and
still leaves the question of how do we deal with primary CPUs moving secondary
CPU's image?

## Todos

If RFC is accepted: 

- [ ] Add documentation on this to board porting guide
- [ ] Migrate existing boards to use `enable-method` and `status semantics
