#ifndef PER_CPU_COUNTER_HH_
#define PER_CPU_COUNTER_HH_

#include <osv/types.h>
#include <sched.hh>
#include <cpu-local.hh>
#include <vector>
#include <memory>

void per_cpu_counter_init();

class per_cpu_counter {
public:
    explicit per_cpu_counter();
    ~per_cpu_counter();
    void increment();
    ulong read();
private:
    unsigned _index;
    typedef std::vector<arch::cpu_local<ulong>> counter_array;
    typedef std::vector<std::unique_ptr<counter_array>> cpu_array;
    static cpu_array _cpus;
    friend void per_cpu_counter_init();
};

#endif /* PER_CPU_COUNTER_HH_ */
