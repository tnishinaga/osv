/*
 * Copyright (C) 2013 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#include <string.h>

#include "drivers/vmware.hh"
#include <osv/debug.h>
#include "osv/trace.hh"

using namespace pci;

namespace vmware {

vmware_driver::vmware_driver(pci::device& dev)
    : hw_driver()
    , _dev(dev)
{
    parse_pci_config();
    _dev.set_bus_master(true);
    _dev.msix_enable();
}

void vmware_driver::dump_config(void)
{
    u8 B, D, F;
    _dev.get_bdf(B, D, F);

    _dev.dump_config();
    vmware_d("%s [%x:%x.%x] vid:id= %x:%x", get_name().c_str(),
        (u16)B, (u16)D, (u16)F,
        _dev.get_vendor_id(),
        _dev.get_device_id());
}

void vmware_driver::parse_pci_config(void)
{
    if (!_dev.parse_pci_config()) {
        throw std::runtime_error("_dev cannot parse PCI config");
    }
}

}

