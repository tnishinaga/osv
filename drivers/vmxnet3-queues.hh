/*
 * Copyright (C) 2013 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#ifndef VMXNET3_QUEUES_H
#define VMXNET3_QUEUES_H

#include <bsd/porting/netport.h>

#include <atomic>
#include <functional>
#include <osv/mutex.h>
#include "debug.hh"

namespace vmware {

    template <class T> class vmxnet3_layout_holder {
    public:
        vmxnet3_layout_holder(void* storage)
            : _layout(static_cast<T *>(storage)) {
                memset(_layout, 0, sizeof(*_layout));
        }

        static size_t size() { return sizeof(*_layout); }
    protected:
        T *_layout;
    };

    typedef struct {
        u64    addr;

        u32    len:14;
        u32    gen:1;           // Generation
        u32    pad1:1;
        u32    dtype:1;         //Descriptor type
        u32    pad2:1;
        u32    offload_pos:14;  //Offloading position

        u32    hlen:10;         //Header len
        u32    offload_mode:2;  //Offloading mode
        u32    eop:1;           //End of packet
        u32    compreq:1;       //Completion request
        u32    pad3:1;
        u32    vtag_mode:1;     //VLAN tag insertion mode
        u32    vtag:16;         //VLAN tag
    } __packed vmxnet3_tx_descr_layout;


    class vmxnet3_tx_descr
        : public vmxnet3_layout_holder<vmxnet3_tx_descr_layout> {
    public:
        vmxnet3_tx_descr(void* storage)
            : vmxnet3_layout_holder(storage) {}
    };

    typedef struct {
        u32    eop_idx:12;      // EOP index in Tx ring
        u32    pad1:20;

        u32    pad2:32;
        u32    pad3:32;

        u32    rsvd:24;
        u32    type:7;
        u32    gen:1;
    } __packed vmxnet3_tx_compdesc_layout;

    class vmxnet3_tx_compdesc
        : public vmxnet3_layout_holder<vmxnet3_tx_compdesc_layout> {
    public:
        vmxnet3_tx_compdesc(void* storage)
            : vmxnet3_layout_holder(storage) {}
    };

    typedef struct {
        u64    addr;

        u32    len:14;
        u32    btype:1;         //Buffer type
        u32    dtype:1;         //Descriptor type
        u32    rsvd:15;
        u32    gen:1;

        u32    pad1:32;
    } __packed vmxnet3_rx_desc_layout;

    class vmxnet3_rx_desc
        : public vmxnet3_layout_holder<vmxnet3_rx_desc_layout> {
    public:
        vmxnet3_rx_desc(void* storage)
            : vmxnet3_layout_holder(storage) {}
    };

    typedef struct {
        u32     rxd_idx:12;     //Rx descriptor index
        u32     pad1:2;
        u32     eop:1;          //End of packet
        u32     sop:1;          //Start of packet
        u32     qid:10;
        u32     rss_type:4;
        u32     no_csum:1;      //No checksum calculated
        u32     pad2:1;

        u32     rss_hash:32;    //RSS hash value

        u32     len:14;
        u32     error:1;
        u32     vlan:1;         //802.1Q VLAN frame
        u32     vtag:16;        //VLAN tag

        u32     csum:16;
        u32     csum_ok:1;      //TCP/UDP checksum ok
        u32     udp:1;
        u32     tcp:1;
        u32     ipcsum_ok:1;    //IP checksum OK
        u32     ipv6:1;
        u32     ipv4:1;
        u32     fragment:1;     //IP fragment
        u32     fcs:1;          //Frame CRC correct
        u32     type:7;
        u32     gen:1;
    } __packed vmxnet3_rx_compdesc_layout;

    class vmxnet3_rx_compdesc
        : public vmxnet3_layout_holder<vmxnet3_rx_compdesc_layout> {
    public:
        vmxnet3_rx_compdesc(void* storage)
            : vmxnet3_layout_holder(storage) {}
    };

    struct UPT1_TxStats {
        u64    TSO_packets;
        u64    TSO_bytes;
        u64    ucast_packets;
        u64    ucast_bytes;
        u64    mcast_packets;
        u64    mcast_bytes;
        u64    bcast_packets;
        u64    bcast_bytes;
        u64    error;
        u64    discard;
    } __packed;

    typedef struct {
        // Control
        u32    npending;
        u32    intr_threshold;
        u64    reserved1;

        // Config
        u64    cmd_ring;
        u64    data_ring;
        u64    comp_ring;
        u64    driver_data;
        u64    reserved2;
        u32    cmd_ring_len;
        u32    data_ring_len;
        u32    comp_ring_len;
        u32    driver_data_len;
        u8     intr_idx;
        u8     pad1[7];

        // Queue status
        u8     stopped;
        u8     pad2[3];
        u32    error;

        struct UPT1_TxStats stats;

        u8      pad3[88];
    } __packed vmxnet3_txq_shared_layout;

    class vmxnet3_txq_shared
        : public vmxnet3_layout_holder<vmxnet3_txq_shared_layout> {
    public:
        vmxnet3_txq_shared(void* storage)
            : vmxnet3_layout_holder(storage) {}
    };

    struct UPT1_RxStats {
        u64   LRO_packets;
        u64   LRO_bytes;
        u64   ucast_packets;
        u64   ucast_bytes;
        u64   mcast_packets;
        u64   mcast_bytes;
        u64   bcast_packets;
        u64   bcast_bytes;
        u64   nobuffer;
        u64   error;
    } __packed;

    typedef struct {
        u8      update_rxhead;
        u8      pad1[7];
        u64     reserved1;

        u64     cmd_ring[2];
        u64     comp_ring;
        u64     driver_data;
        u64     reserved2;
        u32     cmd_ring_len[2];
        u32     comp_ring_len;
        u32     driver_data_len;
        u8      intr_idx;
        u8      pad2[7];

        u8      stopped;
        u8      pad3[3];
        u32     error;

        struct  UPT1_RxStats stats;

        u8      pad4[88];
    } __packed vmxnet3_rxq_shared_layout;

    class vmxnet3_rxq_shared
        : public vmxnet3_layout_holder<vmxnet3_rxq_shared_layout> {
    public:
        vmxnet3_rxq_shared(void* storage)
            : vmxnet3_layout_holder(storage) {}
    };

}

#endif // VIRTIO_VRING_H
