/*
 * Copyright (C) 2013 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#ifndef VMWARE3_DRIVER_H
#define VMWARE3_DRIVER_H

#include <bsd/porting/netport.h>
#include <bsd/sys/net/if_var.h>
#include <bsd/sys/net/if.h>
#define _KERNEL
#include <bsd/sys/sys/mbuf.h>

#include "drivers/vmware.hh"
#include "drivers/pci-device.hh"

namespace vmware {

    class vmxnet3 : public vmware_driver {
    public:

        enum {
            VMXNET3_DEVICE_ID=0x07B0,

            //BAR1 registers
            VMXNET3_BAR1_VRRS=0x000,    // Revision
            VMXNET3_BAR1_UVRS=0x008     // UPT version
        };

        explicit vmxnet3(pci::device& dev);
        virtual ~vmxnet3() {};

        virtual const std::string get_name(void) { return std::string("vmxnet3"); }

        static hw_driver* probe(hw_device* dev);

    private:
        void parse_pci_config(void);
        void do_version_handshake(void);

        //maintains the vmxnet3 instance number for multiple adapters
        static int _instance;
        int _id;

        pci::bar *_bar1 = nullptr;
        pci::bar *_bar2 = nullptr;
    };
}

#endif

