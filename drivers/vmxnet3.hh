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

            unsigned head, next, fill;
            int gen;
            mmu::phys get_desc_pa() const { return _desc_mem.get_pa(); }
            static u32 get_desc_num() { return NDesc; }
            DescT& get_desc(int i) { return _desc[i]; }
            void clear_desc(int i) { _desc[i].clear(); }
            void clear_descs() { for (int i = 0; i < NDesc; i++) clear_desc(i); }
            void increment_fill();
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


        sched::thread *get_isr_thread() { return &isr_thread; };
        void start_isr_thread() { isr_thread.start(); }
    private:
        virtual void isr() = 0;
        sched::thread isr_thread;
        unsigned _intr_idx = 0;
    };

    class vmxnet3_txqueue : public vmxnet3_txq_shared
                          , public vmxnet3_isr_thread {
    public:
        void init(std::function<void (void)> isr_handler);
        void set_intr_idx(unsigned idx) { layout->intr_idx = static_cast<u8>(idx); }
        int enqueue(struct mbuf *m);
        void attach(void* storage);
        typedef vmxnet3_ring<vmxnet3_tx_desc, VMXNET3_MAX_TX_NDESC> cmdRingT;
        typedef vmxnet3_ring<vmxnet3_tx_compdesc, VMXNET3_MAX_TX_NCOMPDESC> compRingT;
        cmdRingT cmd_ring;

        compRingT comp_ring;
        struct mbuf *buf[VMXNET3_MAX_TX_NDESC];

    private:
        std::function<void (void)> _isr_handler;
        virtual void isr() { printf("%s\n", __PRETTY_FUNCTION__); _isr_handler(); };
    };

    class vmxnet3_rxqueue : public vmxnet3_rxq_shared
                          , public vmxnet3_isr_thread {
    public:
        void init(std::function<void (void)> isr_handler);
        void set_intr_idx(unsigned idx) { layout->intr_idx = static_cast<u8>(idx); }
        void discard(int rid, int idx);
        void discard_chain(int rid);
        int newbuf(int rid);
        void attach(void* storage);

        typedef vmxnet3_ring<vmxnet3_rx_desc, VMXNET3_MAX_RX_NDESC> cmdRingT;
        typedef vmxnet3_ring<vmxnet3_rx_compdesc, VMXNET3_MAX_RX_NCOMPDESC> compRingT;

        cmdRingT cmd_rings[VMXNET3_RXRINGS_PERQ];
        compRingT comp_ring;
        struct mbuf *buf[VMXNET3_RXRINGS_PERQ][VMXNET3_MAX_RX_NDESC];

    private:
        std::function<void (void)> _isr_handler;
        virtual void isr() { printf("%s\n", __PRETTY_FUNCTION__); _isr_handler(); };
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
        void easy_unregister();

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
        enum {
            VMXNET3_INIT_GEN = 1,

            // Buffer types
            VMXNET3_BTYPE_HEAD = 0, // Head only
            VMXNET3_BTYPE_BODY = 1 // Body only
        };
        explicit vmxnet3(pci::device& dev);
        virtual ~vmxnet3() {};

        virtual const std::string get_name() { return std::string("vmxnet3"); }

        void transmit(struct mbuf* m_head);
        void receive_work();
        void gc_work();

        mutex _lock;

        static hw_driver* probe(hw_device* dev);
    private:
        enum {
            VMXNET3_DEVICE_ID=0x07B0,

            //Queues number
            VMXNET3_TX_QUEUES = 1,
            VMXNET3_RX_QUEUES = 1,

            //BAR0 registers
            VMXNET3_BAR0_TXH = 0x600, // Queue 0 of Tx head
            VMXNET3_BAR0_RXH1 = 0x800, // Queue 0 of Ring1 Rx head
            VMXNET3_BAR0_RXH2 = 0xA00, // Queue 0 of Ring2 Rx head

            //BAR1 registers
            VMXNET3_BAR1_VRRS = 0x000,    // Revision
            VMXNET3_BAR1_UVRS = 0x008,    // UPT version
            VMXNET3_BAR1_DSL  = 0x010,    // Driver shared address low
            VMXNET3_BAR1_DSH  = 0x018,    // Driver shared address high
            VMXNET3_BAR1_CMD  = 0x020,    // Command

            //VMXNET3 commands
            VMXNET3_CMD_ENABLE = 0xCAFE0000, // Enable VMXNET3
            VMXNET3_CMD_DISABLE = 0xCAFE0001, // Disable VMXNET3
            VMXNET3_CMD_RESET = 0xCAFE0002, // Reset device
            VMXNET3_CMD_SET_RXMODE = 0xCAFE0003, // Set interface flags
            VMXNET3_CMD_SET_FILTER = 0xCAFE0004, // Set address filter
            VMXNET3_CMD_VLAN_FILTER = 0xCAFE0005, // Set VLAN filter
            VMXNET3_CMD_GET_STATUS = 0xF00D0000,  // Get queue errors
            VMXNET3_CMD_GET_STATS = 0xF00D0001, // Get queue statistics
            VMXNET3_CMD_GET_LINK = 0xF00D0002, // Get link status
            VMXNET3_CMD_GET_MACL = 0xF00D0003, // Get MAC address low
            VMXNET3_CMD_GET_MACH = 0xF00D0004, // Get MAC address high
            VMXNET3_CMD_GET_INTRCFG = 0xF00D0008,  // Get interrupt config

            //Shared memory alignment
            VMXNET3_DRIVER_SHARED_ALIGN = 1,
            VMXNET3_QUEUES_SHARED_ALIGN = 128,
            VMXNET3_MULTICAST_ALIGN = 32,

            //Generic definitions
            VMXNET3_ETH_ALEN = 6,

            //Internal device parameters
            VMXNET3_MULTICAST_MAX = 32,
            VMXNET3_MAX_RX_SEGS = 17,
            VMXNET3_NUM_INTRS = 3,

            //Offloading modes
            VMXNET3_OM_NONE = 0,
            VMXNET3_OM_CSUM = 2,
            VMXNET3_OM_TSO = 3,

            //RX modes
            VMXNET3_RXMODE_UCAST = 0x01,
            VMXNET3_RXMODE_MCAST = 0x02,
            VMXNET3_RXMODE_BCAST = 0x04,
            VMXNET3_RXMODE_ALLMULTI = 0x08,
            VMXNET3_RXMODE_PROMISC = 0x10
        };
        static inline constexpr u32 VMXNET3_BAR0_IMASK(int irq)
        {
            return 0x000 + irq * 8;
        }

        void parse_pci_config();
        void stop();
        void enable_device();
        void do_version_handshake();
        void attach_queues_shared();
        void fill_driver_shared();
        void allocate_interrupts();

        template <class T>
        static void fill_intr_requirement(T *q, std::vector<vmxnet3_intr_mgr::binding> &ints);
        template <class T>
        static void fill_intr_requirements(T &q, std::vector<vmxnet3_intr_mgr::binding> &ints);

        void set_intr_idx(unsigned idx) { _drv_shared.set_evt_intr_idx(static_cast<u8>(idx)); }
        virtual void isr() {};

        void write_cmd(u32 cmd);
        u32 read_cmd(u32 cmd);
        void get_mac_address(u_int8_t *macaddr);
        void txq_encap(vmxnet3_txqueue &txq, struct mbuf *m_head);
        void txq_gc(vmxnet3_txqueue &txq);
        bool txq_gc_avail(vmxnet3_txqueue &txq);
        void rxq_eof(vmxnet3_rxqueue &rxq);
        bool rxq_avail(vmxnet3_rxqueue &rxq);
        void enable_interrupts();
        void disable_interrupts();

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

        vmxnet3_txqueue _txq[VMXNET3_TX_QUEUES];
        vmxnet3_rxqueue _rxq[VMXNET3_RX_QUEUES];

        memory::phys_contiguious_memory _mcast_list;

        //Interrupt manager
        vmxnet3_intr_mgr _int_mgr;
    };
}

#endif

