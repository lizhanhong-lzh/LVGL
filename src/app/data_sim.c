#include "data_sim.h"

#include "lxb_feed.h"

/*
 * 之前这里是纯随机模拟。
 * 现在改为：按老师工程 Lxb 的协议喂“字节流”到 obuf，再解析成指标给 UI。
 *
 * data_sim 在工程里的定位：
 * - 它是 UI 的“数据入口”。UI 不关心协议/串口/LoRa，只调用 data_sim_get()。
 *
 * 后续对接真实硬件时：
 * - 仍然保持 data_sim_get() -> lxb_feed_poll() 这条链路。
 * - 只需要把 lxb_feed.c 里产生字节流的部分替换为“真实读串口后写入 obuf”。
 */

void data_sim_init(void)
{
    lxb_feed_init();
}

plant_metrics_t data_sim_get(void)
{
    lxb_feed_poll();
    return lxb_feed_get_metrics();
}
