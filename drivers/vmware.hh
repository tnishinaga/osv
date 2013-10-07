/*
 * Copyright (C) 2013 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#ifndef VMWARE_DRIVER_H
#define VMWARE_DRIVER_H

#include "driver.hh"
#include "drivers/pci.hh"
#include "drivers/driver.hh"
#include "drivers/pci-function.hh"
#include "drivers/pci-device.hh"
#include "interrupt.hh"

#define vmware_tag "vmware"
#define vmware_d(...)   tprintf_d(vmware_tag, __VA_ARGS__)
#define vmware_i(...)   tprintf_i(vmware_tag, __VA_ARGS__)
#define vmware_w(...)   tprintf_w(vmware_tag, __VA_ARGS__)
#define vmware_e(...)   tprintf_e(vmware_tag, __VA_ARGS__)

namespace vmware {

class vmware_driver : public hw_driver {
public:
    explicit vmware_driver(pci::device& dev);
    virtual ~vmware_driver() {}

    enum {
        VMWARE_VENDOR_ID = 0x15AD,
    };

    virtual void dump_config(void);

    pci::device& pci_device() { return _dev; }
protected:
    pci::device& _dev;
private:
    void parse_pci_config(void);
};

}

#endif

