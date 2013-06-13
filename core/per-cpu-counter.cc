#include <osv/per-cpu-counter.hh>
#include <osv/mutex.h>
#include <debug.hh>

per_cpu_counter::cpu_array per_cpu_counter::_cpus;

namespace {

static std::vector<bool> used_indices(1000);   // FIXME: allow auto-expand later
mutex mtx;

unsigned allocate_index()
{
    std::lock_guard<mutex> guard{mtx};
    auto i = std::find(used_indices.begin(), used_indices.end(), false);
    if (i == used_indices.end()) {
        abort("out of per-cpu counters");
    }
    *i = true;
    return i - used_indices.begin();
}

void free_index(unsigned index)
{
    std::lock_guard<mutex> guard{mtx};
    assert(used_indices[index]);
    used_indices[index] = false;
}

}

per_cpu_counter::per_cpu_counter()
    : _index(allocate_index())
{
    for (auto& cpu : _cpus) {
        (*cpu)[_index].store(0);
    }
}

per_cpu_counter::~per_cpu_counter()
{
    free_index(_index);
}

void per_cpu_counter::increment()
{
    sched::preempt_disable();
    (*_cpus[sched::cpu::current()->id])[_index].increment();
    sched::preempt_enable();
}

ulong per_cpu_counter::read()
{
    ulong sum = 0;
    for (auto& cpu : _cpus) {
        sum += (*cpu)[_index].load();
    }
    return sum;
}

void per_cpu_counter_init()
{

}
