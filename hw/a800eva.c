/*
 * Atmark Techno Armadillo-800 EVA emulation.
 *
 * Copyright (c) 2012 Masashi YOKOTA <yktmss@gmail.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#include "sysbus.h"
#include "arm-misc.h"
#include "exec-memory.h"
#include "boards.h"

#define SDRAM_START 			        (0x40000000)
#define SDRAM_SIZE  			        (0x20000000)
#define SRAM_START			            (0xe80c0000)
#define SRAM_SIZE			            (0x00040000)
#define A800_AXI_PERI_VIRT_START    	(0xf1000000)
#define A800_AXI_PERI_PHYS_START    	(0xc1000000)
#define A800_AXI_PERI_LENGTH        	(0x07000000)

static struct arm_boot_info a800_binfo = {};

static inline target_phys_addr_t v2p(target_phys_addr_t addr)
{
	/* 0xc1000000-0xc7ffffff -> 0xf1000000-0xf7ffffff (AXI Peri) */
    if (A800_AXI_PERI_VIRT_START <= addr &&
        addr < (A800_AXI_PERI_VIRT_START + A800_AXI_PERI_LENGTH))
        return  A800_AXI_PERI_PHYS_START + (addr - A800_AXI_PERI_VIRT_START);

    return addr;
}

static void a800_init(ram_addr_t ram_size, const char *boot_device,
                        const char *kernel_filename, const char *kernel_cmdline,
                        const char *initrd_filename, const char *cpu_model)
{
    CPUARMState *env = NULL;
    MemoryRegion *sysmem = get_system_memory();
    MemoryRegion *sdram = g_new(MemoryRegion, 1);
    MemoryRegion *sram = g_new(MemoryRegion, 1);
    DeviceState *dev;
    SysBusDevice *busdev;
    qemu_irq *irqp;
    qemu_irq pic[128];
    qemu_irq cpu_irq;
    int n;

    if (!cpu_model) {
        cpu_model = "cortex-a9";
    }

    env = cpu_init(cpu_model);
    if (!env) {
        fprintf(stderr, "Unable to find CPU definition\n");
        exit(1);
    }

    irqp = arm_pic_init_cpu(env);
    cpu_irq = irqp[ARM_PIC_CPU_IRQ];

    memory_region_init_ram(sdram, "a800eva.sdram", SDRAM_SIZE);
    vmstate_register_ram_global(sdram);
    memory_region_add_subregion(sysmem, SDRAM_START, sdram);

    memory_region_init_ram(sram, "a800eva.sram", SRAM_SIZE);
    vmstate_register_ram_global(sram);
    memory_region_add_subregion(sysmem, SRAM_START, sram);

    /* External GIC */
    dev = qdev_create(NULL, "r-mobile_a1.gic");
    qdev_init_nofail(dev);
    busdev = sysbus_from_qdev(dev);
    /* Map CPU interface */
    sysbus_mmio_map(busdev, 0, v2p(0xf2000000));
    /* Map Distributer interface */
    sysbus_mmio_map(busdev, 1, v2p(0xf2800000));
    sysbus_connect_irq(busdev, 0, cpu_irq);

    for (n = 0; n < 128; n++) {
        pic[n] = qdev_get_gpio_in(dev, n);
    }

    sysbus_create_varargs("l2x0", 0xf0100000, NULL);

    sysbus_create_varargs("sh,scif", 0xe6c50000, 
                          pic[101], NULL);

    sysbus_create_varargs("sh,cmt", 0xe6138000, 
                          pic[58], pic[59], pic[60], 
                          pic[61], pic[62], NULL);

    a800_binfo.ram_size = ram_size;
    a800_binfo.kernel_filename = kernel_filename;
    a800_binfo.kernel_cmdline = kernel_cmdline;
    a800_binfo.initrd_filename = initrd_filename;
    a800_binfo.nb_cpus = 1;
    a800_binfo.board_id = 3863;
    a800_binfo.loader_start = SDRAM_START;
    arm_load_kernel(first_cpu, &a800_binfo);
}

static QEMUMachine a800_eva_machine = {
    .name = "a800eva",
    .desc = "Armadillo-800 EVA Platform Baseboard",
    .init = a800_init,
};

static void a800_machine_init(void)
{
    qemu_register_machine(&a800_eva_machine);
}

machine_init(a800_machine_init);

