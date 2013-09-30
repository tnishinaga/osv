/*
 * Copyright (C) 2013 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */


#define _KERNEL

#include <sys/cdefs.h>

#include "drivers/vmware.hh"
#include "drivers/vmxnet3.hh"
#include "drivers/pci-device.hh"
#include "interrupt.hh"

#include <sstream>
#include <string>
#include <string.h>
#include <map>
#include <errno.h>
#include <osv/debug.h>

#include "sched.hh"
#include "osv/trace.hh"

#include "drivers/clock.hh"
#include "drivers/clockevent.hh"

#include <osv/device.h>
#include <osv/ioctl.h>
#include <bsd/sys/net/ethernet.h>
#include <bsd/sys/net/if_types.h>
#include <bsd/sys/sys/param.h>

#include <bsd/sys/net/ethernet.h>
#include <bsd/sys/net/if_vlan_var.h>
#include <bsd/sys/netinet/in.h>
#include <bsd/sys/netinet/ip.h>
#include <bsd/sys/netinet/udp.h>
#include <bsd/sys/netinet/tcp.h>
#include <bsd/machine/atomic.h>

using namespace memory;

namespace vmware {

    int vmxnet3::_instance = 0;

    #define vmxnet3_tag "vmxnet3"
    #define vmxnet3_d(...)   tprintf_d(vmxnet3_tag, __VA_ARGS__)
    #define vmxnet3_i(...)   tprintf_i(vmxnet3_tag, __VA_ARGS__)
    #define vmxnet3_w(...)   tprintf_w(vmxnet3_tag, __VA_ARGS__)
    #define vmxnet3_e(...)   tprintf_e(vmxnet3_tag, __VA_ARGS__)

    template<class T> void slice_memory(void *&va, T &holder)
    {
        for (auto &e : holder) {
            e.attach(va);
            va += e.size();
        }
    }

    vmxnet3_txqueue::vmxnet3_txqueue()
    {
        _layout->cmd_ring = _cmd_ring.get_desc_pa();
        _layout->cmd_ring_len = _cmd_ring.get_desc_num();
        _layout->comp_ring = _comp_ring.get_desc_pa();
        _layout->comp_ring_len = _comp_ring.get_desc_num();

        _layout->driver_data = mmu::virt_to_phys(this);
        _layout->driver_data_len = sizeof(*this);
    }

    vmxnet3_rxqueue::vmxnet3_rxqueue()
    {
        for (unsigned i = 0; i < VMXNET3_RXRINGS_PERQ; i++) {
            _layout->cmd_ring[i] = _cmd_rings[i].get_desc_pa();
            _layout->cmd_ring_len[i] = _cmd_rings[i].get_desc_num();
        }

        _layout->comp_ring = _comp_ring.get_desc_pa();
        _layout->comp_ring_len = _comp_ring.get_desc_num();

        _layout->driver_data = mmu::virt_to_phys(this);
        _layout->driver_data_len = sizeof(*this);
    }

    vmxnet3::vmxnet3(pci::device& dev)
        : vmware_driver(dev)
        , _drv_shared_mem(vmxnet3_drv_shared::size(),
                          VMXNET3_DRIVER_SHARED_ALIGN)
        , _queues_shared_mem(vmxnet3_txq_shared::size() * VMXNET3_TX_QUEUES +
                             vmxnet3_rxq_shared::size() * VMXNET3_RX_QUEUES,
                             VMXNET3_QUEUES_SHARED_ALIGN)
        , _mcast_list(VMXNET3_MULTICAST_MAX * VMXNET3_ETH_ALEN, VMXNET3_MULTICAST_ALIGN)
    {
        vmxnet3_i("VMXNET3 INSTANCE");
        _id = _instance++;
        _drv_shared.attach(_drv_shared_mem.get_va());
        attach_queues_shared();

        parse_pci_config();
        do_version_handshake();
        fill_driver_shared();
    }

    void vmxnet3::attach_queues_shared(void)
    {
        auto *va = _queues_shared_mem.get_va();

        slice_memory(va, _txq);
        slice_memory(va, _rxq);
    }

    void vmxnet3::fill_driver_shared(void)
    {
        _drv_shared.set_driver_data(mmu::virt_to_phys(this), sizeof(*this));
        _drv_shared.set_queue_shared(_queues_shared_mem.get_pa(),
                                     _queues_shared_mem.get_size());
        _drv_shared.set_max_sg_len(VMXNET3_MAX_RX_SEGS);
        _drv_shared.set_mcast_table(_mcast_list.get_pa(),
                                    _mcast_list.get_size());
    }

    hw_driver* vmxnet3::probe(hw_device* dev)
    {
        try {
            if (auto pci_dev = dynamic_cast<pci::device*>(dev)) {
                if (pci_dev->get_id() == hw_device_id(VMWARE_VENDOR_ID, VMXNET3_DEVICE_ID)) {
                    return new vmxnet3(*pci_dev);
                }
            }
        } catch (std::exception& e) {
            vmxnet3_e("Exception on device construction: %s", e.what());
        }
        return nullptr;
    }

    void vmxnet3::parse_pci_config(void)
    {
        _bar0 = _dev.get_bar(1);
        if (_bar0 == nullptr) {
            throw std::runtime_error("BAR1 is absent");
        }

        _bar1 = _dev.get_bar(2);
        if (_bar1 == nullptr) {
            throw std::runtime_error("BAR2 is absent");
        }
    }

    void vmxnet3::do_version_handshake(void)
    {
        auto val = _bar1->read(VMXNET3_BAR1_VRRS);
        if ((val & VMXNET3_VERSIONS_MASK) != VMXNET3_REVISION) {
            auto err = boost::format("unknown HW version %d") % val;
            throw std::runtime_error(err.str());
        }
        _bar1->write(VMXNET3_BAR1_VRRS, VMXNET3_REVISION);

        val = _bar1->read(VMXNET3_BAR1_UVRS);
        if ((val & VMXNET3_VERSIONS_MASK) != VMXNET3_UPT_VERSION) {
            auto err = boost::format("unknown UPT version %d") % val;
            throw std::runtime_error(err.str());
        }
        _bar1->write(VMXNET3_BAR1_UVRS, VMXNET3_UPT_VERSION);
    }

    void vmxnet3::write_cmd(u32 cmd)
    {
        _bar1->write(VMXNET3_BAR1_CMD, cmd);
    }

    u32 vmxnet3::read_cmd(u32 cmd)
    {
        write_cmd(cmd);
        mb();
        return _bar1->read(VMXNET3_BAR1_CMD);
    }
}

