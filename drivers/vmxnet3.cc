/*
 * Copyright (C) 2013 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */


#include <sys/cdefs.h>

#include "drivers/vmware.hh"
#include "drivers/vmxnet3.hh"
#include "drivers/pci-device.hh"
#include <osv/interrupt.hh>

#include <sstream>
#include <string>
#include <string.h>
#include <map>
#include <errno.h>
#include <osv/debug.h>

#include <osv/sched.hh>
#include <osv/trace.hh>

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
#include <typeinfo>
#include <cxxabi.h>

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

void vmxnet3_txqueue::start()
{
    _layout->cmd_ring = _cmd_ring.get_desc_pa();
    _layout->cmd_ring_len = _cmd_ring.get_desc_num();
    _layout->comp_ring = _comp_ring.get_desc_pa();
    _layout->comp_ring_len = _comp_ring.get_desc_num();

    _layout->driver_data = mmu::virt_to_phys(this);
    _layout->driver_data_len = sizeof(*this);

    start_isr_thread();
}

void vmxnet3_rxqueue::start()
{
    for (unsigned i = 0; i < VMXNET3_RXRINGS_PERQ; i++) {
        _layout->cmd_ring[i] = _cmd_rings[i].get_desc_pa();
        _layout->cmd_ring_len[i] = _cmd_rings[i].get_desc_num();
    }

    _layout->comp_ring = _comp_ring.get_desc_pa();
    _layout->comp_ring_len = _comp_ring.get_desc_num();

    _layout->driver_data = mmu::virt_to_phys(this);
    _layout->driver_data_len = sizeof(*this);

    start_isr_thread();
}

vmxnet3::vmxnet3(pci::device &dev)
    : vmware_driver(dev)
    , _drv_shared_mem(vmxnet3_drv_shared::size(),
                        VMXNET3_DRIVER_SHARED_ALIGN)
    , _queues_shared_mem(vmxnet3_txq_shared::size() * VMXNET3_TX_QUEUES +
                            vmxnet3_rxq_shared::size() * VMXNET3_RX_QUEUES,
                            VMXNET3_QUEUES_SHARED_ALIGN)
    , _mcast_list(VMXNET3_MULTICAST_MAX * VMXNET3_ETH_ALEN, VMXNET3_MULTICAST_ALIGN)
    , _int_mgr(&dev, [this] (unsigned idx) { this->disable_interrupt(idx); })
{
    vmxnet3_i("VMXNET3 INSTANCE");
    _id = _instance++;
    _drv_shared.attach(_drv_shared_mem.get_va());

    parse_pci_config();
    do_version_handshake();
    allocate_interrupts();
    fill_driver_shared();

    start_isr_thread();
}

template <class T>
void vmxnet3::fill_intr_requirement(T *entry, std::vector<vmxnet3_intr_mgr::binding> &ints)
{
    vmxnet3_intr_mgr::binding b;

    b.thread = entry->get_isr_thread();
    b.assigned_idx = [entry] (unsigned idx) { entry->set_intr_idx(idx); };
    ints.push_back(b);
}

template <class T>
void vmxnet3::fill_intr_requirements(T &entries, std::vector<vmxnet3_intr_mgr::binding> &ints)
{
    for (auto &entry : entries) {
        fill_intr_requirement(&entry, ints);
    }
}

void vmxnet3::allocate_interrupts(void)
{
    std::vector<vmxnet3_intr_mgr::binding> ints;

    fill_intr_requirements(_txq, ints);
    fill_intr_requirements(_rxq, ints);
    fill_intr_requirement(this, ints);

    auto intr_cfg = read_cmd(VMXNET3_CMD_GET_INTRCFG);
    _int_mgr.easy_register(intr_cfg, ints);
}
 
void vmxnet3::attach_queues_shared(void)
{
    auto *va = _queues_shared_mem.get_va();

    slice_memory(va, _txq);
    slice_memory(va, _rxq);

    for (auto &q : _txq) {
        q.start();
    }
    for (auto &q : _rxq) {
        q.start();
    }
}

void vmxnet3::fill_driver_shared(void)
{
    _drv_shared.set_driver_data(mmu::virt_to_phys(this), sizeof(*this));
    _drv_shared.set_queue_shared(_queues_shared_mem.get_pa(),
                                 _queues_shared_mem.get_size());
    _drv_shared.set_max_sg_len(VMXNET3_MAX_RX_SEGS);
    _drv_shared.set_mcast_table(_mcast_list.get_pa(),
                                _mcast_list.get_size());
    _drv_shared.set_intr_config(static_cast<u8>(_int_mgr.interrupts_number()),
                                static_cast<u8>(_int_mgr.is_automask()));
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
    auto val = _bar1->readl(VMXNET3_BAR1_VRRS);
    if ((val & VMXNET3_VERSIONS_MASK) != VMXNET3_REVISION) {
        auto err = boost::format("unknown HW version %d") % val;
        throw std::runtime_error(err.str());
    }
    _bar1->writel(VMXNET3_BAR1_VRRS, VMXNET3_REVISION);

    val = _bar1->readl(VMXNET3_BAR1_UVRS);
    if ((val & VMXNET3_VERSIONS_MASK) != VMXNET3_UPT_VERSION) {
        auto err = boost::format("unknown UPT version %d") % val;
        throw std::runtime_error(err.str());
    }
    _bar1->writel(VMXNET3_BAR1_UVRS, VMXNET3_UPT_VERSION);
}

void vmxnet3::write_cmd(u32 cmd)
{
    _bar1->writel(VMXNET3_BAR1_CMD, cmd);
}

u32 vmxnet3::read_cmd(u32 cmd)
{
    write_cmd(cmd);
    mb();
    return _bar1->readl(VMXNET3_BAR1_CMD);
}

void vmxnet3_intr_mgr::easy_register_msix(const std::vector<binding> &bindings)
{
    std::vector<msix_binding> msix_vec;
    unsigned idx = 0;

    for (auto b : bindings) {
        msix_binding mb = {idx++, nullptr, b.thread};

        if(_is_active_mask) {
            mb.isr = [this, idx] { _disable_int(idx); };
            b.assigned_idx(idx);
        }

        msix_vec.push_back(mb);
    }

    if(!_msi.easy_register(msix_vec)) {
        throw std::runtime_error("Failed to register interrupts");
    }

    _msix_registered = true;
    _num_interrupts = bindings.size();
}

void vmxnet3_intr_mgr::easy_register(u32 intr_cfg,
                                     const std::vector<binding> &bindings)
{
    _is_auto_mask = false;
    _is_active_mask = false;

    switch ((intr_cfg >> VMXNET3_IT_MODE_SHIFT) & VMXNET3_IT_MODE_MASK)
    {
    case VMXNET3_IMM_AUTO:
        _is_auto_mask = true;
        break;
    case VMXNET3_IMM_ACTIVE:
        _is_active_mask = true;
        break;
    }

    switch (intr_cfg & VMXNET3_IT_TYPE_MASK)
    {
    case VMXNET3_IT_AUTO:
    case VMXNET3_IT_MSIX:
        easy_register_msix(bindings);
        break;
    default:
        throw std::runtime_error("Non-MSIX interrupts not supported yet");
    }
}

void vmxnet3_intr_mgr::easy_unregister(void)
{
    if(_msix_registered) {
        _msi.easy_unregister();
        _msix_registered = false;
        _num_interrupts = 0;
    }
}

}
