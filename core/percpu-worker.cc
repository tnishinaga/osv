#include <sched.hh>
#include <osv/trace.hh>
#include <osv/percpu.hh>

#include <osv/percpu-worker.hh>

TRACEPOINT(trace_pcpu_worker_started, "");
TRACEPOINT(trace_pcpu_worker_invoke, "worker_item=%p", worker_item*);
TRACEPOINT(trace_pcpu_worker_working, "num_items=%d", size_t);

sched::cpu::notifier workman::_cpu_notifier(workman::pcpu_init);

PERCPU(std::atomic<bool>, workman::_duty);
PERCPU(sched::thread*, workman::_work_sheriff);

extern char _percpu_workers_start[];
extern char _percpu_workers_end[];

workman _workman;

worker_item::worker_item(std::function<void ()> handler)
{
    _handler = handler;
    for (int i=0; i<64; i++) {
        _have_work[i].store(false, std::memory_order_relaxed);
    }
}

void worker_item::signal(sched::cpu* cpu)
{
    _have_work[cpu->id].store(true, std::memory_order_release);
    _workman.signal(cpu);
}

void workman::signal(sched::cpu* cpu)
{
    //
    // let the sheriff know that he have to do what he have to do.
    // we simply set _duty=true and wake the sheriff
    //
    // when we signal a worker_item, we set 2 variables to true, the per
    // worker_item's per-cpu _have_work variable and the global _duty variable
    // of the cpu's sheriff we are signaling.
    //
    // why use std::atomic with release->acquire?
    //
    // we want the sheriff to see _duty=true only after _have_work=true.
    // in case duty=true will be seen before _have_work=true, we may miss
    // it in the sheriff thread.
    //
    (*(_duty.for_cpu(cpu))).store(true, std::memory_order_release);
    (*_work_sheriff.for_cpu(cpu))->wake();
}

void workman::call_of_duty(void)
{
    trace_pcpu_worker_started();

    while (true) {
        // Wait for duty
        sched::thread::wait_until([] {
            return ((*_duty).load(std::memory_order_acquire) == true);
        });

        (*_duty).store(false, std::memory_order_release);

        unsigned cpu_id = sched::cpu::current()->id;

        // number of work items
        size_t num_work_items =
            (_percpu_workers_end-_percpu_workers_start) / sizeof(worker_item);
        trace_pcpu_worker_working(num_work_items);

        // FIXME: we loop on the list so this is O(N), if the amount of PCPU
        // workers grow above 10-20 than maybe it's better to re-think this
        // design.
        for (unsigned i=0; i < num_work_items; i++) {
            worker_item* it = reinterpret_cast<worker_item*>
                (_percpu_workers_start + i * sizeof(worker_item));

            // if the worker_item is signaled on our cpu, we will make sure
            // the handler does it's work.
            if (it->_have_work[cpu_id].load(std::memory_order_acquire)) {
                (it->_have_work[cpu_id]).store(false, std::memory_order_release);
                trace_pcpu_worker_invoke(it);
                it->_handler();
            }
        }
    }
}

void workman::pcpu_init()
{
    // initialize the sheriff thread
    (*_duty).store(false, std::memory_order_relaxed);
    *_work_sheriff = new sched::thread([] { workman::call_of_duty(); });
    (*_work_sheriff)->start();
}
