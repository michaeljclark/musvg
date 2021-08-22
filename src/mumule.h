#pragma once

#undef NDEBUG
#include <stddef.h>
#include <stdbool.h>
#include <stdatomic.h>
#include <string.h>
#include <threads.h>
#include <assert.h>

#include "mulog.h"

#ifdef __cplusplus
extern "C" {
#endif

struct mu_mule;
typedef struct mu_mule mu_mule;
struct mu_thread;
typedef struct mu_thread mu_thread;

/*
 * mumule interface
 */

typedef void(*mumule_work_fn)(void *arg, size_t thr_idx, size_t item_idx);
static void mule_init(mu_mule *mule, size_t num_threads, mumule_work_fn kernel, void *userdata);
static int mule_launch(mu_mule *mule);
static size_t mule_submit(mu_mule *mule, size_t count);
static int mule_synchronize(mu_mule *mule);
static int mule_destroy(mu_mule *mule);

/*
 * mumule implementation
 */

enum { mumule_max_threads = 8 };

struct mu_thread { mu_mule *mule; size_t idx; thrd_t thread; };

struct mu_mule
{
    _Atomic(bool)    running;
    _Atomic(size_t)  queued;
    _Atomic(size_t)  processing;
    _Atomic(size_t)  processed;
    mtx_t            mutex;
    cnd_t            dispatcher;
    cnd_t            worker;
    void*            userdata;
    mumule_work_fn   kernel;
    size_t           num_threads;
    _Atomic(size_t)  threads_running;
    mu_thread        threads[mumule_max_threads];
};

static void mule_init(mu_mule *mule, size_t num_threads, mumule_work_fn kernel, void *userdata)
{
    memset(mule, 0, sizeof(mu_mule));
    mule->userdata = userdata;
    mule->kernel = kernel;
    mule->num_threads = num_threads;
    mtx_init(&mule->mutex, mtx_plain);
    cnd_init(&mule->worker);
    cnd_init(&mule->dispatcher);
}

static int mule_thread(void *arg)
{
    mu_thread *thread = (mu_thread*)arg;
    mu_mule *mule = thread->mule;
    void* userdata = mule->userdata;
    const size_t thread_idx = thread->idx;
    size_t queued, processing, workitem, processed;

    debugf("mule_thread-%zu: started\n", thread_idx);

    atomic_fetch_add_explicit(&mule->threads_running, 1, __ATOMIC_RELAXED);

    do {
        /* find out how many items still need processing */
        queued = atomic_load_explicit(&mule->queued, __ATOMIC_ACQUIRE);
        processing = atomic_load_explicit(&mule->processing, __ATOMIC_ACQUIRE);

        /* sleep on dispatcher condition if there is no work */
        if (processing == queued) {
            debugf("mule_thread-%zu: sleeping\n", thread_idx);

            cnd_signal(&mule->dispatcher);
            mtx_lock(&mule->mutex);

            /* exit if asked to stop */
            if (!atomic_load(&mule->running)) {
                mtx_unlock(&mule->mutex);
                break;
            }

            /* wait for work */
            cnd_wait(&mule->worker, &mule->mutex);
            mtx_unlock(&mule->mutex);

            debugf("mule_thread-%zu: woke\n", thread_idx);
            continue;
        }

        /* dequeue work-item using compare-and-swap */
        workitem = processing + 1;
        if (!atomic_compare_exchange_weak(&mule->processing, &processing,
            workitem)) continue;
        (mule->kernel)(userdata, thread_idx, workitem);
        processed = atomic_fetch_add_explicit(&mule->processed, 1, __ATOMIC_SEQ_CST);

    } while ((processed + 1 < queued));

    atomic_fetch_add_explicit(&mule->threads_running, -1, __ATOMIC_RELAXED);
    cnd_signal(&mule->dispatcher);

    debugf("mule_thread-%zu: exiting\n", thread_idx);

    return 0;
}

static int mule_launch(mu_mule *mule)
{
    debugf("mule_launch: starting threads\n");

    for (size_t idx = 0; idx < mule->num_threads; idx++) {
        mule->threads[idx].mule = mule;
        mule->threads[idx].idx = idx;
    }
    atomic_store(&mule->running, 1);
    atomic_thread_fence(__ATOMIC_SEQ_CST);

    for (size_t i = 0; i < mule->num_threads; i++) {
        assert(!thrd_create(&mule->threads[i].thread, mule_thread, &mule->threads[i]));
    }
    return 0;
}

static size_t mule_submit(mu_mule *mule, size_t count)
{
    size_t idx = atomic_fetch_add(&mule->queued, count);
    cnd_broadcast(&mule->worker);
    return idx;
}

static int mule_synchronize(mu_mule *mule)
{
    debugf("mule_synchronize: quench\n");

    mtx_lock(&mule->mutex);

    /* wait for queue to quench */
    size_t queued, processed;
    for (;;) {
        queued = atomic_load_explicit(&mule->queued, __ATOMIC_ACQUIRE);
        processed = atomic_load_explicit(&mule->processed, __ATOMIC_ACQUIRE);
        if (processed < queued) {
            cnd_wait(&mule->dispatcher, &mule->mutex);
        } else {
            break;
        }
    };

    /* shutdown workers */
    debugf("mule_synchronize: stopping\n");
    atomic_store_explicit(&mule->running, 0, __ATOMIC_RELEASE);
    mtx_unlock(&mule->mutex);
    cnd_broadcast(&mule->worker);

    /* join workers */
    for (size_t i = 0; i < mule->num_threads; i++) {
        int res;
        assert(!thrd_join(mule->threads[i].thread, &res));
    }
    return 0;
}

static int mule_destroy(mu_mule *mule)
{
    cnd_destroy(&mule->worker);
    cnd_destroy(&mule->dispatcher);
    return -1;
}

#ifdef __cplusplus
}
#endif
