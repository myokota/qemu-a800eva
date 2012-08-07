/*
 * QEMU model of the SH CMT.
 *
 * Copyright (c) 2012 Masashi Yokota <yktmss@gmail.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#include "sysbus.h"
#include "qemu-timer.h"
#include "ptimer.h"

#if 0
#define D(x) (x)
#else
#define D(x)
#endif

enum {
    R_CSR,
    R_CNT,
    R_COR,
    R_MAX,
};

#define CSR_CMF   (1 << 15)
#define CSR_OVF   (1 << 14)
#define CSR_CMR   (3 << 4)
#define CSR_CKS   (3 << 0)

struct cmt_unit {
    QEMUBH *bh;
    qemu_irq irq;
    int nr;
    ptimer_state *ptimer;
    int enabled;
    uint32_t regs[R_MAX];
};

struct sh_cmt {
    SysBusDevice busdev;
    MemoryRegion mmio;
    uint32_t nr_timers;
    uint32_t freq_hz;
    uint32_t cmstr; /* share reg */
    struct cmt_unit *timers;
};

static inline unsigned int timer_from_addr(target_phys_addr_t addr)
{
    return (addr >> 4) - 1;
}

static void cmt_update(struct cmt_unit *t)
{
    int irq = (t->regs[R_CSR] & CSR_CMF) != 0;

    if (irq && ((t->regs[R_CSR] & CSR_CMR) == 0))
        return;

    qemu_set_irq(t->irq, irq);
}

static uint64_t
timer_read(void *opaque, target_phys_addr_t addr, unsigned int size)
{
    struct sh_cmt *t = opaque;
    struct cmt_unit *cmt;
    uint32_t r = 0;
    unsigned int timer;

    if (addr == 0) {
        r = t->cmstr;
        goto out;
    }

    timer = timer_from_addr(addr);
    assert(timer <= t->nr_timers);
    cmt = &t->timers[timer];

    addr &= 0xf;
    if (size == 4)
        addr >>= 2;
    else if (size == 2)
        addr >>= 1;

    /* Further decoding to address a specific timers reg.  */
    switch (addr) {
    case R_CNT:
        if (cmt->enabled)
            r = cmt->regs[R_COR] - (uint32_t)ptimer_get_count(cmt->ptimer);
        else
            r = 0;
        break;
    default:
        if (addr < ARRAY_SIZE(cmt->regs))
            r = cmt->regs[addr];
        else
            hw_error("%s: Bad addr %x\n", __func__, addr);
        break;
    }
out:
    D(printf("%s addr=%x timer=%d %x=%x\n", __func__, addr, timer, addr * 4, r));
    return r;
}

static void cmt_start_stop(void *opaque, int enable)
{
    struct cmt_unit *t = (struct cmt_unit *)opaque;

    if (t->enabled && !enable) {
        ptimer_stop(t->ptimer);
    }
    if (!t->enabled && enable) {
        ptimer_run(t->ptimer, 0);
    }
    t->enabled = !!enable;
}

static void
timer_write(void *opaque, target_phys_addr_t addr,
            uint64_t val64, unsigned int size)
{
    struct sh_cmt *t = opaque;
    struct cmt_unit *cmt;
    uint32_t value = val64;
    uint32_t timer;
    uint32_t freq_hz;
    int64_t cnt;

    if (addr == 0) {
        int i;
        for (i = 0; i < t->nr_timers; i++)
            cmt_start_stop(&t->timers[i], (value & (1 << i)) != 0);
        t->cmstr = value;
        goto out;
    }

    timer = timer_from_addr(addr);
    assert(timer <= t->nr_timers);
    cmt = &t->timers[timer];

    addr &= 0xf;
    if (size == 4)
        addr >>= 2;
    else if (size == 2)
        addr >>= 1;

    switch (addr) {
    case R_COR:
        ptimer_set_limit(cmt->ptimer, value, true);
        cmt->regs[R_COR] = value;
        break;
 
    case R_CNT:
        cnt = cmt->regs[R_COR] - value;
        if (cnt < 0)
            cnt = cmt->regs[R_COR];
        cmt->regs[R_CNT] = cnt;
        ptimer_set_count(cmt->ptimer, cnt);
        break;

    case R_CSR:
        cmt->regs[R_CSR] &= ~(CSR_CKS | CSR_CMR | CSR_CKS);
        cmt->regs[R_CSR] |= value & (CSR_CKS | CSR_CMR | CSR_CKS);
        
        if (cmt->enabled) {
            /* Pause the timer if it is running.  This may cause some
               inaccuracy dure to rounding, but avoids a whole lot of other
               messyness.  */
            ptimer_stop(cmt->ptimer);
        }

        switch (value & CSR_CKS) {
            case 0: freq_hz = t->freq_hz >> 3; break;
            case 1: freq_hz = t->freq_hz >> 5; break;
            case 2: freq_hz = t->freq_hz >> 7; break;
            case 3: freq_hz = t->freq_hz; break;
            default: hw_error("%s: Reserved CKS value\n", __func__); break;
        }

        if (((value & CSR_CMF) == 0) &&
            (cmt->regs[R_CSR] & CSR_CMF)) {
                cmt->regs[R_CSR] &= ~CSR_CMF;
                cmt->regs[R_CSR] |= CSR_OVF;
         }

        if (((value & CSR_OVF) == 0) &&
            (cmt->regs[R_CSR] & CSR_OVF)) {
            cmt->regs[R_CSR] &= ~CSR_OVF;
        }

        ptimer_set_freq(cmt->ptimer, freq_hz);

        if (cmt->enabled) {
            /* Restart the timer if still enabled.  */
            ptimer_run(cmt->ptimer, 0);
        }

        cmt_update(cmt);
        break;

    default:
        hw_error("%s: Bad addr %x\n", __func__, addr);
        break;
    }
out:
    D(printf("%s addr=%x val=%x (timer=%d off=%d)\n",
             __func__, addr * 4, value, timer, addr & 3));
}

static const MemoryRegionOps timer_ops = {
    .read = timer_read,
    .write = timer_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
#if 0
    .valid = {
        .min_access_size = 2,
        .max_access_size = 4
    }
#endif
};

static void cmt_tick(void *opaque)
{
    struct cmt_unit *t = (struct cmt_unit *)opaque;

    t->regs[R_CSR] |= CSR_CMF;
    cmt_update(t);
}

static int sh_cmt_init(SysBusDevice *dev)
{
    struct sh_cmt *t = FROM_SYSBUS(typeof (*t), dev);
    unsigned int i;

    /* Init all the ptimers.  */
    t->timers = g_malloc0(sizeof(t->timers[0]) * t->nr_timers);
    for (i = 0; i < t->nr_timers; i++) {
        struct cmt_unit *cmt = &t->timers[i];

        cmt->nr = i;
        cmt->regs[R_CSR] = 0;
        cmt->regs[R_CNT] = 0;
        cmt->regs[R_COR] = 0xffffffff;
        cmt->enabled = 0;
        cmt->bh = qemu_bh_new(cmt_tick, cmt);
        cmt->ptimer = ptimer_init(cmt->bh);
        ptimer_set_freq(cmt->ptimer, t->freq_hz >> 3);
        sysbus_init_irq(dev, &cmt->irq);
    }

    memory_region_init_io(&t->mmio, &timer_ops, t, "sh,cmt", 0x1000);
    sysbus_init_mmio(dev, &t->mmio);
    return 0;
}

static Property sh_cmt_properties[] = {
    DEFINE_PROP_UINT32("frequency", struct sh_cmt, freq_hz,   50000000),
    DEFINE_PROP_UINT32("nr-timers", struct sh_cmt, nr_timers, 5),
    DEFINE_PROP_END_OF_LIST(),
};

static void sh_cmt_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    SysBusDeviceClass *k = SYS_BUS_DEVICE_CLASS(klass);

    k->init = sh_cmt_init;
    dc->props = sh_cmt_properties;
}

static TypeInfo sh_cmt_info = {
    .name          = "sh,cmt",
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(struct sh_cmt),
    .class_init    = sh_cmt_class_init,
};

static void sh_cmt_register_types(void)
{
    type_register_static(&sh_cmt_info);
}

type_init(sh_cmt_register_types)
