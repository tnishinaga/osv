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
#include "drivers/vmxnet3-queues.hh"
#include "drivers/pci-device.hh"
#include "mempool.hh"

namespace vmware {

    class vmxnet3_txqueue : public vmxnet3_txq_shared {
    public:
        vmxnet3_txqueue();
    private:

        enum {
            //Queue descriptors alignment
            VMXNET3_DESCR_ALIGN = 512
        };

        void attach_descriptors(void);

        memory::phys_contiguious_memory _tx_descr_mem;
        memory::phys_contiguious_memory _tx_compdescr_mem;

        vmxnet3_tx_descr        _tx_desc[VMXNET3_MAX_TX_NDESC];
        vmxnet3_tx_compdesc     _tx_comp_desc[VMXNET3_MAX_TX_NCOMPDESC];
    };

    class vmxnet3_rxqueue : public vmxnet3_rxq_shared {
    private:
    };

    class vmxnet3 : public vmware_driver {
    public:

        explicit vmxnet3(pci::device& dev);
        virtual ~vmxnet3() {};

        virtual const std::string get_name(void) { return std::string("vmxnet3"); }

        static hw_driver* probe(hw_device* dev);

    private:

        enum {
            VMXNET3_DEVICE_ID=0x07B0,

            //Queues number
            //TODO: Make configurable?
            VMXNET3_TX_QUEUES = 1,
            VMXNET3_RX_QUEUES = 1,

            //BAR1 registers
            VMXNET3_BAR1_VRRS=0x000,    // Revision
            VMXNET3_BAR1_UVRS=0x008,    // UPT version

            //Shared memory alignment
            VMXNET3_DRIVER_SHARED_ALIGN = 1,
            VMXNET3_QUEUES_SHARED_ALIGN = 128,
        };

        void parse_pci_config(void);
        void do_version_handshake(void);
        void attach_queues_shared(void);

        //maintains the vmxnet3 instance number for multiple adapters
        static int _instance;
        int _id;

        //Shared memory
        pci::bar *_bar1 = nullptr;
        pci::bar *_bar2 = nullptr;

        memory::phys_contiguious_memory _drv_shared_mem;
        vmxnet3_drv_shared _drv_shared;

        memory::phys_contiguious_memory _queues_shared_mem;

        vmxnet3_txqueue _txq[VMXNET3_MAX_TX_QUEUES];
        vmxnet3_rxqueue _rxq[VMXNET3_RX_QUEUES];
    };
}

#endif

