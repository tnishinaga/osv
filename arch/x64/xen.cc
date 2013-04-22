#include "osv/types.h"
#include "xen.hh"
#include <xen/xen.h>
#include "mmu.hh"
#include "mempool.hh"

// make sure xen_start_info is not in .bss, or it will be overwritten
// by init code, as xen_init() is called before .bss initialization
struct start_info* xen_start_info = reinterpret_cast<start_info*>(1);
extern void* xen_bootstrap_end;
extern char xen_hypercall_page[];
extern "C" { ulong xen_hypercall_5(ulong a1, ulong a2, ulong a3, ulong a4, ulong a5, unsigned type); }

namespace xen {

bool is_enabled;

// we only have asm constraints for the first three hypercall args,
// so we only inline hypercalls with <= 3 args.  The others are out-of-line,
// implemented in normal asm.
inline ulong hypercall(unsigned type, ulong a1, ulong a2, ulong a3)
{
    ulong ret;
    asm volatile("call *%[hyp]"
                 : "=a"(ret), "+D"(a1), "+S"(a2), "+d"(a3)
                 : [hyp]"c"(xen_hypercall_page + 32 * type)
                 : "memory", "r10", "r8");
    return ret;
}

inline ulong hypercall(unsigned type, ulong a1, ulong a2, ulong a3, ulong a4, ulong a5)
{
    // swap argument order to get the first three register pre-loaded
    return xen_hypercall_5(a1, a2, a3, a4, a5, type);
}

inline ulong hypercall(unsigned type)
{
    return hypercall(type, 0, 0, 0);
}

inline ulong hypercall(unsigned type, ulong a1)
{
    return hypercall(type, a1, 0, 0);
}

inline ulong hypercall(unsigned type, ulong a1, ulong a2)
{
    return hypercall(type, a1, a2, 0);
}

inline ulong hypercall(unsigned type, ulong a1, ulong a2, ulong a3, ulong a4)
{
    return hypercall(type, a1, a2, a3, a4, 0);
}

// some template magic to auto-cast pointers to ulongs in hypercall().

inline ulong cast_pointer(ulong v)
{
    return v;
}

inline ulong cast_pointer(void* p)
{
    return reinterpret_cast<ulong>(p);
}

template <typename... T>
inline ulong
hypercall(unsigned type, T... args)
{
    return hypercall(type, cast_pointer(args)...);
}

unsigned pt_index(void* vaddr, unsigned level)
{
    ulong v = reinterpret_cast<ulong>(vaddr);
    return (v >> (12 + level * 9)) & 511;
}

void setup_free_memory()
{
    auto ret = hypercall(__HYPERVISOR_vm_assist, VMASST_CMD_enable, VMASST_TYPE_writable_pagetables);
    assert(ret == 0);
    u64* base_pt = reinterpret_cast<u64*>(xen_start_info->pt_base);
    // FIXME: is 1:1 virt:phys guaranteed? what if the kernel is at 0xffffblah?
    auto base_pt_pfn = (xen_start_info->pt_base) >> 12;
    auto mfn_list = reinterpret_cast<const u64*>(xen_start_info->mfn_list);
    auto nr_pages = xen_start_info->nr_pages;
    auto base_pt_mfn = mfn_list[base_pt_pfn];
    // FIXME: assumes phys_map at 0xffffc00000000000
    ulong ptr = (base_pt_mfn << 12) | 0xc00;
    ptr |= MMU_NORMAL_PT_UPDATE;
    mmu_update req { ptr, base_pt[0] };
    unsigned done;
    ret = hypercall(__HYPERVISOR_mmu_update, &req, 1, &done, DOMID_SELF);
    assert(ret == 0);
    assert(done == 1);
    void* free_mem_start = xen_bootstrap_end;
    void* free_mem_end = free_mem_start + 512 * 1024;

    auto tmp_alloc_pt = [&]() { return static_cast<u64*>(free_mem_end -= 4096); };

    auto tmp_v2m = [=](void* v) {
        // FIXME: is 1:1 virt:phys guaranteed? what if the kernel is at 0xffffblah?
        return (mfn_list[reinterpret_cast<ulong>(v) >> 12] << 12)
                | (reinterpret_cast<ulong>(v) & 4095);
    };

    auto tmp_m2p = [=](u64 m) {
        return std::find(mfn_list, mfn_list + nr_pages, m) - mfn_list;
    };

    auto tmp_p2v = [=](u64 p) {
        // FIXME: is this guaranteed?
        return reinterpret_cast<void*>(p << 12);
    };

    auto tmp_m2v = [=](u64 m) {
        return tmp_p2v(tmp_m2p(m));
    };

    auto down_one_level = [=](u64* table, void* v, unsigned level) {
        u64 pte = table[pt_index(v, level)];
        assert(pte & 1);
        u64 mfn = pte >> 12;
        return static_cast<u64*>(tmp_m2v(mfn));
    };

    auto mark_page_ro = [=](void* v) {
        u64* pt = down_one_level(base_pt, v, 3);
        pt = down_one_level(pt, v, 2);
        pt = down_one_level(pt, v, 1);
        auto ptep = &pt[pt_index(v, 0)];
        mmu_update req = { tmp_v2m(ptep) | MMU_NORMAL_PT_UPDATE, *ptep & ~(u64)2 };
        unsigned done;
        auto ret = hypercall(__HYPERVISOR_mmu_update, &req, 1, &done);
        assert(ret == 0 && done == 1);
    };

    auto tmp_pt_l0 = tmp_alloc_pt();
    auto tmp_pt_l1 = tmp_alloc_pt();
    auto tmp_pt_l2 = tmp_alloc_pt();

    // force-map shared_info into temporary page table so irq enable/disable can work
    tmp_pt_l0[pt_index(shared, 0)] = (xen_start_info->shared_info) | 0x67;
    tmp_pt_l1[pt_index(shared, 1)] = tmp_v2m(tmp_pt_l0) | 0x67;
    tmp_pt_l2[pt_index(shared, 2)] = tmp_v2m(tmp_pt_l1) | 0x67;

    // mark page tables read-only for Xen
    mark_page_ro(tmp_pt_l2);
    mark_page_ro(tmp_pt_l1);
    mark_page_ro(tmp_pt_l0);

    assert(base_pt[pt_index(shared, 3)] == 0);
    req.ptr = (base_pt_mfn << 12) | (pt_index(shared, 3) * 8) | MMU_NORMAL_PT_UPDATE;
    req.val = tmp_v2m(tmp_pt_l2) | 0x67;
    ret = hypercall(__HYPERVISOR_mmu_update, &req, 1, &done, DOMID_SELF);
    assert(ret == 0);
    assert(done == 1);
}

__attribute__((constructor(101)))
void setup()
{
    is_enabled = true;
}

}

extern "C"
void xen_init(struct start_info* si)
{
    xen_start_info = si;
}
