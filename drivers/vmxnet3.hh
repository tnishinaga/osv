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
#include <bsd/sys/sys/mbuf.h>

#include "drivers/vmware.hh"
#include "drivers/vmxnet3-queues.hh"
#include "drivers/pci-device.hh"
#include <osv/mempool.hh>

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

    class vmxnet3_isr_thread {
    public:
        vmxnet3_isr_thread()
            : isr_thread([this] { this->isr(); }) {};


        sched::thread *get_isr_thread(void) { return &isr_thread; };
        void start_isr_thread(void) { isr_thread.start(); }
    private:
        virtual void isr(void) = 0;
        sched::thread isr_thread;
        unsigned _intr_idx = 0;
    };

    class vmxnet3_txqueue : public vmxnet3_txq_shared
                          , public vmxnet3_isr_thread {
    public:
        void start();
        void set_intr_idx(unsigned idx) { _layout->intr_idx = static_cast<u8>(idx); }

    private:
        virtual void isr(void) {};

        typedef vmxnet3_ring<vmxnet3_tx_desc, VMXNET3_MAX_TX_NDESC> _cmdRingT;
        typedef vmxnet3_ring<vmxnet3_tx_compdesc, VMXNET3_MAX_TX_NCOMPDESC> _compRingT;

        _cmdRingT _cmd_ring;
        _compRingT _comp_ring;
    };

    class vmxnet3_rxqueue : public vmxnet3_rxq_shared
                          , public vmxnet3_isr_thread {
    public:
        void start();
        void set_intr_idx(unsigned idx) { _layout->intr_idx = static_cast<u8>(idx); }

    private:
        virtual void isr(void) {};

        typedef vmxnet3_ring<vmxnet3_rx_desc, VMXNET3_MAX_RX_NDESC> _cmdRingT;
        typedef vmxnet3_ring<vmxnet3_rx_compdesc, VMXNET3_MAX_RX_NCOMPDESC> _compRingT;

        _cmdRingT _cmd_rings[VMXNET3_RXRINGS_PERQ];
        _compRingT _comp_ring;
    };

    class vmxnet3_intr_mgr {
    public:
        struct binding {
            //Thread to wake
            sched::thread *thread;
            //Callback to notify about interrupt index assigned for this thread
            std::function<void (unsigned idx)> assigned_idx;
        };

        vmxnet3_intr_mgr(pci::function *dev,
                         std::function<void (unsigned idx)> disable_int)
            : _msi(dev)
            , _disable_int(disable_int)
            {}

        ~vmxnet3_intr_mgr() { easy_unregister(); }

        void easy_register(u32 intr_cfg,
                           const std::vector<binding>& bindings);
        void easy_register_msix(const std::vector<binding>& bindings);
        void easy_unregister(void);

        bool is_automask() const { return _is_auto_mask; }
        unsigned interrupts_number() const { return _num_interrupts; }
    private:
        enum {
            // Interrupt types
            VMXNET3_IT_AUTO = 0x00,
            VMXNET3_IT_LEGACY = 0x01,
            VMXNET3_IT_MSI = 0x02,
            VMXNET3_IT_MSIX = 0x03,

            VMXNET3_IT_TYPE_MASK  = 0x03,
            VMXNET3_IT_MODE_MASK  = 0x03,
            VMXNET3_IT_MODE_SHIFT = 0x02,

            // Interrupt mask mode
            VMXNET3_IMM_AUTO        = 0x00,
            VMXNET3_IMM_ACTIVE      = 0x01,
            VMXNET3_IMM_LAZY        = 0x02
        };

        interrupt_manager _msi;

        bool _is_auto_mask = false;
        bool _is_active_mask = false;
        bool _msix_registered = false;
        unsigned _num_interrupts = 0;
        std::function<void (unsigned idx)> _disable_int;
    };

    class vmxnet3 : public vmware_driver
                  , protected vmxnet3_isr_thread {
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
            VMXNET3_BAR1_CMD  = 0x020,    // Command

            //VMXNET3 commands
            VMXNET3_CMD_GET_INTRCFG = 0xF00D0008,  // Get interrupt config

            //Shared memory alignment
            VMXNET3_DRIVER_SHARED_ALIGN = 1,
            VMXNET3_QUEUES_SHARED_ALIGN = 128,
            VMXNET3_MULTICAST_ALIGN = 32,

            //Generic definitions
            VMXNET3_ETH_ALEN = 6,

            //Internal device parameters
            VMXNET3_MULTICAST_MAX = 32,
            VMXNET3_MAX_RX_SEGS = 17
        };

        void parse_pci_config(void);
        void do_version_handshake(void);
        void attach_queues_shared(void);
        void fill_driver_shared(void);
        void allocate_interrupts(void);

        template <class T>
        static void fill_intr_requirement(T *q, std::vector<vmxnet3_intr_mgr::binding> &ints);
        template <class T>
        static void fill_intr_requirements(T &q, std::vector<vmxnet3_intr_mgr::binding> &ints);

        void set_intr_idx(unsigned idx) { _drv_shared.set_evt_intr_idx(static_cast<u8>(idx)); }
        void disable_interrupt(unsigned idx) {/*TODO: implement me*/}
        virtual void isr(void) {};

        void write_cmd(u32 cmd);
        u32 read_cmd(u32 cmd);
        //maintains the vmxnet3 instance number for multiple adapters
        static int _instance;
        int _id;
        struct ifnet* _ifn;

        //Shared memory
        pci::bar *_bar0 = nullptr;
        pci::bar *_bar1 = nullptr;

        memory::phys_contiguious_memory _drv_shared_mem;
        vmxnet3_drv_shared _drv_shared;

        memory::phys_contiguious_memory _queues_shared_mem;

        vmxnet3_txqueue _txq[VMXNET3_MAX_TX_QUEUES];
        vmxnet3_rxqueue _rxq[VMXNET3_RX_QUEUES];

        memory::phys_contiguious_memory _mcast_list;

        //Interrupt manager
        vmxnet3_intr_mgr _int_mgr;
    };
}

#endif

