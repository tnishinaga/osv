#include "tst-hub.hh"
#include "sched.hh"
#include "debug.hh"
#include "lockfree/mutex.hh"

#include <string.h>

class test_wakeup: public unit_tests::vtest {

public:
    // The waker_thread test features several threads that participate in the
    // following silly but simple protocol:
    //   1. A shared value says which thread to wake next.
    //   2. When thread A sees value B in the shared value and manages to
    //      atomically replace it A, it:
    //         1. Wakes up B
    //         2. Goes to sleep.
    //   3. Since the shared value is now A, somebody will wake A later, and
    //      the game goes on.
    static void waker_thread(int id, sched::thread **threads, long len, std::atomic<int> *shared)
     {
         sched::thread *current = sched::thread::current();
         debug(fmt("Starting thread %d\n") % id, false);
         for(int i=0; i<len;){
             sched::wait_guard wait_guard(current);
             int towake = shared->load();
             if (towake!=-1 && std::atomic_compare_exchange_strong(shared, &towake, id)){
                 debug(fmt("%d waking %d\n") % id % towake, false);
                 threads[towake]->wake();
                 debug(fmt("%d finished waking, now sleeping.\n") % id, false);
                 current->tcpu()->schedule(true);
                 debug(fmt("%d woke.\n") % id, false);
                 i++; // count successful wakeups
             }
         }
         debug(fmt("Ended thread %d\n") % id, false);
     }

    void run()
    {
        debug("Running wakeup tests");
        atomic<int> shared(0);
        long len=5000;
        int nthreads=2;
        sched::thread *threads[nthreads];
        for(int i=0; i<nthreads; i++)
             threads[i]= new sched::thread([=,&shared,&threads] {waker_thread(i, threads, len, &shared);});
        for(int i=0; i<nthreads; i++)
            threads[i]->start();
        for(int i=0; i<nthreads; i++)
            threads[i]->join();
        for(int i=0; i<nthreads; i++)
            delete threads[i];
        debug("wakeup tests succeeded");
    }
};
