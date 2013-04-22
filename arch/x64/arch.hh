#ifndef ARCH_HH_
#define ARCH_HH_

#include "processor.hh"
#include "msr.hh"
#include "xen.hh"

// namespace arch - architecture independent interface for architecture
//                  dependent operations (e.g. irq_disable vs. cli)

namespace arch {

inline void irq_disable()
{
    if (!xen::enabled()) {
        processor::cli();
    } else {
        xen::irq_disable();
    }
}

inline void irq_enable()
{
    if (!xen::enabled()) {
        processor::sti();
    } else {
        xen::irq_enable();
    }
}

inline void wait_for_interrupt()
{
    processor::sti_hlt();
}

class irq_flag {
public:
    // need to clear the red zone when playing with the stack. also, can't
    // use "m" constraint as it might be addressed relative to %rsp
    void save() {
        asm volatile("sub $128, %%rsp; pushfq; popq %0; add $128, %%rsp" : "=r"(_rflags));
    }
    void restore() {
        asm volatile("sub $128, %%rsp; pushq %0; popfq; add $128, %%rsp" : : "r"(_rflags));
    }
    bool enabled() const {
        return _rflags & 0x200;
    }
private:
    unsigned long _rflags;
};

inline bool irq_enabled()
{
    irq_flag f;
    f.save();
    return f.enabled();
}

extern bool tls_available() __attribute__((no_instrument_function));

inline bool tls_available()
{
    unsigned a, d;
    asm("rdmsr" : "=a"(a), "=d"(d) : "c"(msr::IA32_FS_BASE));
    // don't call rdmsr, since we don't want function instrumentation
    return a != 0 || d != 0;
}

}

#endif /* ARCH_HH_ */
