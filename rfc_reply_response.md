> ### Proposed Change (Detailed)
> ### `/cpus/cpu*` status Property
> The [Devicetree Specification (section 3.8.1)](https://devicetree-specification.readthedocs.io/en/latest/chapter3-devicenodes.html#general-properties-of-cpus-cpu-nodes) also describes the status property values for CPU nodes in an SMP configuration as follows
> 
> Value	Description
> `"okay"`	The CPU is running
> `"disabled"`	The CPU is in a quiescent state
> `"fail"`	The CPU is not operational/does not exist
> A CPU with status `"disabled"` must have an `enabled-method` property.

This part of the Specification seems *heavily* biased towards the typical (i.e., Linux) usecase of Devicetree where it is a dynamic/runtime structure. This is not the case at all in Zephyr so we may want to deviate from the Spec here - e.g., by remaining aligned with Zephyr's use of `status`:
* `okay`: the CPU should be explicitly enabled/used
  * If there is no `enable-method`, it is assumed the CPU is enabled by default on reset
  * If there is an `enable-method`, it shall be used to bring up the CPU during kernel init
* `disabled`: the CPU should be left disabled/unused

BTW, this begs the question of where this AMP bring-up code should go (because "standard" methods could/should share code but wouldn't be able to install a `soc_xxx_hook()` for obvious reasons)

> ### Changes to `dts/bindings/cpu/cpu.yaml`
> Update `cpu.yaml`'s `enable-method` description.
> 
> [...]

I would suggest having `cpu-release-address` in a dedicated binding file which sets `enable-method: const "spin-table"` as suggested for vendor-specific methods.

> ### enable-method as string match to separate node's compatible
> Instead of adding compatible to cpu node to add required properties for enable-method, match the enable-method string to a separate dts node. This is similar to the `psci` node for Cortex-A SoCs.
> 
> ```
> &cpu1 {
>     compatible = "arm,cortex-m33";
>     enable-method = "raspberrypi,rpi-pico-cpu1";
> };
>   
> / {
>     cpu_launcher {
>         compatible = "raspberrypi,rpi-pico-cpu1";
>         execution-memory = <&sram0_cpu1>;
>         source-memory = <&cpu1_slot0_partition>;
>         status = "okay";
>     };
> };
> ```
> 
> * This is nice looking, but there is currently no way to ensure the
>   enable-method matches the correct compat string of the other node. Would need
>   changes to edtlib.py?

Why not add a `cpu = <&cpu1>` phandle in the `cpu_launcher`?

But this actually begs the question of why you'd need an `enable-method` in the first place: if the `cpu_launcher` brings up the CPU (really, the **coprocessor**), then why do we care about what's on the `cpu` node? Which circles back to my question above: how does this integrate in Zephyr?


# my Response


> This part of the Specification seems *heavily* biased towards the typical (i.e., Linux) usecase of Devicetree where it is a dynamic/runtime structure. This is not the case at all in Zephyr so we may want to deviate from the Spec here - e.g., by remaining aligned with Zephyr's use of `status`:
> * `okay`: the CPU should be explicitly enabled/used
>   * If there is no `enable-method`, it is assumed the CPU is enabled by default on reset
>   * If there is an `enable-method`, it shall be used to bring up the CPU during kernel init
> * `disabled`: the CPU should be left disabled/unused

I agree that the spec does lean towards dynamic devicetree. My proposed usage was based on how zephyr still includes disabled nodes in devicetree. Therefore, an application writer could still use the information in the node to launch the CPU however they wish.

I don't think that `status = "fail";` makes much sense on further review.

> BTW, this begs the question of where this AMP bring-up code should go (because "standard" methods could/should share code but wouldn't be able to install a soc_xxx_hook() for obvious reasons)

Eventually, it would be nice to have a CONFIG_AMP similar to CONFIG_SMP. For now, I was just proposing a standard way in Devicetree to give the necessary information to launch secondary CPUs in their vendor defined manner. The actual launch code could stay in their `soc_xxx_hook()`'s 

Alternatively, we can use `SYS_INIT` instead of `soc_xxx_hook()`'s since the `SYS_INIT` levels are intertwined with the soc_xxx_hook()'s (see [`kernel/init.c`](https://github.com/zephyrproject-rtos/zephyr/blob/d4edf519ff9be7c51dbb1da7a3b3a1dcdcad4d8a/kernel/init.c#L532)).
    
> Why not add a cpu = <&cpu1> phandle in the cpu_launcher?

So we would add 

```
cpu-launcher {
    compatible = "raspberrypi,pico-pcbs";
    cpu = <&cpu1>;
    execution-memory = <&sram0_cpu1>;
    source-memory = <&cpu1_slot0_partition>;
    status = "okay";
};
```

> But this actually begs the question of why you'd need an enable-method in the first place: if the cpu_launcher brings up the CPU (really, the coprocessor), then why do we care about what's on the cpu node? Which circles back to my question above: how does this integrate in Zephyr?

I was using the enable method because it was part of the Devicetree spec. 


