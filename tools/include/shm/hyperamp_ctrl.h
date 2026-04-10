#ifndef _HYPERAMP_CTRL_H_
#define _HYPERAMP_CTRL_H_

#include <stdint.h>

/**
 * HyperAMP MMIO Control Region
 * 
 * 位于 msg 区域头部（64 字节），用于 MMIO 触发中断
 * 物理地址：0x7e400000（从 shm_config.json 中的 linux-2-npucore-msg 获取）
 */

#define HYPERAMP_CTRL_IPI_TRIGGER_OFFSET  0x00  // 触发中断的偏移
#define HYPERAMP_CTRL_TARGET_ZONE_OFFSET  0x04  // 目标 Zone ID
#define HYPERAMP_CTRL_SERVICE_ID_OFFSET   0x08  // 服务 ID

#define HYPERAMP_CTRL_SIZE  64  // 控制区域大小（一个缓存行）

/**
 * 控制区域结构体（64 字节）
 * 
 * 使用方式（优化版本）：
 * 1. Root Linux 将 target_zone_id 和 service_id 打包成一个 32 位值
 *    value = (target_zone_id << 16) | (service_id & 0xFFFF)
 * 2. Root Linux 写入 ipi_trigger = value（触发 MMIO trap）
 * 3. Hypervisor 捕获写操作，从 mmio.value 解包参数
 * 4. Hypervisor 调用 set_ispender(irq) 直接注入中断
 * 
 * 优点：单次写入，不需要读取物理内存，支持任意 MMIO 地址
 */
struct HyperAMPCtrl {
    volatile uint32_t ipi_trigger;    // offset 0x00: 写入 (zone_id<<16 | service_id) 触发中断
    uint32_t reserved[15];            // offset 0x04: 保留（对齐到 64 字节）
} __attribute__((packed, aligned(64)));

// 控制区域物理地址（从 phytium-pi/configs/shm_config.json 读取）
// 使用 Non-Root 消息队列区域（0xde410000）头部 64 字节作为 MMIO 控制区
// 原因：
// 1. Root 消息队列（0xde400000）访问频繁，不适合 MMIO（会导致大量 trap）
// 2. Non-Root 队列（0xde410000）Root 仅在初始化时读一次，改动影响最小
// 3. 控制区 64B，剩余 3968B 仍可用于消息队列（只需偏移 64B）
#define HYPERAMP_CTRL_PA           0x6e410000UL  // MMIO 控制区物理地址
#define HYPERAMP_CTRL_IPA_ROOT     0xde400000UL  // Root Linux IPA（保留，未使用）
#define HYPERAMP_CTRL_IPA_NONROOT  0xde410000UL  // Non-Root IPA

// Non-Root 消息队列数据区偏移（跳过 64B 控制区）
#define HYPERAMP_NONROOT_MSGQ_OFFSET  64

#endif // _HYPERAMP_CTRL_H_
