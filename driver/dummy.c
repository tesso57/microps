#include <stdio.h>
#include <stddef.h>
#include <stdint.h>

#include "platform.h"

#include "util.h"
#include "net.h"

#define DUMMY_MTU UINT16_MAX /* maximum size of IP datagram */

#define DUMMY_IRQ INTR_IRQ_BASE // ダミーデバイスが利用するIRQ番号

//ダミーデバイス用のtansmit 関数
static int dummy_transmit(struct net_device *dev, uint16_t type, const uint8_t *data, size_t len, const void *dst)
{
    // データをダンプして廃棄
    debugf("dev=%s, type=0x%04x, len=%zu", dev->name, type, len);
    debugdump(data, len);
    intr_raise_irq(DUMMY_IRQ); // テスト用に割り込みを行う
    return 0;
}

static int dummy_isr(unsigned int irq, void *id)
{
    // 割り込んだ時に呼ばれる関数
    debugf("irq=%u, dev=%s", irq, ((struct net_device *)id)->name);
    return 0;
}

// memo 初期化する際にメンバーを指定することができる。
static struct net_device_ops dummy_ops = {
    .transmit = dummy_transmit,
};

// ダミーデバイスの初期化
struct net_device *dummy_init(void)
{

    struct net_device *dev;
    dev = net_device_alloc(); // デバイスのメモリ確保
    if (!dev)
    {
        errorf("net_device_alloc() failure");
        return NULL;
    }
    // デバイスの設定
    dev->type = NET_DEVICE_TYPE_DUMMY;
    dev->mtu = DUMMY_MTU;
    dev->hlen = 0;
    dev->alen = 0;
    dev->ops = &dummy_ops;

    // デバイスの登録
    if (net_device_register(dev) == -1)
    {
        errorf("net_device_register() failure");
        return NULL;
    }
    // 割り込みハンドラとして dummiy_isrを登録
    intr_request_irq(DUMMY_IRQ, dummy_isr, INTR_IRQ_SHARED, dev->name, dev);
    debugf("initialized, dev=%s", dev->name);
    return dev;
}