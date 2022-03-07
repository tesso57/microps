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
    struct irq_entry *next;                      // 次のIRQ構造体へのポインタ
    unsigned int irq;                            // irq番号
    int (*handler)(unsigned int irq, void *dev); // 割り込みハンドラ。割り込んだ際に行うこと
    int flags;                                   // フラグ
    char name[16];                               // デバッグで認識する用
    void *dev;                                   // 割り込みの発生源になるデバイス　(※ネットデバイス以外でもOK)
};

// irqのリスト
static struct irq_entry *irqs;
// シグナル集合。捕捉したいシグナルのみ登録。ブロックしたいシグナルの集合
static sigset_t sigmask;

// 割り込み処理のスレッドID
static pthread_t tid;
static pthread_barrier_t barrier;

// irqの登録
int intr_request_irq(unsigned int irq, int (*handler)(unsigned int irq, void *dev), int flags, const char *name, void *dev)
{
    struct irq_entry *entry;

    debugf("irq=%u, flag=%d, name=%s", irq, flags, name);
    // irqが被っていないかを見る。
    for (entry = irqs; entry; entry = entry->next)
    {
        if (entry->irq == irq)
        {
            // どちらもIRQの共有を許可しているかを確認
            if (entry->flags ^ INTR_IRQ_SHARED || flags ^ INTR_IRQ_SHARED)
            {
                errorf("conflicts with already registered IRQs");
                return -1;
            }
        }
    }

    //メモリ確保
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

    // IRQリストに追加
    entry->next = irqs;
    irqs = entry;

    // シグナルマスクに追加
    sigaddset(&sigmask, irq);
    debugf("registerd: irq=%u, name=%s", irq, name);

    return 0;
}

int intr_raise_irq(unsigned int irq)
{
    // 割り込み処理スレッドへシグナルを送信
    return pthread_kill(tid, (int)irq);
}

//　割り込みスレッドのエントリポイント
static void *intr_thread(void *arg)
{
    /*
     * 非同期実行されるシグナルハンドラでは実行できる処理が大きく制限されるため、
     * 割り込み処理用の別スレッドを起動して、シグナルの発生を待ち受ける。
     */

    int terminate = 0, sig, err;
    struct irq_entry *entry;

    debugf("start...");
    //メインスレッドと同期
    pthread_barrier_wait(&barrier);
    //割り込みが発生するまで待つ
    while (!terminate)
    {
        // シグナル一覧
        // https://atmarkit.itmedia.co.jp/ait/articles/1708/04/news015.html
        // https://www.xmisao.com/2013/11/10/linux-kill-signals.html
        err = sigwait(&sigmask, &sig); // 非同期にブロックされたシグナルを待つ
        if (err)
        {
            errorf("sigwait() %s", strerror(err));
            break;
        }
        switch (sig)
        {
        //制御端末の切断
        case SIGHUP:
            terminate = 1;
            break;
        // ユーザー定義のシグナル
        case SIGUSR1:
            net_softirq_handler();
            break;
        // それ以外
        default:
            // IRQリストを巡回する
            // Linuxのリアルタイムシグナル
            for (entry = irqs; entry; entry = entry->next)
            {
                // IRQ番号と一致するやつのハンドラを呼ぶ
                if (entry->irq == (unsigned int)sig)
                {
                    debugf("irq=%d, name=%s", entry->irq, entry->name);
                    // ハンドラを呼び出す
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
    // シグナルマスクの設定を変更。ブロックしたいシグナル一覧をここで登録
    err = pthread_sigmask(SIG_BLOCK, &sigmask, NULL);
    if (err)
    {
        errorf("pthread_sigmask() %s", strerror(err));
        return -1;
    }

    // 割り込みスレッドを起動
    err = pthread_create(&tid, NULL, intr_thread, NULL);
    if (err)
    {
        errorf("pthread_create() %s", strerror(err));
        return -1;
    }

    //スレッドが動き出すまで待つ
    pthread_barrier_wait(&barrier);
    return 0;
}

void intr_shutdown(void)
{
    // 割り込みスレッドが起動中かどうかを確認
    if (pthread_equal(tid, pthread_self()) != 0)
    {
        return;
    }

    pthread_kill(tid, SIGHUP); // SIGHUPを送信して、割り込みスレッドを停止させる
    pthread_join(tid, NULL);   // 割り込みスレッドの終了を待つ
}

int intr_init(void)
{
    tid = pthread_self();                    // tidを自分のスレッドの番号で初期化
    pthread_barrier_init(&barrier, NULL, 2); // スレッドの数を初期化
    sigemptyset(&sigmask);                   // シグナル集合を空にする
    sigaddset(&sigmask, SIGHUP);             // シグナル集合にSIGHUPをついか
    sigaddset(&sigmask, SIGUSR1);            // シグナル集合にSIGUSR1を追加
    return 0;
}