#ifndef __PERCPU_WORKER_HH__
#define __PERCPU_WORKER_HH__

#include <list>
#include <functional>
#include <osv/percpu.hh>
#include <sched.hh>

#define PCPU_WORKERITEM(name, lambda) \
    worker_item name __attribute__((section(".percpu_workers"))) { lambda }

class workman;
class worker_item {
public:
    explicit worker_item(std::function<void ()> handler);
    void signal(sched::cpu* cpu, bool wait = false);
    void set_finished(sched::cpu* cpu);
    bool is_finished(sched::cpu* cpu);
private:
    // no clean way around this, we save a per-cpu byte table
    // that signals the worker_item should be invoked for a specified cpu_id
    std::atomic<bool> _have_work[sched::max_cpus];
    std::atomic<sched::thread*> _pcpu_waiters[sched::max_cpus];
public:
    std::function<void ()> _handler;
    friend class workman;
};

// invokes work items in a per cpu manner
class workman {
public:
    void signal(sched::cpu* cpu, worker_item* item, bool wait = false);
private:
    static sched::cpu::notifier _cpu_notifier;
    static void pcpu_init();

    // per CPU thread that invokes worker items
    static percpu<std::atomic<bool>> _duty;
    static percpu<std::atomic<bool>> _ready;
    static percpu<sched::thread *> _work_sheriff;
    static void call_of_duty(void);
};

#endif // !__PERCPU_WORKER_HH__
