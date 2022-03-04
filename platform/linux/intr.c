#include <stdio.h>
#include <string.h>
#include <signal.h>
#include <pthread.h>

#include "platform.h"

#include "util.h"
#include "net.h"

// IRQ ハンドラとの対応表
struct irq_entry
{
    struct irq_entry *next; // 次のIRQ構造体へのポインタ
    unsigned int irq;
    int (*handler)(unsigned int irq, void *dev);
    int flags;
    char name[16];
    void *dev;
};

static struct irq_entry *irqs;

static sigset_t sigmask;

static pthread_t tid;
static pthread_barrier_t barrier;

int intr_request_irq(unsigned int irq, int (*handler)(unsigned int irq, void *dev), int flags, const char *name, void *dev)
{
    struct irq_entry *entry;
    debugf("irq=%u, flag=%d, name=%s", irq, flags, name);
    for (entry = irqs; entry; entry = entry->next)
    {
        if (entry->irq == irq)
        {
            if (entry->flags ^ INTR_IRQ_SHARED || flags ^ INTR_IRQ_SHARED)
            {
                errorf("conflicts with already registered IRQs");
                return -1;
            }
        }
    }

    entry = memory_alloc(sizeof(*entry));
    if (!entry)
    {
        errorf("memory_alloc() failure");
        return -1;
    }

    entry->irq = irq;
    entry->handler = handler;
    entry->flags = flags;
    strncpy(entry->name, name, sizeof(entry->name) - 1);
    entry->dev = dev;
    entry->next = irqs;
    irqs = entry;
    sigaddset(&sigmask, irq);
    debugf("registerd: irq=%u, name=%s", irq, name);

    return 0;
}

int intr_raise_irq(unsigned int irq)
{
    return pthread_kill(tid, (int)irq);
}

static void *intr_thread(void *arg)
{
    int terminate = 0, sig, err;
    struct irq_entry *entry;
    debugf("start...");
    pthread_barrier_wait(&barrier);
    while (!terminate)
    {
        err = sigwait(&sigmask, &sig);
        if (err)
        {
            errorf("sigwait() %s", strerror(err));
            break;
        }
        switch (sig)
        {
        case SIGHUP:
            terminate = 1;
            break;

        case SIGUSR1:
            net_softirq_handler();
            break;

        default:
            for (entry = irqs; entry; entry = entry->next)
            {
                if (entry->irq == (unsigned int)sig)
                {
                    debugf("irq=%d, name=%s", entry->irq, entry->name);
                    entry->handler(entry->irq, entry->dev);
                }
            }
            break;
        }
    }
    debugf("terminated");
    return NULL;
}

int intr_run(void)
{
    int err;
    err = pthread_sigmask(SIG_BLOCK, &sigmask, NULL);
    if (err)
    {
        errorf("pthread_sigmask() %s", strerror(err));
        return -1;
    }

    err = pthread_create(&tid, NULL, intr_thread, NULL);
    if (err)
    {
        errorf("pthread_create() %s", strerror(err));
        return -1;
    }
    pthread_barrier_wait(&barrier);
    return 0;
}

void intr_shutdown(void)
{
    if (pthread_equal(tid, pthread_self()) != 0)
    {
        return;
    }

    pthread_kill(tid, SIGHUP);
    pthread_join(tid, NULL);
}

int intr_init(void)
{
    tid = pthread_self();
    pthread_barrier_init(&barrier, NULL, 2);
    sigemptyset(&sigmask);
    sigaddset(&sigmask, SIGHUP);
    sigaddset(&sigmask, SIGUSR1);
    return 0;
}