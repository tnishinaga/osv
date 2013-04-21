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

void setup_free_memory()
{
    auto ret = hypercall(__HYPERVISOR_vm_assist, VMASST_CMD_enable, VMASST_TYPE_writable_pagetables);
    assert(ret == 0);
    u64* base_pt = reinterpret_cast<u64*>(xen_start_info->pt_base);
    // FIXME: is 1:1 virt:phys guaranteed? what if the kernel is at 0xffffblah?
    auto base_pt_pfn = (xen_start_info->pt_base) >> 12;
    auto mfn_list = reinterpret_cast<const u64*>(xen_start_info->mfn_list);
    auto base_pt_mfn = mfn_list[base_pt_pfn];
    // FIXME: assumes phys_map at 0xffffc00000000000
    ulong ptr = (base_pt_mfn << 12) | 0xc00;
    ptr |= MMU_NORMAL_PT_UPDATE;
    mmu_update req { ptr, base_pt[0] };
    unsigned done;
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
