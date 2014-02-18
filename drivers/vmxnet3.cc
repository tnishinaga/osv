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

static int if_ioctl(struct ifnet *ifp, u_long command, caddr_t data)
{
    vmxnet3_d("if_ioctl %x", command);

    int error = 0;
    switch(command) {
    case SIOCSIFMTU:
        vmxnet3_d("SIOCSIFMTU");
        break;
    case SIOCSIFFLAGS:
        vmxnet3_d("SIOCSIFFLAGS");
        /* Change status ifup, ifdown */
        if (ifp->if_flags & IFF_UP) {
            ifp->if_drv_flags |= IFF_DRV_RUNNING;
            vmxnet3_d("if_up");
        } else {
            ifp->if_drv_flags &= ~IFF_DRV_RUNNING;
            vmxnet3_d("if_down");
        }
        break;
    case SIOCADDMULTI:
    case SIOCDELMULTI:
        vmxnet3_d("SIOCDELMULTI");
        break;
    default:
        vmxnet3_d("redirecting to ether_ioctl()...");
        error = ether_ioctl(ifp, command, data);
        break;
    }

    return(error);
}

/**
 * Invalidate the local Tx queues.
 * @param ifp upper layer instance handle
 */
static void if_qflush(struct ifnet *ifp)
{
    /*
     * Since virtio_net currently doesn't have any Tx queue we just
     * flush the upper layer queues.
     */
    ::if_qflush(ifp);
}

/**
 * Transmits a single mbuf instance.
 * @param ifp upper layer instance handle
 * @param m_head mbuf to transmit
 *
 * @return 0 in case of success and an appropriate error code
 *         otherwise
 */
static int if_transmit(struct ifnet* ifp, struct mbuf* m_head)
{
    vmxnet3* vmx = (vmxnet3*)ifp->if_softc;

    vmxnet3_d("*** processing packet! ***");

    vmx->transmit(m_head);

    return 0;
}

static void if_init(void* xsc)
{
    vmxnet3_d("vmxnet3 init");
}

/**
 * Return all the statistics we have gathered.
 * @param ifp
 * @param out_data
 */
static void if_getinfo(struct ifnet* ifp, struct if_data* out_data)
{
    // First - take the ifnet data
    memcpy(out_data, &ifp->if_data, sizeof(*out_data));
}

template<class T> void slice_memory(void *&va, T &holder)
{
    for (auto &e : holder) {
        e.attach(va);
        va += e.size();
    }
}

void vmxnet3_txqueue::attach(void* storage) {
    printf("%s storage=%p pa=%lx\n", __PRETTY_FUNCTION__, storage, mmu::virt_to_phys(storage));
    vmxnet3_layout_holder::attach(storage);
}

void vmxnet3_rxqueue::attach(void* storage) {
    printf("%s storage=%p pa=%lx\n", __PRETTY_FUNCTION__, storage, mmu::virt_to_phys(storage));
    vmxnet3_layout_holder::attach(storage);
}

void vmxnet3_txqueue::init(std::function<void (void)> isr_handler)
{
    _isr_handler = isr_handler;
    printf("%s cmd_ring=%lx\n",
        __func__, cmd_ring.get_desc_pa());
    printf("%s cmd_ring_len=%zd\n",
        __func__, cmd_ring.get_desc_num());
    printf("%s comp_ring=%lx\n",
        __func__, comp_ring.get_desc_pa());
    printf("%s comp_ring_len=%zd\n",
        __func__, comp_ring.get_desc_num());
    printf("%s driver_data=%lx\n",
        __func__, mmu::virt_to_phys(this));
    printf("%s driver_data_len=%zd\n",
        __func__, sizeof(*this));
    layout->cmd_ring = cmd_ring.get_desc_pa();
    layout->cmd_ring_len = cmd_ring.get_desc_num();
    layout->comp_ring = comp_ring.get_desc_pa();
    layout->comp_ring_len = comp_ring.get_desc_num();

    layout->driver_data = mmu::virt_to_phys(this);
    layout->driver_data_len = sizeof(*this);

    start_isr_thread();
}

void vmxnet3_rxqueue::init(std::function<void (void)> isr_handler)
{
    _isr_handler = isr_handler;
    for (unsigned i = 0; i < VMXNET3_RXRINGS_PERQ; i++) {
        printf("%s cmd_ring[%d] = %lx\n",
            __PRETTY_FUNCTION__, i, cmd_rings[i].get_desc_pa());
        printf("%s cmd_ring_len[%d] = %u\n",
            __PRETTY_FUNCTION__, i, cmd_rings[i].get_desc_num());
        layout->cmd_ring[i] = cmd_rings[i].get_desc_pa();
        layout->cmd_ring_len[i] = cmd_rings[i].get_desc_num();
    }
    printf("%s comp_ring = %lx\n",
        __PRETTY_FUNCTION__, comp_ring.get_desc_pa());
    printf("%s com_ring_len = %u\n",
        __PRETTY_FUNCTION__, comp_ring.get_desc_num());

    layout->comp_ring = comp_ring.get_desc_pa();
    layout->comp_ring_len = comp_ring.get_desc_num();
    printf("%s driver_data = %lx\n",
        __PRETTY_FUNCTION__, mmu::virt_to_phys(this));
    printf("%s driver_data_len = %u\n",
        __PRETTY_FUNCTION__, sizeof(*this));

    layout->driver_data = mmu::virt_to_phys(this);
    layout->driver_data_len = sizeof(*this);

    for (unsigned i = 0; i < 1; i++) {
        auto &rxr = cmd_rings[i];
        rxr.fill = 0;
        rxr.gen = vmxnet3::VMXNET3_INIT_GEN;
        rxr.clear_descs();

        for (unsigned idx = 0; idx < rxr.get_desc_num(); idx++) {
            auto error = newbuf(i);
            if (error)
                throw std::runtime_error("buf allocation failed");
        }
    }

    for (unsigned i = 1; i < VMXNET3_RXRINGS_PERQ; i++) {
        auto &rxr = cmd_rings[i];
        rxr.fill = 0;
        rxr.gen = 0;
        rxr.clear_descs();
#if 0
        for (unsigned idx = 0; idx < rxr.get_desc_num(); idx++) {
            auto &rxd = rxr.get_desc(idx);
            printf("%s rid=%u idx=%u pa:%p addr:%p len:%u btype:%u gen:%u\n", 
                __PRETTY_FUNCTION__, 
                i, idx, 
                mmu::virt_to_phys(rxd.layout),
                rxd.layout->addr, 
                rxd.layout->len, 
                rxd.layout->btype, 
                rxd.layout->gen);
        }
#endif
    }

    auto &rxc = comp_ring;
    rxc.next = 0;
    rxc.gen = vmxnet3::VMXNET3_INIT_GEN;
    rxc.clear_descs();
#if 0
    for (unsigned idx = 0; idx < rxc.get_desc_num(); idx++) {
        auto &rxcd = rxc.get_desc(idx);
        printf("%s rxcd idx:%u pa:%lx rxd_idx:%u eop:%u sop:%u qid:%u len:%u udp:%u tcp:%u ipv6:%u ipv4:%u type:%u gen:%u\n",
            __PRETTY_FUNCTION__,
            idx,
            mmu::virt_to_phys(rxcd.layout),
            rxcd.layout->rxd_idx,
            rxcd.layout->eop,
            rxcd.layout->sop,
            rxcd.layout->qid,
            rxcd.layout->len,
            rxcd.layout->udp,
            rxcd.layout->tcp,
            rxcd.layout->ipv6,
            rxcd.layout->ipv4,
            rxcd.layout->type,
            rxcd.layout->gen);
    }
#endif
    start_isr_thread();
}

void vmxnet3_rxqueue::discard(int rid, int idx)
{
    auto &rxr = cmd_rings[rid];
    auto &rxd = rxr.get_desc(idx);
    rxd.layout->gen = rxr.gen;
    rxr.increment_fill();
}

void vmxnet3_rxqueue::discard_chain(int rid)
{
    while (1) {
        auto &rxc = comp_ring;
        auto &rxcd = rxc.get_desc(rxc.next);
        if (rxcd.layout->gen != rxc.gen)
            break;
        rmb();

        if (++rxc.next == rxc.get_desc_num()) {
            rxc.next = 0;
            rxc.gen ^= 1;
        }

        auto idx = rxcd.layout->rxd_idx;
        auto eof = rxcd.layout->eop;
        discard(rid, idx);
        if (eof)
            break;
    }
}

int vmxnet3_rxqueue::newbuf(int rid)
{
    auto &rxr = cmd_rings[rid];
    auto idx = rxr.fill;
    auto &rxd = rxr.get_desc(idx);
    int flags, clsize, btype;
    if (rid == 0 && (idx % 1) == 0) {
        flags = M_PKTHDR;
        clsize = MCLBYTES;
        btype = vmxnet3::VMXNET3_BTYPE_HEAD;
    }else{
        flags = 0;
        clsize = MJUMPAGESIZE;
        btype = vmxnet3::VMXNET3_BTYPE_BODY;
    }
    auto m = m_getjcl(M_NOWAIT, MT_DATA, flags, clsize);
    if (m == NULL)
        return -1;
    if (btype == vmxnet3::VMXNET3_BTYPE_HEAD) {
        m->m_hdr.mh_len = m->M_dat.MH.MH_pkthdr.len = clsize;
        m_adj(m, ETHER_ALIGN);
    }else
        m->m_hdr.mh_len = clsize;

    buf[rid][idx] = m;

    rxd.layout->addr = mmu::virt_to_phys(m->m_hdr.mh_data);
    rxd.layout->len = m->m_hdr.mh_len;
    rxd.layout->btype = btype;
    rxd.layout->gen = rxr.gen;
/*
    printf("%s rid=%u idx=%u pa:%p addr:%p len:%u btype:%u gen:%u\n", 
        __PRETTY_FUNCTION__, 
        rid, idx, 
        mmu::virt_to_phys(rxd.layout),
        rxd.layout->addr, 
        rxd.layout->len, 
        rxd.layout->btype, 
        rxd.layout->gen);
*/

    rxr.increment_fill();
    return 0;
}


vmxnet3::vmxnet3(pci::device &dev)
    : vmware_driver(dev)
    , _drv_shared_mem(vmxnet3_drv_shared::size(),
                        VMXNET3_DRIVER_SHARED_ALIGN)
    , _queues_shared_mem(vmxnet3_txq_shared::size() * VMXNET3_TX_QUEUES +
                            vmxnet3_rxq_shared::size() * VMXNET3_RX_QUEUES,
                            VMXNET3_QUEUES_SHARED_ALIGN)
    , _mcast_list(VMXNET3_MULTICAST_MAX * VMXNET3_ETH_ALEN, VMXNET3_MULTICAST_ALIGN)
    , _int_mgr(&dev, [this] (unsigned idx) { /* this->disable_interrupt(idx); */ })
{
    parse_pci_config();
    stop();
    disable_interrupts();
    vmxnet3_i("VMXNET3 INSTANCE");
    _id = _instance++;
    _drv_shared.attach(_drv_shared_mem.get_va());
    attach_queues_shared();

    do_version_handshake();
    allocate_interrupts();
    fill_driver_shared();

    start_isr_thread();
    enable_device();

    //initialize the BSD interface _if
    _ifn = if_alloc(IFT_ETHER);
    if (_ifn == NULL) {
       //FIXME: need to handle this case - expand the above function not to allocate memory and
       // do it within the constructor.
       vmxnet3_w("if_alloc failed!");
       return;
    }

    if_initname(_ifn, "eth", _id);
    _ifn->if_mtu = ETHERMTU;
    _ifn->if_softc = static_cast<void*>(this);
    _ifn->if_flags = IFF_BROADCAST /*| IFF_MULTICAST*/;
    _ifn->if_ioctl = if_ioctl;
    _ifn->if_transmit = if_transmit;
    _ifn->if_qflush = if_qflush;
    _ifn->if_init = if_init;
    _ifn->if_getinfo = if_getinfo;
    IFQ_SET_MAXLEN(&_ifn->if_snd, 1);

    _ifn->if_capabilities = 0;

    u_int8_t macaddr[6];
    get_mac_address(macaddr);
    ether_ifattach(_ifn, macaddr);
//    enable_interrupts();
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

void vmxnet3::allocate_interrupts()
{
    std::vector<vmxnet3_intr_mgr::binding> ints;

    fill_intr_requirements(_txq, ints);
    fill_intr_requirements(_rxq, ints);
    fill_intr_requirement(this, ints);

    auto intr_cfg = read_cmd(VMXNET3_CMD_GET_INTRCFG);
    _int_mgr.easy_register(intr_cfg, ints);
}

void vmxnet3::enable_interrupts()
{
    for (unsigned irq = 0; irq < VMXNET3_NUM_INTRS; irq++)
        _bar0->writel(VMXNET3_BAR0_IMASK(irq), 0);
}

void vmxnet3::disable_interrupts()
{
    for (unsigned irq = 0; irq < VMXNET3_NUM_INTRS; irq++)
        _bar0->writel(VMXNET3_BAR0_IMASK(irq), 1);
}

void vmxnet3::attach_queues_shared()
{
    auto *va = _queues_shared_mem.get_va();

    slice_memory(va, _txq);
    slice_memory(va, _rxq);

    for (auto &q : _txq) {
        q.init([&] { printf("%s\n", __PRETTY_FUNCTION__); /* gc_work(); */} );
    }
    for (auto &q : _rxq) {
        q.init([&] { printf("%s\n", __PRETTY_FUNCTION__); /* receive_work(); */} );
    }
    sched::thread rx_thread([&] { receive_work(); });
    rx_thread.start();
}

void vmxnet3::fill_driver_shared()
{
    _drv_shared.set_driver_data(mmu::virt_to_phys(this), sizeof(*this));
    printf("%s driver_data=%lx driver_data_len=%zd\n",
        __func__, mmu::virt_to_phys(this), sizeof(*this));
    _drv_shared.set_queue_shared(_queues_shared_mem.get_pa(),
                                 _queues_shared_mem.get_size());
    printf("%s queue_shared=%lx queue_shared_len=%zd\n",
        __func__, _queues_shared_mem.get_pa(),
        _queues_shared_mem.get_size());
    _drv_shared.set_max_sg_len(VMXNET3_MAX_RX_SEGS);
    _drv_shared.set_mcast_table(_mcast_list.get_pa(),
                                _mcast_list.get_size());
    _drv_shared.set_intr_config(static_cast<u8>(_int_mgr.interrupts_number()),
                                static_cast<u8>(_int_mgr.is_automask()));
    _drv_shared.layout->upt_features = 0;
    _drv_shared.layout->mtu = 1500;
    _drv_shared.layout->ntxqueue = 1;
    _drv_shared.layout->nrxqueue = 1;
    _drv_shared.layout->rxmode = VMXNET3_RXMODE_UCAST | VMXNET3_RXMODE_BCAST | VMXNET3_RXMODE_ALLMULTI | VMXNET3_RXMODE_MCAST;
    printf("%s drv_shared_mem=%lx\n",
        __func__, _drv_shared_mem.get_pa());
    _bar1->writel(VMXNET3_BAR1_DSL, _drv_shared_mem.get_pa());
    _bar1->writel(VMXNET3_BAR1_DSH,
        reinterpret_cast<u64>(_drv_shared_mem.get_pa()) >> 32);
    write_cmd(VMXNET3_CMD_SET_FILTER);
    write_cmd(VMXNET3_CMD_SET_RXMODE);
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

void vmxnet3::parse_pci_config()
{
    _bar0 = _dev.get_bar(1);
    _bar0->map();
    if (_bar0 == nullptr) {
        throw std::runtime_error("BAR1 is absent");
    }

    _bar1 = _dev.get_bar(2);
    _bar1->map();
    if (_bar1 == nullptr) {
        throw std::runtime_error("BAR2 is absent");
    }
}

void vmxnet3::stop()
{
    write_cmd(VMXNET3_CMD_DISABLE);
    write_cmd(VMXNET3_CMD_RESET);
}

void vmxnet3::enable_device()
{
    read_cmd(VMXNET3_CMD_ENABLE);
    _bar0->writel(VMXNET3_BAR0_RXH1, 0);
    _bar0->writel(VMXNET3_BAR0_RXH2, 0);
}

void vmxnet3::do_version_handshake()
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

void vmxnet3::transmit(struct mbuf *m)
{
    WITH_LOCK(_lock) {
        printf("%s m=%p\n", __PRETTY_FUNCTION__, m);
        txq_encap(_txq[0], m);
    }
}

void vmxnet3::gc_work()
{
    while(1) {
        WITH_LOCK(_lock) {
            sched::thread::wait_until(_lock, [&] { return txq_gc_avail(_txq[0]); });
            printf("%s\n", __PRETTY_FUNCTION__);

            txq_gc(_txq[0]);
        }
    }
}

void vmxnet3::receive_work()
{
    while(1) {
        WITH_LOCK(_lock) {
//            sched::thread::wait_until(_lock, [&] { return rxq_avail(_rxq[0]); });
            sched::thread::sleep(std::chrono::milliseconds(100));
            printf("%s\n", __PRETTY_FUNCTION__);
            rxq_eof(_rxq[0]);
        }
    }
}

void vmxnet3::txq_encap(vmxnet3_txqueue &txq, struct mbuf *m_head)
{
    auto &txr = txq.cmd_ring;
    auto &txd = txr.get_desc(txr.head);
    auto &sop = txr.get_desc(txr.head);
    auto gen = txr.gen ^ 1; // Owned by cpu (yet)

    for (auto m = m_head; m != NULL; m = m->m_hdr.mh_next) {
        printf("%s m=%p\n", __PRETTY_FUNCTION__, m);
        txq.buf[txr.head] = m;
        txd = txr.get_desc(txr.head);
        txd.layout->addr = mmu::virt_to_phys(m->m_hdr.mh_data);
        txd.layout->len = m->m_hdr.mh_len;
        txd.layout->gen = gen;
        txd.layout->dtype = 0;
        txd.layout->offload_mode = VMXNET3_OM_NONE;
        txd.layout->offload_pos = 0;
        txd.layout->hlen = 0;
        txd.layout->eop = 0;
        txd.layout->compreq = 0;
        txd.layout->vtag_mode = 0;
        txd.layout->vtag = 0;

        if (++txr.head == txr.get_desc_num()) {
            txr.head = 0;
            txr.gen ^= 1;
        }
        gen = txr.gen;
    }
    txd.layout->eop = 1;
    txd.layout->compreq = 1;

    // Finally, change the ownership.
    wmb();
    sop.layout->gen ^= 1;

    if (++txq.layout->npending >= txq.layout->intr_threshold) {
        txq.layout->npending = 0;
        _bar0->writel(VMXNET3_BAR0_TXH, txr.head);
    }
    _bar0->writel(VMXNET3_BAR0_TXH, _txq[0].cmd_ring.head);
}

void vmxnet3::txq_gc(vmxnet3_txqueue &txq)
{
    auto &txc = txq.comp_ring;
    while(1) {
        printf("%s\n", __PRETTY_FUNCTION__);
        auto &txcd = txc.get_desc(txc.next);
        if (txcd.layout->gen != txc.gen)
            break;
        rmb();
        if (++txc.next == txc.get_desc_num()) {
            txc.next = 0;
            txc.gen ^= 1;
        }

        auto sop = txc.next;
        auto m = txq.buf[sop];

        if (m) {
            printf("%s m_freem(%p)\n", __PRETTY_FUNCTION__, m);
            m_freem(m);
            txq.buf[sop] = NULL;
        }

        txc.next = 
            (txcd.layout->eop_idx + 1 ) % txc.get_desc_num();
    }
}

bool vmxnet3::txq_gc_avail(vmxnet3_txqueue &txq)
{
    auto &txc = txq.comp_ring;
    auto &txcd = txc.get_desc(txc.next);
    printf("%s avail?=%d\n", __PRETTY_FUNCTION__, (txcd.layout->gen == txc.gen));
    return (txcd.layout->gen == txc.gen);
}

void vmxnet3::rxq_eof(vmxnet3_rxqueue &rxq)
{
    auto &rxc = rxq.comp_ring;
    struct mbuf *m_head = NULL, *m_tail = NULL;

    printf("%s rxc next:%d gen:%d\n", __PRETTY_FUNCTION__,
        rxc.next, rxc.gen);

    while(1) {
        auto &rxcd = rxc.get_desc(rxc.next);
        assert(rxcd.layout->qid <= 2);

        printf("%s rxcd rxd_idx:%u eop:%u sop:%u qid:%u len:%u udp:%u tcp:%u ipv6:%u ipv4:%u type:%u gen:%u\n",
            __PRETTY_FUNCTION__,
            rxcd.layout->rxd_idx,
            rxcd.layout->eop,
            rxcd.layout->sop,
            rxcd.layout->qid,
            rxcd.layout->len,
            rxcd.layout->udp,
            rxcd.layout->tcp,
            rxcd.layout->ipv6,
            rxcd.layout->ipv4,
            rxcd.layout->type,
            rxcd.layout->gen);

        if (rxcd.layout->gen != rxc.gen)
            break;
        rmb();

        if (++rxc.next == rxc.get_desc_num()) {
            rxc.next = 0;
            rxc.gen ^= 1;
        }

        auto rid = rxcd.layout->qid;
        auto idx = rxcd.layout->rxd_idx;
        auto length = rxcd.layout->len;
        auto &rxr = rxq.cmd_rings[rid];
        auto &rxd = rxr.get_desc(idx);
        auto m = rxq.buf[rid][idx];
        printf("%s rid=%u idx=%u\n", __PRETTY_FUNCTION__, rid, idx);
        printf("%s rxd addr:%lx len:%u btype:%u dtype:%u gen:%u\n",
            __PRETTY_FUNCTION__,
            rxd.layout->addr,
            rxd.layout->len,
            rxd.layout->btype,
            rxd.layout->dtype,
            rxd.layout->gen);
        printf("%s m data=%p\n",
            __PRETTY_FUNCTION__,
            mmu::virt_to_phys(m->m_hdr.mh_data));
        assert(m != NULL);

        if (rxr.fill != idx) {
            while(rxr.fill != idx) {
                rxr.get_desc(rxr.fill).layout->gen = rxr.gen;
                rxr.increment_fill();
            }
        }

        if (rxcd.layout->sop) {
            assert(rxd.layout->btype == VMXNET3_BTYPE_HEAD);
            assert((idx % 1) == 0);
            assert(m_head == NULL);

            if (length == 0) {
                rxq.discard(rid, idx);
                goto next;
            }

            if (rxq.newbuf(rid) != 0) {
                rxq.discard(rid, idx);
                if (!rxcd.layout->eop)
                    rxq.discard_chain(rid);
                goto next;
            }

            m->M_dat.MH.MH_pkthdr.len = length;
            m->M_dat.MH.MH_pkthdr.rcvif = _ifn;
            m->M_dat.MH.MH_pkthdr.csum_flags = 0;
            m->m_hdr.mh_len = length;
            m_head = m_tail = m;
        } else {
            assert(rxd.layout->btype == VMXNET3_BTYPE_BODY);
            assert(m_head != NULL);

            if (rxq.newbuf(rid) != 0) {
                rxq.discard(rid, idx);
                if (!rxcd.layout->eop)
                    rxq.discard_chain(rid);
                m_freem(m_head);
                m_head = m_tail = NULL;
                goto next;
            }

            m->m_hdr.mh_len = length;
            m_head->M_dat.MH.MH_pkthdr.len += length;
            m_tail->m_hdr.mh_next = m;
            m_tail = m;
        }

        if (rxcd.layout->eop) {
            printf("%s if_input m=%p\n",
                __PRETTY_FUNCTION__, m);
            (*_ifn->if_input)(_ifn, m);
            m_head = m_tail = NULL;
        }

next:
        if (rxq.layout->update_rxhead) {
            idx = (idx + 1) % rxr.get_desc_num();
            if (rid == 0)
                _bar0->writel(VMXNET3_BAR0_RXH1, idx);
            else
                _bar0->writel(VMXNET3_BAR0_RXH2, idx);
        }
    }
}

bool vmxnet3::rxq_avail(vmxnet3_rxqueue &rxq)
{
    auto &rxc = rxq.comp_ring;
    auto &rxcd = rxc.get_desc(rxc.next);
    assert(rxcd.layout->qid <= 2);

    printf("%s avail?=%d\n", __PRETTY_FUNCTION__, (rxcd.layout->gen == rxc.gen));
    return (rxcd.layout->gen == rxc.gen);
}

void vmxnet3::get_mac_address(u_int8_t *macaddr)
{
    auto macl = read_cmd(VMXNET3_CMD_GET_MACL);
    auto mach = read_cmd(VMXNET3_CMD_GET_MACH);
    macaddr[0] = macl;
    macaddr[1] = macl >> 8;
    macaddr[2] = macl >> 16;
    macaddr[3] = macl >> 24;
    macaddr[4] = mach;
    macaddr[5] = mach >> 8;
}

void vmxnet3_intr_mgr::easy_register_msix(const std::vector<binding> &bindings)
{
    std::vector<msix_binding> msix_vec;
    unsigned idx = 0;

    for (auto b : bindings) {
        msix_binding mb = {idx++, nullptr, b.thread};

        if(_is_active_mask) {
            printf("%s is_active_mask=true\n", __PRETTY_FUNCTION__);
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

void vmxnet3_intr_mgr::easy_unregister()
{
    if(_msix_registered) {
        _msi.easy_unregister();
        _msix_registered = false;
        _num_interrupts = 0;
    }
}

template<class DescT, int NDesc>
void vmxnet3_ring<DescT, NDesc>::increment_fill()
{
    if (++fill == get_desc_num()) {
        fill = 0;
        gen ^= 1;
    }
}

}
