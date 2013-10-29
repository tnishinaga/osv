/*
 * Copyright (C) 2013 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#ifndef VMXNET3_DRIVER_H
#define VMXNET3_DRIVER_H

#include "driver.hh"
#include "drivers/driver.hh"
#include "drivers/pci-device.hh"
#include <osv/device.h>
#include <bsd/porting/bus.h>

template <typename T>
inline T *bsd_to_dev(struct device *bd)
{
    T *base = NULL;
    return reinterpret_cast<T *>((unsigned long)bd - (unsigned long)&base->_bsd_dev);
}

namespace vmware {

    typedef void (*probe_t)(device_t dev);
    typedef int  (*attach_t)(device_t dev);

    class vmxnet3 : public hw_driver {
    public:

        explicit vmxnet3(pci::device& dev);
        static hw_driver* probe(hw_device* dev);
        pci::device& pci_device() { return _dev; }

        bool parse_pci_config(void);
        void dump_config(void);

        const std::string get_name(void) { return _driver_name; }

        static vmxnet3 *from_device(struct device *dev) { return bsd_to_dev<vmxnet3>(dev); }
        struct device _bsd_dev;
    private:
        pci::device& _dev;

        sched::thread *_main_thread;
        std::string _driver_name;

        probe_t _probe = nullptr;
        attach_t _attach = nullptr;
    };
}

#endif

