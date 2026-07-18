
zaproc (Zephyr asymmetric processor)

zaproc_rp2350_cpu1: zaproc {
	compatible = "raspberrypi,pico-pcbs";

	execution-memory = <&sram1>;
	source-memory = <cpu1_slot0_partition>;
};

Kconfig

menuconfig ZAPROC

if ZAPROC

config ZAPROC_HAS_CONFIG
config ZAPROC_HAS_LOAD

rsource other kconfigs

endif 

Kconfig.rpi_pico

config ZAPROC_RPI_PICO
	select ZAPROC_HAS_LOAD
	select SOC_HAS_EARLY_INIT_HOOK
	select SOC_HAS_LATE_INIT_HOOK

In include/zephyr/zaproc.h

zaproc_config()
zaproc_load()
zaproc_start()

