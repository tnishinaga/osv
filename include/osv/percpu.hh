#ifndef PERCPU_HH_
#define PERCPU_HH_

#include <sched.hh>

extern char _percpu_start[];

std::vector<void*> percpu_base{64};  // FIXME: move to sched::cpu

template <typename T>
class percpu {
public:
    constexpr percpu() {}
    T* operator->() {
        return for_cpu(sched::cpu::current());
    }
    T& operator*() {
        return *addr(sched::cpu::current());
    }
    T* for_cpu(sched::cpu* cpu) {
        return addr(cpu);
    }
private:
    T *addr(sched::cpu* cpu) {
        void* base = cpu->percpu_base;
        size_t offset = reinterpret_cast<char*>(&_var) - _percpu_start;
        return reinterpret_cast<T*>(base + offset);
    }
private:
    static T _var __attribute__((section(".percpu")));
};

template <typename T>
T percpu<T>::_var;

void percpu_init(unsigned cpu);

#endif /* PERCPU_HH_ */
