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

    template<class DescT, int NDesc>
        class vmxnet3_ring {
        public:

            vmxnet3_ring()
                : _desc_mem(DescT::size() * NDesc, VMXNET3_DESC_ALIGN)
            {
                void *va = _desc_mem.get_va();
                slice_memory(va, _desc);
            }

            mmu::phys get_desc_pa() const { return _desc_mem.get_pa(); }
            static u32 get_desc_num() { return NDesc; }

        private:

            enum {
                //Queue descriptors alignment
                VMXNET3_DESC_ALIGN = 512
            };

            memory::phys_contiguious_memory _desc_mem;
            DescT      _desc[NDesc];
        };

    class vmxnet3_txqueue : public vmxnet3_txq_shared {
    public:

        vmxnet3_txqueue();

    private:

        typedef vmxnet3_ring<vmxnet3_tx_desc, VMXNET3_MAX_TX_NDESC> _cmdRingT;
        typedef vmxnet3_ring<vmxnet3_tx_compdesc, VMXNET3_MAX_TX_NCOMPDESC> _compRingT;

        _cmdRingT _cmd_ring;
        _compRingT _comp_ring;
    };

    class vmxnet3_rxqueue : public vmxnet3_rxq_shared {
    public:

        vmxnet3_rxqueue();

    private:

        typedef vmxnet3_ring<vmxnet3_rx_desc, VMXNET3_MAX_RX_NDESC> _cmdRingT;
        typedef vmxnet3_ring<vmxnet3_rx_compdesc, VMXNET3_MAX_RX_NCOMPDESC> _compRingT;

        _cmdRingT _cmd_rings[VMXNET3_RXRINGS_PERQ];
        _compRingT _comp_ring;
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
            VMXNET3_TX_QUEUES = 1,
            VMXNET3_RX_QUEUES = 1,

            //BAR1 registers
            VMXNET3_BAR1_VRRS = 0x000,    // Revision
            VMXNET3_BAR1_UVRS = 0x008,    // UPT version

            VMXNET3_MULTICAST_MAX = 32,
            VMXNET3_MAX_RX_SEGS = 17,

            //Shared memory alignment
            VMXNET3_DRIVER_SHARED_ALIGN = 1,
            VMXNET3_QUEUES_SHARED_ALIGN = 128,
            VMXNET3_MULTICAST_ALIGN = 32,

            //Generic definitions
            VMXNET3_ETH_ALEN = 6
        };

        void parse_pci_config(void);
        void do_version_handshake(void);
        void attach_queues_shared(void);
        void fill_driver_shared(void);

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

        memory::phys_contiguious_memory _mcast_list;
    };
}

#endif

