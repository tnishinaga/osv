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

    enum {
        //General HW configuration
        VMXNET3_REVISION = 1,
        VMXNET3_UPT_VERSION = 1,
        VMXNET3_VERSIONS_MASK = 1,

        VMXNET3_REV1_MAGIC = 0XBABEFEE1,

        VMXNET3_GOS_FREEBSD = 0x10,
        VMXNET3_GOS_32BIT = 0x01,
        VMXNET3_GOS_64BIT = 0x02,

        //Mimic FreeBSD driver behavior
        VMXNET3_DRIVER_VERSION = 0x00010000,
        //TODO: Should we be more specific?
        VMXNET3_GUEST_OS_VERSION = 0x01,

        VMXNET3_MAX_TX_QUEUES = 8,
        VMXNET3_MAX_RX_QUEUES = 16,
        VMXNET3_MAX_INTRS = VMXNET3_MAX_TX_QUEUES + VMXNET3_MAX_RX_QUEUES + 1
    };

    class vmxnet3_drv_shared final {
    public:
        vmxnet3_drv_shared();
        ~vmxnet3_drv_shared();
    private:

        void init_layout(void);

        typedef struct {
            u32 magic;
            u32 pad1;

            /* Misc. control */
            u32 version;		/* Driver version */
            u32 guest;			/* Guest OS */
            u32 vmxnet3_revision;	/* Supported VMXNET3 revision */
            u32 upt_version;		/* Supported UPT version */
            u64 upt_features;
            u64 driver_data;
            u64 queue_shared;
            u32 driver_data_len;
            u32 queue_shared_len;
            u32 mtu;
            u16 nrxsg_max;
            u8  ntxqueue;
            u8  nrxqueue;
            u32 reserved1[4];

            /* Interrupt control */
            u8  automask;
            u8  nintr;
            u8  evintr;
            u8  modlevel[VMXNET3_MAX_INTRS];
            u32 ictrl;
            u32 reserved2[2];

            /* Receive filter parameters */
            u32 rxmode;
            u16 mcast_tablelen;
            u16 pad2;
            u64 mcast_table;
            u32 vlan_filter[4096 / 32];

            struct {
                u32 version;
                u32 len;
                u64 paddr;
            }   rss, pm, plugin;

            u32 event;
            u32 reserved3[5];
        } __packed vmxnet3_shared_layout;

        vmxnet3_shared_layout *_layout;
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

            //BAR1 registers
            VMXNET3_BAR1_VRRS=0x000,    // Revision
            VMXNET3_BAR1_UVRS=0x008,    // UPT version
        };

        void parse_pci_config(void);
        void do_version_handshake(void);

        //maintains the vmxnet3 instance number for multiple adapters
        static int _instance;
        int _id;

        pci::bar *_bar1 = nullptr;
        pci::bar *_bar2 = nullptr;
        vmxnet3_drv_shared _drvshared;
    };
}

#endif

