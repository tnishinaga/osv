#ifndef CPU_LOCAL_HH_
#define CPU_LOCAL_HH_

// like std::atomic<>, but only safe against interrupts
// NOT SMP-safe.

namespace arch {

template <typename T>
class cpu_local;

template <>
struct cpu_local<unsigned long> {
public:
    cpu_local() : _v() {}
    cpu_local(unsigned long v) : _v(v) {}
    void increment() { add(1); }
    void decrement() { add(-1ul); }
    void add(unsigned long v) { asm("addq %1, %0" : "+m"(_v) : "ir"(v)); }
    void store(unsigned long v) { asm("movq %1, %0" : "=m"(_v) : "ir"(v)); }
    unsigned long load() { unsigned long v; asm("movq %1, %0" : "=r"(v) : "m"(_v)); return v; }
private:
    unsigned long _v;
};

}

#endif /* CPU_LOCAL_HH_ */
