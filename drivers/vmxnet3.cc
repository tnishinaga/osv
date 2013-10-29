/*
 * Copyright (C) 2013 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#include "clock.hh"
#include "msr.hh"
#include <osv/types.h>
#include "mmu.hh"
#include "string.h"
#include "cpuid.hh"
#include "barrier.hh"
#include "debug.hh"
#include "processor.hh"
#include "vmxnet3.hh"
#include "exceptions.hh"
#include <osv/device.h>
#include <bsd/porting/bus.h>

#define VMXNET3_VMWARE_VENDOR_ID        0x15AD
#define VMXNET3_VMWARE_DEVICE_ID        0x07B0

extern driver_t vmxnet3_driver;

namespace vmware {

static std::atomic<int> net_unit;

vmxnet3::vmxnet3(pci::device& pci_dev)
    : hw_driver()
    , _dev(pci_dev)
{
    static int initialized;
    int irqno = pci_dev.get_interrupt_line();

    if (!irqno)
        return;

    if (initialized++)
        return;

    parse_pci_config();

    _dev.set_bus_master(true);
    _driver_name = std::string("vmware-vmxnet3");

    _bsd_dev.unit = net_unit++;
    device_method_t *dm = vmxnet3_driver.methods;
    for (auto i = 0; dm[i].id; i++) {
        if (dm[i].id == bus_device_probe)
            _probe = reinterpret_cast<probe_t>(dm[i].func);
        if (dm[i].id == bus_device_attach)
            _attach = reinterpret_cast<attach_t>(dm[i].func);
    }
    _bsd_dev.softc = malloc(vmxnet3_driver.size);
    // Simpler and we don't expect driver loading to fail anyway
    assert(_bsd_dev.softc);
    memset(_bsd_dev.softc, 0, vmxnet3_driver.size);
}

hw_driver* vmxnet3::probe(hw_device* dev)
{
    if (auto pci_dev = dynamic_cast<pci::device*>(dev)) {
        // dev id is the same for all xen devices?
        if (pci_dev->get_subsystem_vid() == VMXNET3_VMWARE_VENDOR_ID ||
	    pci_dev->get_subsystem_id() == VMXNET3_VMWARE_DEVICE_ID) {
            return new vmxnet3(*pci_dev);
        }
    }
    return nullptr;
}

void vmxnet3::dump_config()
{
    _dev.dump_config();
}

bool vmxnet3::parse_pci_config(void)
{
    return _dev.parse_pci_config();
}
};

