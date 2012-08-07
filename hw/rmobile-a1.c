/*
 * Renesas R-Mobile A1 System emulation.
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

#define RMOBILEA1_GIC_NIRQ 		        (32+128)
#define RMOBILEA1_GIC_CPU_REGION_SIZE  	(0x100)
#define RMOBILEA1_GIC_DIST_REGION_SIZE 	(0x1000)

typedef struct {
    SysBusDevice busdev;
    MemoryRegion cpu_container;
    MemoryRegion dist_container;
    DeviceState *gic;
} RMobileA1GicState;

static void rmobile_gic_set_irq(void *opaque, int irq, int level)
{
    RMobileA1GicState *s = (RMobileA1GicState *)opaque;
    qemu_set_irq(qdev_get_gpio_in(s->gic, irq), level);
}

static int rmobile_gic_init(SysBusDevice *dev)
{
    RMobileA1GicState *s = FROM_SYSBUS(RMobileA1GicState, dev);
    SysBusDevice *busdev;

    s->gic = qdev_create(NULL, "arm_gic");
    qdev_prop_set_uint32(s->gic, "num-cpu", 1);
    qdev_prop_set_uint32(s->gic, "num-irq", RMOBILEA1_GIC_NIRQ);
    qdev_init_nofail(s->gic);
    busdev = sysbus_from_qdev(s->gic);

    /* Pass through outbound IRQ lines from the GIC */
    sysbus_pass_irq(dev, busdev);

    /* Pass through inbound GPIO lines to the GIC */
    qdev_init_gpio_in(&s->busdev.qdev, rmobile_gic_set_irq,
                      RMOBILEA1_GIC_NIRQ - 32);

    memory_region_init(&s->cpu_container, "rmobile-cpu-container",
                        RMOBILEA1_GIC_CPU_REGION_SIZE);
    memory_region_init(&s->dist_container, "rmobile-dist-container",
                        RMOBILEA1_GIC_DIST_REGION_SIZE);

    /* Map CPU interface per SMP Core */
    memory_region_add_subregion(&s->cpu_container, 0,
                        sysbus_mmio_get_region(busdev, 1));

    /* Map Distributor per SMP Core */
    memory_region_add_subregion(&s->dist_container, 0,
                        sysbus_mmio_get_region(busdev, 0));

    sysbus_init_mmio(dev, &s->cpu_container);
    sysbus_init_mmio(dev, &s->dist_container);

    return 0;
}

static void rmobile_gic_class_init(ObjectClass *klass, void *data)
{
    SysBusDeviceClass *k = SYS_BUS_DEVICE_CLASS(klass);

    k->init = rmobile_gic_init;
}

static TypeInfo rmobile_gic_info = {
    .name          = "r-mobile_a1.gic",
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(RMobileA1GicState),
    .class_init    = rmobile_gic_class_init,
};

static void rmobile_gic_register_types(void)
{
    type_register_static(&rmobile_gic_info);
}

type_init(rmobile_gic_register_types)
