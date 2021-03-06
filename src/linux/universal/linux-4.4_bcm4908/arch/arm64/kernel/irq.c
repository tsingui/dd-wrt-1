/*
 * Based on arch/arm/kernel/irq.c
 *
 * Copyright (C) 1992 Linus Torvalds
 * Modifications for ARM processor Copyright (C) 1995-2000 Russell King.
 * Support for Dynamic Tick Timer Copyright (C) 2004-2005 Nokia Corporation.
 * Dynamic Tick Timer written by Tony Lindgren <tony@atomide.com> and
 * Tuukka Tikkanen <tuukka.tikkanen@elektrobit.com>.
 * Copyright (C) 2012 ARM Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <linux/kernel_stat.h>
#include <linux/irq.h>
#include <linux/smp.h>
#include <linux/init.h>
#include <linux/irqchip.h>
#include <linux/seq_file.h>

unsigned long irq_err_count;

int arch_show_interrupts(struct seq_file *p, int prec)
{
	show_ipi_list(p, prec);
	seq_printf(p, "%*s: %10lu\n", prec, "Err", irq_err_count);
	return 0;
}

void (*handle_arch_irq)(struct pt_regs *) = NULL;

void __init set_handle_irq(void (*handle_irq)(struct pt_regs *))
{
	if (handle_arch_irq)
		return;

	handle_arch_irq = handle_irq;
}

#if defined CONFIG_BCM_KF_ARM64_BCM963XX && defined CONFIG_HOTPLUG_CPU
#include <linux/cpu.h>

static void bcm63xx_rehint_one(struct irq_desc *desc, int cpu)
{
	struct irq_data *d = irq_desc_get_irq_data(desc);
	const struct cpumask *affinity = d->common->affinity;
	struct irq_chip *c;

	/*
	 * If this is a per-CPU interrupt then we have nothing to do.
	 */
	if (irqd_is_per_cpu(d))
		return;

	/*
	 * If there is no affinity_hint then we have nothing to do.
	 */
	if (!desc->affinity_hint)
		return;

	/*
	 * If affinity matches affinity_hint then we have nothing to do.
	 */
	if (cpumask_equal(affinity, desc->affinity_hint))
		return;

	affinity = desc->affinity_hint;

	// ignore irq_set_affinity failures due to affinity_hint
	// not intersecting cpu_online_mask
	c = irq_data_get_irq_chip(d);
	if (!c->irq_set_affinity)
		pr_warn("IRQ%u: unable to set affinity\n", d->irq);
	else if (c->irq_set_affinity(d, affinity, false) == IRQ_SET_MASK_OK) {
		pr_info("IRQ%d: affinity change from %32pbl to %32pbl\n",
				d->irq, d->common->affinity, affinity);
		cpumask_copy(d->common->affinity, affinity);
	}

	return;
}

/*
 * A new CPU has come online.  Migrate IRQs that have an affinity_hint
 * that doesn't match their active affinity
 */
static int bcm63xx_rehint(struct notifier_block *nb, unsigned long action, void *hcpu)
{
	int cpu = (uintptr_t) hcpu;
	struct irq_desc *desc;
	unsigned long flags;
	unsigned int i;

	if (action != CPU_ONLINE)
		return NOTIFY_OK;

	local_irq_save(flags);

	for_each_irq_desc(i, desc) {
		raw_spin_lock(&desc->lock);
		bcm63xx_rehint_one(desc, cpu);
		raw_spin_unlock(&desc->lock);
	}

	local_irq_restore(flags);

	return NOTIFY_OK;
}
#endif

void __init init_IRQ(void)
{
	irqchip_init();
	if (!handle_arch_irq)
		panic("No interrupt controller found.");
#if defined CONFIG_BCM_KF_ARM64_BCM963XX && defined CONFIG_HOTPLUG_CPU
	hotcpu_notifier(bcm63xx_rehint, 0);
#endif
}
