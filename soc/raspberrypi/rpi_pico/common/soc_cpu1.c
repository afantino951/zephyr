/*
 * Copyright (c) 2025 Dan Collins <dan@collinsnz.com>
 * Copyright (c) 2025 Dmitrii Sharshakov <d3dx12.xx@gmail.com>
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/devicetree.h>
#include <zephyr/logging/log.h>
#include <zephyr/kernel.h>

#include <hardware/structs/sio.h>
#include <hardware/structs/psm.h>

#include <zephyr/drivers/misc/mbox_rpi_pico/mbox_rpi_pico.h>

LOG_MODULE_REGISTER(soc_rpi_pico_cpu1, CONFIG_SOC_LOG_LEVEL);

#define CPU1_SRAM_ADDR DT_REG_ADDR(DT_CHOSEN(zephyr_sram_cpu1_partition))
#define CPU1_SRAM_SIZE DT_REG_SIZE(DT_CHOSEN(zephyr_sram_cpu1_partition))

/* Flash partitions have addresses relative to the flash base */
#define CPU1_CODE_ADDR DT_PARTITION_ADDR(DT_CHOSEN(zephyr_code_cpu1_partition))
#define CPU1_CODE_SIZE DT_REG_SIZE(DT_CHOSEN(zephyr_code_cpu1_partition))

static inline void rpi_pico_mailbox_put_blocking(sio_hw_t *const sio_regs, uint32_t value)
{
	while (!rpi_pico_mbox_write_ready(sio_regs)) {
		k_busy_wait(1);
	}

	rpi_pico_mbox_write(sio_regs, value);

	/* Inform other CPU about FIFO update. */
	__SEV();
}

static inline uint32_t rpi_pico_mailbox_pop_blocking(sio_hw_t *const sio_regs)
{
	while (!rpi_pico_mbox_read_valid(sio_regs)) {
		/*
		 * Wait for a message to be available in the FIFO.
		 * Before IRQ is enabled, this is signalled by an event.
		 */
		__WFE();
	}

	return rpi_pico_mbox_read(sio_regs);
}

#if CONFIG_SOC_RPI_PICO_CPU1_RELOCATE_IMAGE
static void rpi_pico_load_cpu1_image(void)
{
	BUILD_ASSERT((CPU1_SRAM_SIZE >= CPU1_CODE_SIZE),
		     "Image size must not exceed execution memory size");

	BUILD_ASSERT(((CPU1_SRAM_ADDR >= CPU1_CODE_ADDR + CPU1_CODE_SIZE) ||
		      (CPU1_CODE_ADDR >= CPU1_SRAM_ADDR + CPU1_SRAM_SIZE)),
		     "Image source memory must not overlap with execution memory");

	void *src_mem = (void *)CPU1_CODE_ADDR;
	void *exec_mem = (void *)CPU1_SRAM_ADDR;

	LOG_DBG("Copying image from %p to %p", src_mem, exec_mem);

	memcpy(exec_mem, src_mem, MIN(CPU1_SRAM_SIZE, CPU1_CODE_SIZE));
}
#endif /* CONFIG_SOC_RPI_PICO_CPU1_RELOCATE_IMAGE */

#ifdef CONFIG_SOC_RPI_PICO_CPU1_ENABLE_CHECK_VTOR
static inline bool address_in_range(uint32_t addr, uint32_t base, uint32_t size)
{
	return addr >= base && addr < base + size;
}

static inline int rpi_pico_validate_vtor(uint32_t cpu1_sp, uint32_t cpu1_pc)
{
/* Where is code located */
#ifdef CONFIG_SOC_RPI_PICO_CPU1_RELOCATE_IMAGE
#define CPU1_MEM_ADDR CPU1_SRAM_ADDR
#define CPU1_MEM_SIZE CPU1_SRAM_SIZE
#else
#define CPU1_MEM_ADDR CPU1_CODE_ADDR
#define CPU1_MEM_SIZE CPU1_CODE_SIZE
#endif /* CONFIG_SOC_RPI_PICO_CPU1_RELOCATE_IMAGE */

	/* Stack pointer shall point within RAM assigned to the core. */
	if (!address_in_range(cpu1_sp, CPU1_SRAM_ADDR, CPU1_SRAM_SIZE)) {
		LOG_ERR("CPU1 stack pointer 0x%08x invalid.", cpu1_sp);
		return -EINVAL;
	}

	LOG_DBG("CPU1 stack pointer: 0x%08x", cpu1_sp);

	/* Initial program counter shall point to the loaded CPU1 code. */
	if (!address_in_range(cpu1_pc, CPU1_MEM_ADDR, CPU1_MEM_SIZE)) {
		LOG_ERR("CPU1 reset pointer 0x%08x invalid.", cpu1_pc);
		return -EINVAL;
	}

	LOG_DBG("CPU1 reset pointer: 0x%08x", cpu1_pc);
	return 0;
}
#endif /* CONFIG_SOC_RPI_PICO_CPU1_ENABLE_CHECK_VTOR */

static int rpi_pico_reset_cpu1(sio_hw_t *const sio_regs, psm_hw_t *const psm_regs)
{
	uint32_t val;

	/* Power off, and wait for it to take effect. */
	hw_set_bits(&psm_regs->frce_off, PSM_FRCE_OFF_PROC1_BITS);
	while (!(psm_regs->frce_off & PSM_FRCE_OFF_PROC1_BITS)) {
		k_busy_wait(1);
	}

	/*
	 * Power back on, and we can wait for a '0' in the FIFO to know
	 * that it has come back.
	 */
	hw_clear_bits(&psm_regs->frce_off, PSM_FRCE_OFF_PROC1_BITS);
	val = rpi_pico_mailbox_pop_blocking(sio_regs);

	return val == 0 ? 0 : -EIO;
}

static void rpi_pico_boot_cpu1(sio_hw_t *const sio_regs, uint32_t vector_table_addr,
			       uint32_t stack_ptr, uint32_t pc)
{
	/* We synchronise with CPU1 and then we can hand over the memory addresses. */
	uint32_t cmds[] = {0, 0, 1, vector_table_addr, stack_ptr, pc};
	uint32_t seq = 0;

	do {
		uint32_t cmd = cmds[seq], rsp;

		if (cmd == 0) {
			/* Flush the mailbox by reading all pending messages. */
			rpi_pico_mbox_drain(sio_regs);

			/* Signal readiness to CPU1 */
			__SEV();
		}

		rpi_pico_mailbox_put_blocking(sio_regs, cmd);
		rsp = rpi_pico_mailbox_pop_blocking(sio_regs);

		seq = (cmd == rsp) ? seq + 1 : 0;
	} while (seq < ARRAY_SIZE(cmds));
}

void soc_late_init_hook(void)
{
#if CONFIG_SOC_RPI_PICO_CPU1_RELOCATE_IMAGE
	rpi_pico_load_cpu1_image();
#endif /* CONFIG_SOC_RPI_PICO_CPU1_RELOCATE_IMAGE */

	uint32_t cpu1_image_base = CONFIG_SOC_RPI_PICO_CPU1_IMAGE_ADDRESS;

	uint32_t *cpu1_vector_table = (void *)cpu1_image_base;
	uint32_t cpu1_sp = cpu1_vector_table[0];
	uint32_t cpu1_pc = cpu1_vector_table[1];

#ifdef CONFIG_SOC_RPI_PICO_CPU1_ENABLE_CHECK_VTOR
	if (rpi_pico_validate_vtor(cpu1_sp, cpu1_pc) != 0) {
		return;
	}
#endif /* CONFIG_SOC_RPI_PICO_CPU1_ENABLE_CHECK_VTOR */

	LOG_DBG("Launching CPU1 with vector table at 0x%p", (void *)cpu1_vector_table);

	if (rpi_pico_reset_cpu1(sio_hw, psm_hw) != 0) {
		LOG_ERR("CPU1 reset failed.");
		return;
	}

	rpi_pico_boot_cpu1(sio_hw, (uint32_t)cpu1_vector_table, cpu1_sp, cpu1_pc);
}
