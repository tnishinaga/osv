#include "xen.hh"
#include "debug.hh"
#include "mmu.hh"
#include "processor.hh"
#include "cpuid.hh"
#include "exceptions.hh"
#include "sched.hh"
#include "msr.hh"
#include <bsd/porting/pcpu.h>
#include <bsd/machine/xen/xen-os.h>

shared_info_t *HYPERVISOR_shared_info;
uint8_t xen_features[XENFEAT_NR_SUBMAPS * 32];
// make sure xen_start_info is not in .bss, or it will be overwritten
// by init code, as xen_init() is called before .bss initialization
struct start_info* xen_start_info __attribute__((section(".data")));

namespace xen {

extern "C" { ulong xen_hypercall_5(ulong a1, ulong a2, ulong a3, ulong a4, ulong a5, unsigned type); }


// we only have asm constraints for the first three hypercall args,
// so we only inline hypercalls with <= 3 args.  The others are out-of-line,
// implemented in normal asm.
inline ulong hypercall(unsigned type, ulong a1, ulong a2, ulong a3)
{
    ulong ret;
    asm volatile("call *%[hyp]"
                 : "=a"(ret), "+D"(a1), "+S"(a2), "+d"(a3)
                 : [hyp]"c"(hypercall_page + 32 * type)
                 : "memory", "r10", "r8");
    return ret;
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

inline ulong hypercall(unsigned type, ulong a1, ulong a2, ulong a3, ulong a4, ulong a5)
{
    // swap argument order to get the first three register pre-loaded
    return xen_hypercall_5(a1, a2, a3, a4, a5, type);
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
memory_hypercall(unsigned type, T... args)
{
    return hypercall(__HYPERVISOR_memory_op, type, cast_pointer(args)...);
}

template <typename... T>
inline ulong
version_hypercall(unsigned type, T... args)
{
    return hypercall(__HYPERVISOR_xen_version, type, cast_pointer(args)...);
}

inline ulong
hvm_hypercall(unsigned type, struct xen_hvm_param *param)
{
    return hypercall(__HYPERVISOR_hvm_op, type, cast_pointer(param));
}

inline ulong
set_segment_base(u32 segment, u64 base)
{
    return hypercall(__HYPERVISOR_set_segment_base, segment, base);
}

inline void
mmu_hypercall(mmu_update_t *req, unsigned long nr)
{
    unsigned long count;
    ulong ret = hypercall(__HYPERVISOR_mmu_update, cast_pointer(req), nr, cast_pointer(&count), DOMID_SELF);
    assert(ret == 0);
    assert(nr == count);
}

inline void
update_va_mapping(void *va, unsigned long pa)
{
    ulong ret = hypercall(__HYPERVISOR_update_va_mapping, cast_pointer(va), (pa | 0x67), UVMF_INVLPG);
    assert(ret == 0);
}

inline void
update_va_mapping(void *va, unsigned long pa, int flags)
{
    ulong ret = hypercall(__HYPERVISOR_update_va_mapping, cast_pointer(va), (pa | flags), UVMF_INVLPG);
    assert(ret == 0);
}

struct xen_shared_info xen_shared_info __attribute__((aligned(4096)));

static bool xen_pci_enabled()
{
    u16 magic = processor::inw(0x10);
    if (magic != 0x49d2) {
        return false;
    }

    u8 version = processor::inw(0x12);

    if (version != 0) {
        processor::outw(0xffff, 0x10); // product: experimental
        processor::outl(0, 0x10); // build id: whatever
        u16 _magic = processor::inw(0x10); // just make sure we are not blacklisted
        if (_magic != 0x49d2) {
            return false;
        }
    }
    processor::outw(3 ,0x10); // 2 => NICs, 1 => BLK
    return true;
}

#define HVM_PARAM_CALLBACK_IRQ 0
extern "C" void evtchn_do_upcall(void *a);

static void xen_ack_irq()
{
    auto cpu = sched::cpu::current();
    HYPERVISOR_shared_info->vcpu_info[cpu->id].evtchn_upcall_pending = 0; 
}
static u64 *mfn_list;
static unsigned long nr_pages;

void setup_free_memory()
{
    auto ret = hypercall(__HYPERVISOR_vm_assist, VMASST_CMD_enable, VMASST_TYPE_writable_pagetables);
    assert(ret == 0);

    mfn_list = reinterpret_cast<u64*>(xen_start_info->mfn_list);
    nr_pages = xen_start_info->nr_pages;

    // Because we have xen_shared_info inside our ELF, we know for sure that
    // this is already backed by inner level page tables. We can therefore use
    // the much simpler update_va_mapping to establish the mapping.
    update_va_mapping(&xen_shared_info, xen_start_info->shared_info);
    HYPERVISOR_shared_info = reinterpret_cast<shared_info_t *>(&xen_shared_info);
}

int xen_write_msr(u32 index, u64 data)
{
    switch (index) {
        case (u32)msr::IA32_FS_BASE:
            return set_segment_base(index, data);
        default:
            abort();
    }
    return 0;
}

u64 xen_read_msr(u32 index)
{
    abort();
    return 0;
}


void xen_set_callback()
{
    struct xen_hvm_param xhp;

    xhp.domid = DOMID_SELF;
    xhp.index = HVM_PARAM_CALLBACK_IRQ;

    auto vector = idt.register_interrupt_handler(
            [] {}, // pre_eoi
            [] { xen_ack_irq(); }, // eoi
            [] { evtchn_do_upcall(NULL);// handler
    } );

    // The param vector is comprised of the vector number in the low part, and then:
    // - all the rest zeroed if we are requesting an ISA irq
    // - 1 << 56, if we are requesting a PCI INTx irq, and
    // - 2 << 56, if we are requesting a direct callback.
    xhp.value = vector | (2ULL << 56);
    if (hvm_hypercall(HVMOP_set_param, &xhp))
        assert(0);
}

void xen_hvm_init(processor::features_type &features, unsigned base)
{
    if (xen_start_info != nullptr) {
        features.xen_clocksource = true;
        return;
    }

    // Base + 1 would have given us the version number, it is mostly
    // uninteresting for us now
    auto x = processor::cpuid(base + 2);
    processor::wrmsr(x.b, cast_pointer(&hypercall_page));

    struct xen_feature_info info;
    // To fill up the array used by C code
    for (int i = 0; i < XENFEAT_NR_SUBMAPS; i++) {
        info.submap_idx = i;
        if (version_hypercall(XENVER_get_features, &info) < 0)
            assert(0);
        for (int j = 0; j < 32; j++)
            xen_features[j] = !!(info.submap & 1<<j);
    }
    features.xen_clocksource = xen_features[9] & 1;
    features.xen_vector_callback = xen_features[8] & 1;
    if (!features.xen_vector_callback)
        abort("Hypervisor does not support vectored callbacks");

    struct xen_add_to_physmap map;
    map.domid = DOMID_SELF;
    map.idx = 0;
    map.space = 0;
    map.gpfn = cast_pointer(&xen_shared_info) >> 12;

    // 7 => add to physmap
    if (memory_hypercall(XENMEM_add_to_physmap, &map))
        assert(0);

    features.xen_pci = xen_pci_enabled();
    HYPERVISOR_shared_info = reinterpret_cast<shared_info_t *>(&xen_shared_info);
}

extern "C"
void xen_init(struct start_info* si)
{
    xen_start_info = si;
}
}
