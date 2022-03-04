#include <stdio.h>
#include <signal.h>
#include <unistd.h>

#include "util.h"
#include "net.h"

#include "driver/dummy.h"

#include "test.h"

static volatile sig_atomic_t terminate;

static void on_signal(int s)
{
    (void)s;
    terminate = 1;
}

int main(int argc, char *argv[])
{
    struct net_device *dev;
    signal(SIGINT, on_signal); //シグナルに対する処理。　SIGINTはユーザー操作による割り込み https://programming-place.net/ppp/contents/c/appendix/reference/signal.html

    // プロトコルスタックの初期化
    if (net_init() == -1)
    {
        errorf("net_init() failure");
        return -1;
    }
    // ダミーデバイスの作成
    dev = dummy_init();
    if (!dev)
    {
        errorf("dummy_init() failure");
        return -1;
    }
    // プロトコルスタックの開始
    if (net_run() == -1)
    {
        errorf("net_run() failure");
        return -1;
    }
    while (!terminate)
    {
        // ダミーデバイスに対して送信
        if (net_device_output(dev, 0x0800, test_data, sizeof(test_data), NULL) == -1)
        {
            errorf("net_device_output() failure");
            break;
        }
        sleep(1);
    }
    // プロトコルスタックの終了
    net_shutdown();
    return 0;
}