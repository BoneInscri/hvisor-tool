/**
 * @file hyperamp_linux_shm.c
 * @brief HyperAMP Linux 端共享内存队列实现 - 兼容 HighSpeedCProxy
 * 
 * 本文件提供 Linux 端的共享内存队列操作实现，包括：
 * - 物理内存映射 (/dev/mem)
 * - 双向队列操作 (Linux ↔ seL4)
 * - 与 seL4 微内核的消息通信
 */

#define _POSIX_C_SOURCE 199309L
#define _DEFAULT_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <time.h>
#include <errno.h>

#include "shm/hyperamp_shm_queue.h"
#include "cJSON.h"

/* ==================== 平台检测和 Cache 操作宏 ==================== */

#if defined(__aarch64__) || defined(__arm64__)
    /* ARM64 平台：使用内存屏障代替硬件缓存指令（用户空间无法执行特权指令） */
    #define CACHE_INVALIDATE(addr) do { \
        __asm__ volatile("dmb sy" ::: "memory"); \
    } while(0)
    
    #define CACHE_FLUSH(addr) do { \
        __asm__ volatile("dmb sy" ::: "memory"); \
    } while(0)
    
#elif defined(__x86_64__) || defined(__i386__)
    /* x86/x86_64 平台：使用内存屏障（Cache 由硬件自动维护一致性） */
    #define CACHE_INVALIDATE(addr) do { \
        __asm__ volatile("mfence" ::: "memory"); \
    } while(0)
    
    #define CACHE_FLUSH(addr) do { \
        __asm__ volatile("mfence" ::: "memory"); \
    } while(0)

#elif defined(__loongarch__) || defined(__loongarch64) || defined(LOONGARCH64)
    /* LoongArch 平台：使用 dbar 指令作为内存屏障 */
    #define CACHE_INVALIDATE(addr) do { \
        __asm__ volatile("dbar 0" ::: "memory"); \
    } while(0)
    
    #define CACHE_FLUSH(addr) do { \
        __asm__ volatile("dbar 0" ::: "memory"); \
    } while(0)

#else
    /* 其他平台：使用编译器内存屏障 */
    #define CACHE_INVALIDATE(addr) __sync_synchronize()
    #define CACHE_FLUSH(addr) __sync_synchronize()
    #warning "Unknown architecture, using compiler memory barrier"
#endif

/* ==================== 配置定义 ==================== */

/* 共享内存物理地址 - HyperAMP 3通道布局 (LoongArch, 页大小 16KB = 0x4000)
 *
 * CH0: TX=0xC0000000(16KB), RX=0xC0004000(16KB), Data=0xC0008000(2MB-32KB)  → 占 2MB
 * CH1: TX=0xC0200000(16KB), RX=0xC0204000(16KB), Data=0xC0208000(1MB-32KB)  → 占 1MB
 * CH2: TX=0xC0300000(16KB), RX=0xC0304000(16KB), Data=0xC0308000(1MB-32KB)  → 占 1MB
 * 总占用: 0xC0000000 ~ 0xC0400000 = 4MB
 */
#define SHM_CH0_TX_PADDR            0xc0000000UL
#define SHM_CH0_RX_PADDR            0xc0004000UL
#define SHM_CH0_DATA_PADDR          0xc0008000UL

#define SHM_CH1_TX_PADDR            0xc0200000UL
#define SHM_CH1_RX_PADDR            0xc0204000UL
#define SHM_CH1_DATA_PADDR          0xc0208000UL

#define SHM_CH2_TX_PADDR            0xc0300000UL
#define SHM_CH2_RX_PADDR            0xc0304000UL
#define SHM_CH2_DATA_PADDR          0xc0308000UL

/* 兼容旧代码 */
#define SHM_START_PADDR             SHM_CH0_TX_PADDR
#define SHM_CH1_PADDR               SHM_CH1_TX_PADDR
#define SHM_CH2_PADDR               SHM_CH2_TX_PADDR

#define SHM_QUEUE_SIZE              (4 * 4096)                           // 16KB
#define SHM_CH0_DATA_SIZE           (2 * 1024 * 1024 - SHM_QUEUE_SIZE * 2)  // 2MB-32KB
#define SHM_CH1_DATA_SIZE           (1 * 1024 * 1024 - SHM_QUEUE_SIZE * 2)  // 1MB-32KB
#define SHM_CH2_DATA_SIZE           (1 * 1024 * 1024 - SHM_QUEUE_SIZE * 2)  // 1MB-32KB

#define SHM_DATA_SIZE               SHM_CH0_DATA_SIZE  // 兼容旧代码

/* 队列配置 */
#define DEFAULT_QUEUE_CAPACITY      256
#define DEFAULT_BLOCK_SIZE          4096  // 与 HighSpeedCProxy 的 HSNET_MEM_BLOCK_SIZE 一致

/* Zone ID */
#define ZONE_ID_LINUX               0
#define ZONE_ID_SEL4                1

/* ==================== 全局状态 ==================== */

#define HYPERAMP_NUM_CHANNELS   3   /* ch1, ch2, ch3 */
#define HYPERAMP_MAX_DATA_SEGS  8   /* 每 channel 最多 8 段 data region */

/* 单段 data region 的映射信息 */
typedef struct {
    uint64_t      phys_addr;   /* zone0 HPA（Linux 侧物理地址）*/
    size_t        phys_size;
    volatile void *map_base;   /* mmap 返回的原始地址 */
    size_t         map_size;
    volatile void *virt_addr;  /* 页对齐后的虚拟地址 */
} HyperampDataSeg;

typedef struct {
    /* TX queue（Linux → seL4） */
    volatile void *tx_map_base;
    size_t         tx_map_size;
    uint64_t       tx_phys_addr;
    size_t         tx_phys_size;
    volatile HyperampShmQueue *tx_queue;

    /* RX queue（seL4 → Linux） */
    volatile void *rx_map_base;
    size_t         rx_map_size;
    uint64_t       rx_phys_addr;
    size_t         rx_phys_size;
    volatile HyperampShmQueue *rx_queue;

    /* Data region（多段分散映射） */
    HyperampDataSeg data_segs[HYPERAMP_MAX_DATA_SEGS];
    int             data_seg_cnt;
    size_t          data_total_size;  /* 所有段的总字节数 */

    /* 兼容旧代码：指向第一段 data 的虚拟地址 */
    volatile void  *data_region;
    /* 兼容旧代码：单段物理地址（第一段） */
    uint64_t        data_phys_addr;
    size_t          data_phys_size;
} HyperampChannel;

typedef struct {
    int fd_mem;                              // /dev/hvisor 文件描述符

    HyperampChannel ch[HYPERAMP_NUM_CHANNELS]; // ch[0]=ch1, ch[1]=ch2, ch[2]=ch3

    /* 当前活跃 channel（默认 0）*/
    int active_ch;

    /* 兼容旧代码：指向 active_ch 的字段 */
    volatile void *shm_base;
    size_t         shm_size;
    uint64_t       phys_addr;

    volatile HyperampShmQueue *tx_queue;
    volatile HyperampShmQueue *rx_queue;
    volatile void             *data_region;

    int initialized;

    uint32_t tx_count;
    uint32_t rx_count;
    uint32_t tx_errors;
    uint32_t rx_errors;
} HyperampLinuxContext;

static HyperampLinuxContext g_ctx = {0};


/* ==================== 私有函数 ==================== */

/**
 * @brief 对单个物理区域执行 mmap，返回用户空间虚拟地址
 */
static volatile void *map_one_region(int fd, uint64_t phys_addr, size_t size,
                                     volatile void **out_map_base, size_t *out_map_size)
{
    size_t page_size = sysconf(_SC_PAGESIZE);
    off_t page_offset = phys_addr & (page_size - 1);
    size_t map_size = size + page_offset;

    void *mapped = mmap(NULL, map_size,
                        PROT_READ | PROT_WRITE,
                        MAP_SHARED,
                        fd,
                        phys_addr);
    if (mapped == MAP_FAILED) {
        perror("[HyperAMP] mmap failed");
        return NULL;
    }

    *out_map_base = (volatile void *)mapped;
    *out_map_size = map_size;
    return (volatile void *)((char *)mapped + page_offset);
}

/**
 * @brief 映射全部 3 个 channel 的物理区域到用户空间（支持多段 data）
 */
static int map_physical_memory(uint64_t phys_addr, size_t size)
{
    (void)phys_addr; (void)size;

    g_ctx.fd_mem = open("/dev/hvisor", O_RDWR | O_SYNC);
    if (g_ctx.fd_mem < 0) {
        perror("[HyperAMP] Failed to open /dev/hvisor");
        return HYPERAMP_ERROR;
    }

    for (int c = 0; c < HYPERAMP_NUM_CHANNELS; c++) {
        HyperampChannel *ch = &g_ctx.ch[c];

        /* TX queue */
        ch->tx_queue = (volatile HyperampShmQueue *)map_one_region(
            g_ctx.fd_mem, ch->tx_phys_addr, ch->tx_phys_size,
            &ch->tx_map_base, &ch->tx_map_size);
        if (!ch->tx_queue) goto err;

        /* RX queue */
        ch->rx_queue = (volatile HyperampShmQueue *)map_one_region(
            g_ctx.fd_mem, ch->rx_phys_addr, ch->rx_phys_size,
            &ch->rx_map_base, &ch->rx_map_size);
        if (!ch->rx_queue) goto err;

        /* Data region：逐段 mmap */
        ch->data_total_size = 0;
        for (int s = 0; s < ch->data_seg_cnt; s++) {
            HyperampDataSeg *seg = &ch->data_segs[s];
            seg->virt_addr = map_one_region(
                g_ctx.fd_mem, seg->phys_addr, seg->phys_size,
                &seg->map_base, &seg->map_size);
            if (!seg->virt_addr) goto err;
            ch->data_total_size += seg->phys_size;
        }
        /* 兼容旧代码：指向第一段 */
        ch->data_region    = (ch->data_seg_cnt > 0) ? ch->data_segs[0].virt_addr : NULL;
        ch->data_phys_addr = (ch->data_seg_cnt > 0) ? ch->data_segs[0].phys_addr : 0;
        ch->data_phys_size = ch->data_total_size;

        printf("[HyperAMP] ch%d: TX=%p(0x%lx) RX=%p(0x%lx) Data=%p(%zu bytes, %d segs)\n",
               c + 1,
               (void *)ch->tx_queue, ch->tx_phys_addr,
               (void *)ch->rx_queue, ch->rx_phys_addr,
               (void *)ch->data_region, ch->data_total_size, ch->data_seg_cnt);
    }

    /* 兼容旧代码：指向 active_ch */
    {
        HyperampChannel *ach = &g_ctx.ch[g_ctx.active_ch];
        g_ctx.tx_queue    = ach->tx_queue;
        g_ctx.rx_queue    = ach->rx_queue;
        g_ctx.data_region = ach->data_region;
        g_ctx.shm_base    = (volatile void *)ach->tx_queue;
        g_ctx.shm_size    = ach->tx_phys_size;
        g_ctx.phys_addr   = ach->tx_phys_addr;
    }

    return HYPERAMP_OK;

err:
    for (int c = 0; c < HYPERAMP_NUM_CHANNELS; c++) {
        HyperampChannel *ch = &g_ctx.ch[c];
        if (ch->tx_map_base) {
            munmap((void *)ch->tx_map_base, ch->tx_map_size);
            ch->tx_map_base = NULL; ch->tx_map_size = 0; ch->tx_queue = NULL;
        }
        if (ch->rx_map_base) {
            munmap((void *)ch->rx_map_base, ch->rx_map_size);
            ch->rx_map_base = NULL; ch->rx_map_size = 0; ch->rx_queue = NULL;
        }
        for (int s = 0; s < ch->data_seg_cnt; s++) {
            HyperampDataSeg *seg = &ch->data_segs[s];
            if (seg->map_base) {
                munmap((void *)seg->map_base, seg->map_size);
                seg->map_base = NULL; seg->map_size = 0; seg->virt_addr = NULL;
            }
        }
        ch->data_region = NULL;
    }
    close(g_ctx.fd_mem);
    g_ctx.fd_mem = -1;
    return HYPERAMP_ERROR;
}

/**
 * @brief 取消全部 3 个 channel 的内存映射
 */
static void unmap_physical_memory(void)
{
    for (int c = 0; c < HYPERAMP_NUM_CHANNELS; c++) {
        HyperampChannel *ch = &g_ctx.ch[c];
        if (ch->tx_map_base) {
            munmap((void *)ch->tx_map_base, ch->tx_map_size);
            ch->tx_map_base = NULL; ch->tx_map_size = 0; ch->tx_queue = NULL;
        }
        if (ch->rx_map_base) {
            munmap((void *)ch->rx_map_base, ch->rx_map_size);
            ch->rx_map_base = NULL; ch->rx_map_size = 0; ch->rx_queue = NULL;
        }
        for (int s = 0; s < ch->data_seg_cnt; s++) {
            HyperampDataSeg *seg = &ch->data_segs[s];
            if (seg->map_base) {
                munmap((void *)seg->map_base, seg->map_size);
                seg->map_base = NULL; seg->map_size = 0; seg->virt_addr = NULL;
            }
        }
        ch->data_region = NULL; ch->data_phys_size = 0; ch->data_total_size = 0;
    }
    g_ctx.tx_queue = NULL; g_ctx.rx_queue = NULL;
    g_ctx.data_region = NULL; g_ctx.shm_base = NULL;
    g_ctx.shm_size = 0; g_ctx.phys_addr = 0;

    if (g_ctx.fd_mem >= 0) {
        close(g_ctx.fd_mem);
        g_ctx.fd_mem = -1;
    }
}

/* ==================== 公共 API ==================== */

/**
 * @brief 初始化 HyperAMP Linux 客户端
 * @param phys_addr 共享内存物理地址 (0 使用默认值)
 * @param is_creator 是否为队列创建者 (1=创建并初始化队列, 0=连接已存在的队列)
 * @return HYPERAMP_OK 成功, HYPERAMP_ERROR 失败
 */
int hyperamp_linux_init(uint64_t phys_addr, int is_creator)
{
    if (g_ctx.initialized) {
        printf("[HyperAMP] Already initialized\n");
        return HYPERAMP_OK;
    }
    
    // 使用默认地址 (TX Queue 起始地址)
    if (phys_addr == 0) {
        phys_addr = SHM_START_PADDR;
    }
    
    printf("[HyperAMP] ========================================\n");
    printf("[HyperAMP] Initializing HyperAMP Linux Client\n");
    printf("[HyperAMP] ========================================\n");
    printf("[HyperAMP] Mode: %s\n", is_creator ? "CREATOR" : "CONNECTOR");
    printf("[HyperAMP] Physical address: 0x%lx\n", phys_addr);
    
    // 映射三个独立物理区域
    if (map_physical_memory(0, 0) != HYPERAMP_OK) {
        return HYPERAMP_ERROR;
    }

    printf("[HyperAMP] Memory layout (active ch%d):\n", g_ctx.active_ch + 1);
    printf("[HyperAMP]   TX Queue:    %p (phys: 0x%lx)\n",
           g_ctx.tx_queue,    g_ctx.ch[g_ctx.active_ch].tx_phys_addr);
    printf("[HyperAMP]   RX Queue:    %p (phys: 0x%lx)\n",
           g_ctx.rx_queue,    g_ctx.ch[g_ctx.active_ch].rx_phys_addr);
    printf("[HyperAMP]   Data Region: %p (phys: 0x%lx, size: %zu bytes)\n",
           g_ctx.data_region, g_ctx.ch[g_ctx.active_ch].data_phys_addr,
           g_ctx.ch[g_ctx.active_ch].data_phys_size);
    
    // 初始化队列配置 —— 使用从 JSON 或 fallback 填充的物理地址

    if (is_creator) {
        /* 初始化全部 3 个 channel 的队列 */
        for (int c = 0; c < HYPERAMP_NUM_CHANNELS; c++) {
            HyperampChannel *ch = &g_ctx.ch[c];

            HyperampQueueConfig tx_cfg = {
                .map_mode   = HYPERAMP_MAP_MODE_CONTIGUOUS_BOTH,
                .capacity   = DEFAULT_QUEUE_CAPACITY,
                .block_size = DEFAULT_BLOCK_SIZE,
                .phy_addr   = ch->tx_phys_addr,
                .virt_addr  = (uint64_t)ch->tx_queue,
            };
            HyperampQueueConfig rx_cfg = {
                .map_mode   = HYPERAMP_MAP_MODE_CONTIGUOUS_BOTH,
                .capacity   = DEFAULT_QUEUE_CAPACITY,
                .block_size = DEFAULT_BLOCK_SIZE,
                .phy_addr   = ch->rx_phys_addr,
                .virt_addr  = (uint64_t)ch->rx_queue,
            };

            printf("[HyperAMP] Initializing ch%d TX queue...\n", c + 1);
            if (hyperamp_queue_init(ch->tx_queue, &tx_cfg, 1) != HYPERAMP_OK) {
                printf("[HyperAMP] Failed to init ch%d TX queue\n", c + 1);
                unmap_physical_memory();
                return HYPERAMP_ERROR;
            }
            printf("[HyperAMP] Initializing ch%d RX queue...\n", c + 1);
            if (hyperamp_queue_init(ch->rx_queue, &rx_cfg, 1) != HYPERAMP_OK) {
                printf("[HyperAMP] Failed to init ch%d RX queue\n", c + 1);
                unmap_physical_memory();
                return HYPERAMP_ERROR;
            }
            printf("[HyperAMP] Clearing ch%d data region...\n", c + 1);
            hyperamp_safe_memset(ch->data_region, 0, ch->data_phys_size);
        }
    } else {
        /* CONNECTOR 模式：seL4 已初始化队列，Linux 侧以 is_creator=0 注册自己的虚拟地址，
         * 并对全部 3 个 channel 执行 hyperamp_queue_init(queue, cfg, 0)。 */
        printf("[HyperAMP] Connecting to existing queues...\n");

        for (int c = 0; c < HYPERAMP_NUM_CHANNELS; c++) {
            HyperampChannel *ch = &g_ctx.ch[c];

            /* 失效缓存，确保读到 seL4 写入的最新数据 */
            CACHE_INVALIDATE(ch->tx_queue);
            CACHE_INVALIDATE(ch->rx_queue);

            uint16_t tx_cap = hyperamp_safe_read_u16(ch->tx_queue,
                                  offsetof(HyperampShmQueue, capacity));
            uint16_t rx_cap = hyperamp_safe_read_u16(ch->rx_queue,
                                  offsetof(HyperampShmQueue, capacity));
            printf("[HyperAMP] ch%d: TX capacity=%u, RX capacity=%u\n",
                   c + 1, tx_cap, rx_cap);

            /* 注册 Linux 侧虚拟地址（写入 virt_addr2），不重置队列状态 */
            HyperampQueueConfig tx_cfg = {
                .map_mode   = HYPERAMP_MAP_MODE_CONTIGUOUS_BOTH,
                .capacity   = DEFAULT_QUEUE_CAPACITY,
                .block_size = DEFAULT_BLOCK_SIZE,
                .phy_addr   = ch->tx_phys_addr,
                .virt_addr  = (uint64_t)ch->tx_queue,
            };
            HyperampQueueConfig rx_cfg = {
                .map_mode   = HYPERAMP_MAP_MODE_CONTIGUOUS_BOTH,
                .capacity   = DEFAULT_QUEUE_CAPACITY,
                .block_size = DEFAULT_BLOCK_SIZE,
                .phy_addr   = ch->rx_phys_addr,
                .virt_addr  = (uint64_t)ch->rx_queue,
            };

            if (hyperamp_queue_init(ch->tx_queue, &tx_cfg, 0) != HYPERAMP_OK) {
                printf("[HyperAMP] Failed to connect ch%d TX queue\n", c + 1);
                unmap_physical_memory();
                return HYPERAMP_ERROR;
            }
            if (hyperamp_queue_init(ch->rx_queue, &rx_cfg, 0) != HYPERAMP_OK) {
                printf("[HyperAMP] Failed to connect ch%d RX queue\n", c + 1);
                unmap_physical_memory();
                return HYPERAMP_ERROR;
            }
            printf("[HyperAMP] ch%d connected\n", c + 1);
        }
    }
    
    g_ctx.initialized = 1;
    g_ctx.tx_count = 0;
    g_ctx.rx_count = 0;
    g_ctx.tx_errors = 0;
    g_ctx.rx_errors = 0;
    
    printf("[HyperAMP] Initialization complete!\n");
    
    // 安全读取队列信息（逐字节）
    uint32_t tx_magic = hyperamp_safe_read_u32(g_ctx.tx_queue, offsetof(HyperampShmQueue, magic));
    uint16_t tx_capacity = hyperamp_safe_read_u16(g_ctx.tx_queue, offsetof(HyperampShmQueue, capacity));
    uint16_t tx_block_size = hyperamp_safe_read_u16(g_ctx.tx_queue, offsetof(HyperampShmQueue, block_size));
    
    uint32_t rx_magic = hyperamp_safe_read_u32(g_ctx.rx_queue, offsetof(HyperampShmQueue, magic));
    uint16_t rx_capacity = hyperamp_safe_read_u16(g_ctx.rx_queue, offsetof(HyperampShmQueue, capacity));
    uint16_t rx_block_size = hyperamp_safe_read_u16(g_ctx.rx_queue, offsetof(HyperampShmQueue, block_size));
    
    printf("[HyperAMP] TX Queue: magic=0x%08x, capacity=%u, block_size=%u\n",
           tx_magic, tx_capacity, tx_block_size);
    printf("[HyperAMP] RX Queue: magic=0x%08x, capacity=%u, block_size=%u\n",
           rx_magic, rx_capacity, rx_block_size);
    printf("[HyperAMP] ========================================\n");
    
    return HYPERAMP_OK;
}

/**
 * @brief 关闭 HyperAMP Linux 客户端
 */
void hyperamp_linux_cleanup(void)
{
    if (!g_ctx.initialized) return;
    
    printf("[HyperAMP] Cleaning up...\n");
    printf("[HyperAMP] Statistics:\n");
    printf("[HyperAMP]   TX: %u sent, %u errors\n", g_ctx.tx_count, g_ctx.tx_errors);
    printf("[HyperAMP]   RX: %u received, %u errors\n", g_ctx.rx_count, g_ctx.rx_errors);
    
    unmap_physical_memory();
    
    g_ctx.initialized = 0;
    g_ctx.tx_queue = NULL;
    g_ctx.rx_queue = NULL;
    g_ctx.data_region = NULL;
    
    printf("[HyperAMP] Cleanup complete\n");
}

/**
 * @brief 发送消息到 seL4 (Linux -> seL4)
 * @param msg_type 消息类型 (HyperampMsgType)
 * @param frontend_sess_id 前端会话ID
 * @param backend_sess_id 后端会话ID
 * @param payload 载荷数据
 * @param payload_len 载荷长度
 * @return HYPERAMP_OK 成功, HYPERAMP_ERROR 失败
 */
/**
 * @brief 向指定 channel 发送消息（内部实现）
 */
static int _send_on_ch(int ch_idx,
                       uint8_t msg_type,
                       uint16_t frontend_sess_id,
                       uint16_t backend_sess_id,
                       const void *payload,
                       uint16_t payload_len)
{
    if (!g_ctx.initialized || ch_idx < 0 || ch_idx >= HYPERAMP_NUM_CHANNELS)
        return HYPERAMP_ERROR;

    HyperampChannel *ch = &g_ctx.ch[ch_idx];

    CACHE_INVALIDATE(ch->tx_queue);
    uint16_t cap = hyperamp_safe_read_u16(ch->tx_queue, offsetof(HyperampShmQueue, capacity));
    if (cap == 0) {
        printf("[HyperAMP] ch%d TX not ready\n", ch_idx + 1);
        return HYPERAMP_ERROR;
    }

    if (payload_len > HYPERAMP_MSG_MAX_SIZE) {
        printf("[HyperAMP] ch%d payload too large: %u\n", ch_idx + 1, payload_len);
        return HYPERAMP_ERROR;
    }

    uint8_t msg_buf[HYPERAMP_MSG_HDR_PLUS_MAX_SIZE];
    HyperampMsgHeader *hdr = (HyperampMsgHeader *)msg_buf;
    hdr->version          = 1;
    hdr->proxy_msg_type   = msg_type;
    hdr->frontend_sess_id = frontend_sess_id;
    hdr->backend_sess_id  = backend_sess_id;
    hdr->payload_len      = payload_len;

    if (payload && payload_len > 0) {
        const uint8_t *src = (const uint8_t *)payload;
        uint8_t *dst = msg_buf + sizeof(HyperampMsgHeader);
        for (uint16_t i = 0; i < payload_len; i++) dst[i] = src[i];
    }

    size_t total_len = sizeof(HyperampMsgHeader) + payload_len;
    int ret = hyperamp_queue_enqueue(ch->tx_queue, ZONE_ID_LINUX,
                                     msg_buf, total_len,
                                     ch->data_region);
    if (ret == HYPERAMP_OK) {
        g_ctx.tx_count++;
        printf("[HyperAMP] ch%d TX: type=%u sess=%u/%u len=%u\n",
               ch_idx + 1, msg_type, frontend_sess_id, backend_sess_id, payload_len);
    } else {
        g_ctx.tx_errors++;
        printf("[HyperAMP] ch%d TX failed (queue full?)\n", ch_idx + 1);
    }
    return ret;
}

/**
 * @brief 发送消息到 active channel (兼容旧接口)
 */
int hyperamp_linux_send(uint8_t msg_type,
                        uint16_t frontend_sess_id,
                        uint16_t backend_sess_id,
                        const void *payload,
                        uint16_t payload_len)
{
    return _send_on_ch(g_ctx.active_ch, msg_type, frontend_sess_id,
                       backend_sess_id, payload, payload_len);
}

/**
 * @brief 同时向全部 3 个 channel 广播消息
 * @return HYPERAMP_OK 若至少一个 channel 成功，否则 HYPERAMP_ERROR
 */
int hyperamp_linux_send_all(uint8_t msg_type,
                            uint16_t frontend_sess_id,
                            uint16_t backend_sess_id,
                            const void *payload,
                            uint16_t payload_len)
{
    int ok = 0;
    for (int c = 0; c < HYPERAMP_NUM_CHANNELS; c++) {
        if (_send_on_ch(c, msg_type, frontend_sess_id, backend_sess_id,
                        payload, payload_len) == HYPERAMP_OK)
            ok++;
    }
    return ok > 0 ? HYPERAMP_OK : HYPERAMP_ERROR;
}

/**
 * @brief 从 seL4 接收消息 (seL4 -> Linux)
 * @param hdr 输出：消息头
 * @param payload 输出：载荷缓冲区
 * @param max_payload_len 载荷缓冲区最大长度
 * @param actual_payload_len 输出：实际载荷长度
 * @return HYPERAMP_OK 成功, HYPERAMP_ERROR 失败（无消息或错误）
 */
int hyperamp_linux_recv(HyperampMsgHeader *hdr,
                        void *payload,
                        uint16_t max_payload_len,
                        uint16_t *actual_payload_len)
{
    static int queue_not_ready_printed = 0;  // 只打印一次"队列未就绪"
    static int queue_ready_printed = 0;      // 只打印一次"队列已就绪"
    
    if (!g_ctx.initialized || !hdr) {
        return HYPERAMP_ERROR;
    }
    
    /* 关键：每次接收前都要失效 cache，确保读取到 seL4 的最新数据！ */
    CACHE_INVALIDATE(g_ctx.rx_queue);
    
    // 检查队列是否已被 seL4 初始化（避免访问未初始化队列导致段错误）
    uint16_t rx_capacity = hyperamp_safe_read_u16(g_ctx.rx_queue, offsetof(HyperampShmQueue, capacity));
    
    if (rx_capacity == 0) {
        // 队列未初始化，直接返回（只打印一次）
        if (!queue_not_ready_printed) {
            printf("[HyperAMP] RX Queue not initialized yet (capacity=0), waiting for seL4...\n");
            queue_not_ready_printed = 1;
        }
        return HYPERAMP_ERROR;
    }
    
    // 队列已就绪，打印一次
    if (!queue_ready_printed) {
        // 关键：检测到队列就绪后，再次强制失效 cache，确保读取最新的队列数据
        CACHE_INVALIDATE(g_ctx.rx_queue);
        
        // 重新读取 capacity，确保是正确的值
        rx_capacity = hyperamp_safe_read_u16(g_ctx.rx_queue, offsetof(HyperampShmQueue, capacity));
        
        printf("[HyperAMP] ✓ RX Queue initialized (capacity=%u), ready to receive!\n", rx_capacity);
        
        // 关键：打印前再次失效缓存，确保读取的是最新数据而不是 CPU 缓存中的旧数据
        CACHE_INVALIDATE(g_ctx.rx_queue);
        // 打印队列头部的原始字节（只在首次初始化时）
        volatile uint8_t *rx_bytes = (volatile uint8_t *)g_ctx.rx_queue;
        for (int i = 0; i < 16; i++) {
            printf("%02x ", rx_bytes[i]);
        }
        printf("\n");
        
        queue_ready_printed = 1;
    }
    
    uint8_t msg_buf[HYPERAMP_MSG_HDR_PLUS_MAX_SIZE];
    size_t actual_len = 0;
    // 重要：数据区使用独立的共享内存区域，而不是队列控制块后面

    volatile void *rx_data_base = g_ctx.data_region;  // 共享数据区

    // rx_queue出队，Physical addr: 0x7e000000
    int ret = hyperamp_queue_dequeue(g_ctx.rx_queue, ZONE_ID_LINUX,
                                      msg_buf, sizeof(msg_buf), &actual_len, rx_data_base);
    if (ret != HYPERAMP_OK) {
        return HYPERAMP_ERROR;  // 队列空
    }

    if (actual_len < sizeof(HyperampMsgHeader)) {
        g_ctx.rx_errors++;
        printf("[HyperAMP] RX: invalid message (too short: %zu)\n", actual_len);
        return HYPERAMP_ERROR;
    }

    // 解析消息头，生命周期被限制在花括号内部，避免和下面的dst、src变量冲突
    {
        uint8_t *dst = (uint8_t *)hdr;
        const uint8_t *src = msg_buf;
        for (size_t i = 0; i < sizeof(HyperampMsgHeader); i++) {
            dst[i] = src[i];
        }
    }
   
    // 复制载荷长度
    uint16_t payload_len = hdr->payload_len;
    if (payload_len > max_payload_len) {
        payload_len = max_payload_len;
    }
    
    // 复制载荷
    if (payload && payload_len > 0) {
        uint8_t *dst = (uint8_t *)payload;
        const uint8_t *src = msg_buf + sizeof(HyperampMsgHeader);
        for (uint16_t i = 0; i < payload_len; i++) {
            dst[i] = src[i];
        }
    }
    
    if (actual_payload_len) {
        *actual_payload_len = payload_len;
    }
    
    g_ctx.rx_count++;
    printf("[HyperAMP] RX: type=%u, sess=%u/%u, len=%u (total: %u)\n",
           hdr->proxy_msg_type, hdr->frontend_sess_id, hdr->backend_sess_id,
           hdr->payload_len, g_ctx.rx_count);
    
    return HYPERAMP_OK;
}

/**
 * @brief 发送原始数据消息 (便捷函数)
 */
/**
 * @brief 发送原始数据消息 (便捷函数)
 */
int hyperamp_linux_send_data(uint16_t session_id, const void *data, uint16_t data_len)
{
    return hyperamp_linux_send(HYPERAMP_MSG_TYPE_DATA, session_id, 0, data, data_len);
}

int hyperamp_linux_send_data_all(uint16_t session_id, const void *data, uint16_t data_len)
{
    return hyperamp_linux_send_all(HYPERAMP_MSG_TYPE_DATA, session_id, 0, data, data_len);
}

int hyperamp_linux_call_service(uint16_t service_id, const void *data, uint16_t data_len)
{
    return hyperamp_linux_send(HYPERAMP_MSG_TYPE_SERVICE, service_id, 0, data, data_len);
}

int hyperamp_linux_call_service_all(uint16_t service_id, const void *data, uint16_t data_len)
{
    return hyperamp_linux_send_all(HYPERAMP_MSG_TYPE_SERVICE, service_id, 0, data, data_len);
}

/**
 * @brief 检查是否有待接收的消息
 */
int hyperamp_linux_has_message(void)
{
    if (!g_ctx.initialized) return 0;
    
    // 安全读取 header 和 tail
    uint16_t header = hyperamp_safe_read_u16(g_ctx.rx_queue, offsetof(HyperampShmQueue, header));
    uint16_t tail = hyperamp_safe_read_u16(g_ctx.rx_queue, offsetof(HyperampShmQueue, tail));
    
    return (tail != header);
}

/**
 * @brief 获取队列状态
 */
void hyperamp_linux_get_status(void)
{
    if (!g_ctx.initialized) {
        printf("[HyperAMP] Not initialized\n");
        return;
    }
    
    printf("[HyperAMP] ========== Queue Status ==========\n");
    printf("[HyperAMP] TX Queue[%p] (Linux -> seL4):\n", g_ctx.tx_queue);
    
    // 安全读取 TX 队列状态
    uint16_t tx_header = hyperamp_safe_read_u16(g_ctx.tx_queue, offsetof(HyperampShmQueue, header));
    uint16_t tx_tail = hyperamp_safe_read_u16(g_ctx.tx_queue, offsetof(HyperampShmQueue, tail));
    uint16_t tx_capacity = hyperamp_safe_read_u16(g_ctx.tx_queue, offsetof(HyperampShmQueue, capacity));
    uint32_t tx_enqueue = hyperamp_safe_read_u32(g_ctx.tx_queue, offsetof(HyperampShmQueue, enqueue_count));
    uint32_t tx_dequeue = hyperamp_safe_read_u32(g_ctx.tx_queue, offsetof(HyperampShmQueue, dequeue_count));
    
    printf("[HyperAMP]   header: %u, tail: %u, capacity: %u\n",
           tx_header, tx_tail, tx_capacity);
    
    // 计算队列长度
    uint16_t tx_length = (tx_header >= tx_tail) ? (tx_header - tx_tail) : (tx_capacity - tx_tail + tx_header);
    printf("[HyperAMP]   length: %u, enqueued: %u, dequeued: %u\n",
           tx_length, tx_enqueue, tx_dequeue);
    
    // 读取锁状态
    size_t tx_lock_offset = offsetof(HyperampShmQueue, queue_lock);
    uint32_t tx_lock_value = hyperamp_safe_read_u32(g_ctx.tx_queue, tx_lock_offset + offsetof(HyperampSpinlock, lock_value));
    uint32_t tx_lock_owner = hyperamp_safe_read_u32(g_ctx.tx_queue, tx_lock_offset + offsetof(HyperampSpinlock, owner_zone_id));
    uint32_t tx_lock_count = hyperamp_safe_read_u32(g_ctx.tx_queue, tx_lock_offset + offsetof(HyperampSpinlock, lock_count));
    uint32_t tx_lock_contention = hyperamp_safe_read_u32(g_ctx.tx_queue, tx_lock_offset + offsetof(HyperampSpinlock, contention_count));
    
    printf("[HyperAMP]   lock: value=%u, owner=%u, count=%u, contention=%u\n",
           tx_lock_value, tx_lock_owner, tx_lock_count, tx_lock_contention);
    
    printf("[HyperAMP] RX Queue[%p] (seL4 -> Linux):\n",g_ctx.rx_queue);
    
    // 安全读取 RX 队列状态
    uint16_t rx_header = hyperamp_safe_read_u16(g_ctx.rx_queue, offsetof(HyperampShmQueue, header));
    uint16_t rx_tail = hyperamp_safe_read_u16(g_ctx.rx_queue, offsetof(HyperampShmQueue, tail));
    uint16_t rx_capacity = hyperamp_safe_read_u16(g_ctx.rx_queue, offsetof(HyperampShmQueue, capacity));
    uint32_t rx_enqueue = hyperamp_safe_read_u32(g_ctx.rx_queue, offsetof(HyperampShmQueue, enqueue_count));
    uint32_t rx_dequeue = hyperamp_safe_read_u32(g_ctx.rx_queue, offsetof(HyperampShmQueue, dequeue_count));
    
    printf("[HyperAMP]   header: %u, tail: %u, capacity: %u\n",
           rx_header, rx_tail, rx_capacity);
    
    uint16_t rx_length = (rx_header >= rx_tail) ? (rx_header - rx_tail) : (rx_capacity - rx_tail + rx_header);
    printf("[HyperAMP]   length: %u, enqueued: %u, dequeued: %u\n",
           rx_length, rx_enqueue, rx_dequeue);
    
    // 读取锁状态
    size_t rx_lock_offset = offsetof(HyperampShmQueue, queue_lock);
    uint32_t rx_lock_value = hyperamp_safe_read_u32(g_ctx.rx_queue, rx_lock_offset + offsetof(HyperampSpinlock, lock_value));
    uint32_t rx_lock_owner = hyperamp_safe_read_u32(g_ctx.rx_queue, rx_lock_offset + offsetof(HyperampSpinlock, owner_zone_id));
    uint32_t rx_lock_count = hyperamp_safe_read_u32(g_ctx.rx_queue, rx_lock_offset + offsetof(HyperampSpinlock, lock_count));
    uint32_t rx_lock_contention = hyperamp_safe_read_u32(g_ctx.rx_queue, rx_lock_offset + offsetof(HyperampSpinlock, contention_count));
    
    printf("[HyperAMP]   lock: value=%u, owner=%u, count=%u, contention=%u\n",
           rx_lock_value, rx_lock_owner, rx_lock_count, rx_lock_contention);
    
    printf("[HyperAMP] Local Statistics:\n");
    printf("[HyperAMP]   TX: %u sent, %u errors\n", g_ctx.tx_count, g_ctx.tx_errors);
    printf("[HyperAMP]   RX: %u received, %u errors\n", g_ctx.rx_count, g_ctx.rx_errors);
    printf("[HyperAMP] ====================================\n");
}

/* ==================== 测试主函数 ==================== */

#ifdef HYPERAMP_TEST_MAIN

/*
 * parse_global_addr - 解析 zone_shm.json（shm_regions 格式），
 * 填充 g_ctx.ch[0..2] 的物理地址字段。
 *
 * JSON 格式：
 *   { "shm_regions": [
 *       { "flag": "sel4-tx-queue-ch1",
 *         "zone0_ram_ipa": "0x...",   <- Linux 侧 HPA，用于 mmap
 *         "zonex_ram_ipa": "0x...",   <- seL4 侧 IPA（仅记录，不用于 mmap）
 *         "size": "0x..." }, ... ] }
 *
 * 命名约定（从 seL4 视角）：
 *   sel4-tx-queue-chN  = seL4 发送 / Linux 接收 → ch[N-1].rx_*
 *   sel4-rx-queue-chN  = seL4 接收 / Linux 发送 → ch[N-1].tx_*
 *   sel4-data-region-chN                         → ch[N-1].data_segs[]（多段）
 */
static void parse_global_addr(char *json_path) {
    FILE *fp = fopen(json_path, "r");
    if (!fp) {
        printf("[Error] parse_global_addr: cannot open %s\n", json_path);
        while(1) {}
    }
    fseek(fp, 0, SEEK_END);
    long fsize = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    char *buf = malloc(fsize + 1);
    if (!buf) { fclose(fp); printf("[Error] malloc failed\n"); while(1) {} }
    fread(buf, 1, fsize, fp);
    buf[fsize] = '\0';
    fclose(fp);

    cJSON *root = cJSON_Parse(buf);
    free(buf);
    if (!root) {
        printf("[Error] parse_global_addr: JSON parse failed\n");
        while(1) {}
    }

    cJSON *regions = cJSON_GetObjectItem(root, "shm_regions");
    if (!regions || !cJSON_IsArray(regions)) {
        printf("[Error] parse_global_addr: 'shm_regions' not found\n");
        cJSON_Delete(root);
        while(1) {}
    }

    int found_tx[HYPERAMP_NUM_CHANNELS]   = {0};
    int found_rx[HYPERAMP_NUM_CHANNELS]   = {0};
    int found_data[HYPERAMP_NUM_CHANNELS] = {0};

    int n = cJSON_GetArraySize(regions);
    for (int i = 0; i < n; i++) {
        cJSON *r      = cJSON_GetArrayItem(regions, i);
        cJSON *flag_j = cJSON_GetObjectItem(r, "flag");
        cJSON *z0_j   = cJSON_GetObjectItem(r, "zone0_ram_ipa");
        cJSON *sz_j   = cJSON_GetObjectItem(r, "size");
        if (!flag_j || !z0_j || !sz_j) continue;

        const char *flag = flag_j->valuestring;
        uint64_t paddr = strtoull(z0_j->valuestring, NULL, 16);
        uint64_t sz    = strtoull(sz_j->valuestring,  NULL, 16);
        if (sz == 0) continue;

        /* 匹配 channel 编号和类型 */
        int ch_idx = -1;
        if      (strstr(flag, "ch1")) ch_idx = 0;
        else if (strstr(flag, "ch2")) ch_idx = 1;
        else if (strstr(flag, "ch3")) ch_idx = 2;
        if (ch_idx < 0) continue;

        HyperampChannel *ch = &g_ctx.ch[ch_idx];

        if (strstr(flag, "tx-queue")) {
            /* sel4-tx-queue = seL4 发送 / Linux 接收 → rx_phys_addr */
            ch->rx_phys_addr = paddr;
            ch->rx_phys_size = sz;
            found_rx[ch_idx] = 1;
        } else if (strstr(flag, "rx-queue")) {
            /* sel4-rx-queue = seL4 接收 / Linux 发送 → tx_phys_addr */
            ch->tx_phys_addr = paddr;
            ch->tx_phys_size = sz;
            found_tx[ch_idx] = 1;
        } else if (strstr(flag, "data-region")) {
            int s = ch->data_seg_cnt;
            if (s >= HYPERAMP_MAX_DATA_SEGS) {
                printf("[Warn] ch%d: too many data segs, skip 0x%lx\n", ch_idx+1, paddr);
                continue;
            }
            ch->data_segs[s].phys_addr = paddr;
            ch->data_segs[s].phys_size = sz;
            ch->data_seg_cnt++;
            found_data[ch_idx] = 1;
        }
    }
    cJSON_Delete(root);

    for (int c = 0; c < HYPERAMP_NUM_CHANNELS; c++) {
        HyperampChannel *ch = &g_ctx.ch[c];
        printf("[Info] ch%d: TX=0x%lx(%zu) RX=0x%lx(%zu) data_segs=%d\n",
               c+1, ch->tx_phys_addr, ch->tx_phys_size,
               ch->rx_phys_addr, ch->rx_phys_size, ch->data_seg_cnt);
        if (!found_tx[c] || !found_rx[c] || !found_data[c]) {
            printf("[Error] ch%d missing regions (tx=%d rx=%d data=%d)\n",
                   c+1, found_tx[c], found_rx[c], found_data[c]);
            while(1) {}
        }
    }
}

static void print_usage(const char *prog)
{
    printf("Usage: %s [options]\n", prog);
    printf("Options:\n");
    printf("  -c          Create/initialize queues (default: connect to existing)\n");
    printf("  -j FILE     SHM JSON config path (zone_shm.json)\n");
    printf("  -a ADDR     Physical address in hex (default: 0x%lx)\n", SHM_START_PADDR);
    printf("  -C CH       Target channel: 1, 2, 3, or 'all' (default: 1)\n");
    printf("  -s MSG      Send a test message (Data type)\n");
    printf("  -e MSG      Request Encryption Service (ID 1)\n");
    printf("  -d MSG      Request Decryption Service (ID 2)\n");
    printf("  -p MSG      Request Echo Service (ID 0)\n");
    printf("  -o FILE     Save response to output file\n");
    printf("  -w          Wait for response after sending\n");
    printf("  -r          Receive messages\n");
    printf("  -t          Run interactive test\n");
    printf("  -h          Show this help\n");
    printf("\nChannel Examples:\n");
    printf("  Send to CH1 only:  %s -j cfg.json -C 1 -s hello\n", prog);
    printf("  Send to CH2 only:  %s -j cfg.json -C 2 -s hello\n", prog);
    printf("  Broadcast all:     %s -j cfg.json -C all -s hello\n", prog);
    printf("\nFile Input:\n");
    printf("  Use @filename to read data from file, e.g.:\n");
    printf("    %s -e @plaintext.txt -o encrypted.bin -w\n", prog);
    printf("    %s -d @encrypted.bin -o decrypted.txt -w\n", prog);
}

/**
 * @brief 从文件读取数据
 * @param filename 文件名
 * @param data 输出缓冲区 (需要 free)
 * @param data_len 输出数据长度
 * @param max_size 最大允许文件大小 (Bulk模式使用BULK_BUFFER_SIZE, 普通模式使用HYPERAMP_MSG_MAX_SIZE)
 * @return 0 成功, -1 失败
 */
static int read_file(const char *filename, uint8_t **data, size_t *data_len, size_t max_size)
{
    FILE *fp = fopen(filename, "rb");
    if (!fp) {
        perror("[HyperAMP] Failed to open input file");
        return -1;
    }
    
    fseek(fp, 0, SEEK_END);
    long size = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    
    if (size <= 0 || (size_t)size > max_size) {
        printf("[HyperAMP] File too large or empty: %ld bytes (max %zu)\n", size, max_size);
        fclose(fp);
        return -1;
    }
    
    *data = (uint8_t *)malloc(size);
    if (!*data) {
        perror("[HyperAMP] Failed to allocate memory");
        fclose(fp);
        return -1;
    }
    
    size_t read_bytes = fread(*data, 1, size, fp);
    fclose(fp);
    
    if (read_bytes != (size_t)size) {
        printf("[HyperAMP] Failed to read file: expected %ld, got %zu\n", size, read_bytes);
        free(*data);
        *data = NULL;
        return -1;
    }
    
    *data_len = read_bytes;
    printf("[HyperAMP] Read %zu bytes from %s\n", read_bytes, filename);
    return 0;
}

/**
 * @brief 将数据写入文件
 * @param filename 文件名
 * @param data 数据
 * @param data_len 数据长度
 * @return 0 成功, -1 失败
 */
static int write_file(const char *filename, const uint8_t *data, size_t data_len)
{
    FILE *fp = fopen(filename, "wb");
    if (!fp) {
        perror("[HyperAMP] Failed to open output file");
        return -1;
    }
    
    size_t written = fwrite(data, 1, data_len, fp);
    fclose(fp);
    
    if (written != data_len) {
        printf("[HyperAMP] Failed to write file: expected %zu, wrote %zu\n", data_len, written);
        return -1;
    }
    
    printf("[HyperAMP] Wrote %zu bytes to %s\n", written, filename);
    return 0;
}

/**
 * @brief 执行 Bulk Transfer (大文件传输)
 * 核心逻辑：
 * 1. 检查数据大小 (< 2MB)
 * 2. 写入共享内存固定位置 (OFFSET 0x100000)
 * 3. 发送 BULK 描述符消息
 * 4. 等待并读取响应结果
 */
int hyperamp_linux_bulk_transfer(const void *input_data, size_t input_len, 
                                void *output_buffer, size_t *output_len, 
                                uint16_t service_id)
{
    if (input_len > BULK_BUFFER_SIZE) {
        printf("[HyperAMP] Error: Data too large for Bulk Transfer (%zu > %d)\n", 
               input_len, BULK_BUFFER_SIZE);
        return HYPERAMP_ERROR;
    }

    // 1. 准备数据: 写入共享内存
    // 使用 Data Region 中间部分 (1MB处) 作为临时大缓冲区
    volatile void *bulk_buf_ptr = (volatile void *)((uintptr_t)g_ctx.data_region + BULK_BUFFER_OFFSET);
    
    // Memory copy to Shared Memory
    volatile uint8_t *dst = (volatile uint8_t *)bulk_buf_ptr;
    const uint8_t *src = (const uint8_t *)input_data;
    for (size_t i = 0; i < input_len; i++) {
        dst[i] = src[i];
    }
    
    // 2. 构造描述符
    HyperampBulkDescriptor desc = {
        .offset = BULK_BUFFER_OFFSET,
        .length = (uint32_t)input_len,
        .service_id = service_id,
        .status = 0 // Request
    };

    printf("[HyperAMP] Bulk Transfer: Writing %zu bytes to offset 0x%x\n", input_len, BULK_BUFFER_OFFSET);

    // 3. 发送消息 (Payload 为描述符)
    int ret = hyperamp_linux_send(HYPERAMP_MSG_TYPE_BULK, 0, 0, &desc, sizeof(desc));
    if (ret != HYPERAMP_OK) {
        printf("[HyperAMP] Failed to send Bulk request\n");
        return ret;
    }

    // 4. 等待响应
    printf("[HyperAMP] Waiting for Bulk response...\n");
    int timeout_ms = 5000;
    while (timeout_ms > 0) {
        if (hyperamp_linux_has_message()) {
            break;
        }
        usleep(1000); // 1ms
        timeout_ms--;
    }

    if (timeout_ms <= 0) {
        printf("[HyperAMP] Timeout waiting for Bulk response\n");
        return HYPERAMP_ERROR;
    }

    // 5. 读取响应
    HyperampMsgHeader hdr;
    HyperampBulkDescriptor resp_desc;
    
    uint16_t actual_pl_len = 0;
    ret = hyperamp_linux_recv(&hdr, &resp_desc, (uint16_t)sizeof(resp_desc), &actual_pl_len);
    if (ret != HYPERAMP_OK) {
        printf("[HyperAMP] Failed to receive Bulk response\n");
        return ret;
    }

    if (hdr.proxy_msg_type != HYPERAMP_MSG_TYPE_BULK) {
        printf("[HyperAMP] Unexpected response type: 0x%x\n", hdr.proxy_msg_type);
        return HYPERAMP_ERROR;
    }

    if (resp_desc.status <= 0) {
        printf("[HyperAMP] Bulk processing failed on seL4 side (status=%d)\n", resp_desc.status);
        return HYPERAMP_ERROR;
    }

    // 6. 读取结果 (从共享内存读回)
    // 注意: seL4 可能处理后的数据长度不变，也可能变化。这里简单假设长度一致或是 header 中的 length
    size_t result_len = resp_desc.length;
    if (result_len > BULK_BUFFER_SIZE) result_len = BULK_BUFFER_SIZE;
    
    // Memory copy from Shared Memory
    volatile uint8_t *bulk_src = (volatile uint8_t *)((uintptr_t)g_ctx.data_region + resp_desc.offset);
    uint8_t *out_dst = (uint8_t *)output_buffer;
    
    for (size_t i = 0; i < result_len; i++) {
        out_dst[i] = bulk_src[i];
    }
    
    *output_len = result_len;
    printf("[HyperAMP] Bulk Transfer Complete: Read %zu bytes from offset 0x%x\n", result_len, resp_desc.offset);

    return HYPERAMP_OK;
}

int main(int argc, char *argv[])
{
    int is_creator = 0;
    uint64_t phys_addr = 0;
    int do_send = 0;
    int do_recv = 0;
    int do_test = 0;
    int do_wait = 0;  // 等待响应
    char *send_msg = NULL;
    char *output_file = NULL;  // 输出文件
    int service_call_id = -1; // -1 表示无服务调用
    int use_bulk = 0;         // 是否使用 Bulk (大数据) 传输
    int use_signed = 0;       // 是否使用签名验证
    int use_validate = 0;     // 是否使用字段验证 (目标检测数据)
    char *signature_file = NULL; // 签名文件路径
    char *shm_json_path = NULL;  // shm JSON 配置路径
    uint8_t *file_data = NULL;  // 文件数据
    size_t file_data_len = 0;
    /* -1 = broadcast all, 0/1/2 = ch1/ch2/ch3 (0-based) */
    int target_ch = 0;  // 默认 CH1

    int opt;
    while ((opt = getopt(argc, argv, "ca:s:e:d:p:o:wrthBS:Vj:C:")) != -1) {
        switch (opt) {
            case 'c':       // Create/initialize queues
                is_creator = 1;
                break;
            case 'a':       // Physical address in hex
                phys_addr = strtoull(optarg, NULL, 16);
                break;
            case 'j':       // SHM JSON config path
                shm_json_path = optarg;
                break;
            case 'C':       // Target channel
                if (strcmp(optarg, "all") == 0) {
                    target_ch = -1;
                } else {
                    int ch = atoi(optarg);
                    if (ch < 1 || ch > HYPERAMP_NUM_CHANNELS) {
                        printf("Invalid channel: %s (use 1-%d or 'all')\n",
                               optarg, HYPERAMP_NUM_CHANNELS);
                        return 1;
                    }
                    target_ch = ch - 1;  /* 转为 0-based */
                }
                break;
            case 's':       // Send
                do_send = 1;
                send_msg = optarg;
                break;
            case 'e':       // Encrypt
                do_send = 1;
                service_call_id = 1;
                send_msg = optarg;
                break;
            case 'd':       // Decrypt
                do_send = 1;
                service_call_id = 2;
                send_msg = optarg;
                break;
            case 'p': // Echo
                do_send = 1;
                service_call_id = 0;
                send_msg = optarg;
                break;
            case 'o':       // Output file
                output_file = optarg;
                break;
            case 'w':       // Wait for response
                do_wait = 1;
                break;
            case 'r':       // Receive
                do_recv = 1;
                break;
            case 't':       // Test
                do_test = 1;
                break;
            case 'B':       // Bulk Transfer
                use_bulk = 1;
                break;
            case 'S':       // Signed data (requires signature file)
                use_signed = 1;
                signature_file = optarg;
                break;
            case 'V':       // Validate mission data fields
                use_validate = 1;
                break;
            case 'h':       // Help
            default:
                print_usage(argv[0]);
                return (opt == 'h') ? 0 : 1;
        }
    }
    
    printf("========================================\n");
    printf("HyperAMP Linux Client Test\n");
    printf("========================================\n");

    // 解析 shm JSON，获取三个区域的物理地址
    if (shm_json_path) {
        parse_global_addr(shm_json_path);
    } else {
        // 兼容 -a 参数：根据通道基地址自动计算，填充全部 3 个 channel
        static const uint64_t ch_tx[HYPERAMP_NUM_CHANNELS]   = { SHM_CH0_TX_PADDR,   SHM_CH1_TX_PADDR,   SHM_CH2_TX_PADDR   };
        static const uint64_t ch_rx[HYPERAMP_NUM_CHANNELS]   = { SHM_CH0_RX_PADDR,   SHM_CH1_RX_PADDR,   SHM_CH2_RX_PADDR   };
        static const uint64_t ch_data[HYPERAMP_NUM_CHANNELS] = { SHM_CH0_DATA_PADDR, SHM_CH1_DATA_PADDR, SHM_CH2_DATA_PADDR };
        static const size_t   ch_dsz[HYPERAMP_NUM_CHANNELS]  = { SHM_CH0_DATA_SIZE,  SHM_CH1_DATA_SIZE,  SHM_CH2_DATA_SIZE  };

        for (int c = 0; c < HYPERAMP_NUM_CHANNELS; c++) {
            /* -a 只覆盖 CH0 的 TX 起始地址，其余保持默认 */
            g_ctx.ch[c].tx_phys_addr   = (c == 0 && phys_addr) ? phys_addr          : ch_tx[c];
            g_ctx.ch[c].rx_phys_addr   = (c == 0 && phys_addr) ? phys_addr + SHM_QUEUE_SIZE : ch_rx[c];
            g_ctx.ch[c].data_phys_addr = (c == 0 && phys_addr) ? phys_addr + 2 * SHM_QUEUE_SIZE : ch_data[c];
            g_ctx.ch[c].tx_phys_size   = SHM_QUEUE_SIZE;
            g_ctx.ch[c].rx_phys_size   = SHM_QUEUE_SIZE;
            g_ctx.ch[c].data_phys_size = ch_dsz[c];
        }
    }

    // 初始化
    if (target_ch >= 0) g_ctx.active_ch = target_ch;
    if (hyperamp_linux_init(phys_addr, is_creator) != HYPERAMP_OK) {
        printf("Failed to initialize HyperAMP\n");
        return 1;
    }
    
    // 发送测试消息
    if (do_send && send_msg) {
        uint8_t *data_to_send = NULL;
        size_t data_len = 0;
        int need_free = 0;
        
        // 检查是否是文件输入 (@filename)
        if (send_msg[0] == '@') {
            const char *filename = send_msg + 1;
            // Bulk模式允许最大2MB, 普通模式最大4KB
            size_t max_file_size = use_bulk ? BULK_BUFFER_SIZE : HYPERAMP_MSG_MAX_SIZE;
            if (read_file(filename, &file_data, &file_data_len, max_file_size) != 0) {
                printf("Failed to read input file: %s\n", filename);
                hyperamp_linux_cleanup();
                return 1;
            }
            data_to_send = file_data;
            data_len = file_data_len;
            need_free = 1;
        } else {
            data_to_send = (uint8_t *)send_msg;
            data_len = strlen(send_msg);
        }
        
        if (service_call_id >= 0) {
            if (use_bulk) {
                // === Bulk Transfer Mode ===
                uint8_t *final_data = data_to_send;
                size_t final_len = data_len;
                int final_service_id = service_call_id;
                int free_final_data = 0;
                
                // 如果使用字段验证模式，修改 service_id
                if (use_validate) {
                    if (service_call_id == SERVICE_ENCRYPT) {
                        final_service_id = SERVICE_VALIDATE_ENCRYPT;
                    } else if (service_call_id == SERVICE_DECRYPT) {
                        final_service_id = SERVICE_VALIDATE_DECRYPT;
                    }
                    printf("[HyperAMP] Using validation mode (service_id=%d)\n", final_service_id);
                }
                
                // 如果使用签名验证，构造签名数据包
                if (use_signed && signature_file) {
                    printf("[HyperAMP] Reading signature from %s\n", signature_file);
                    
                    uint8_t *sig_data = NULL;
                    size_t sig_len = 0;
                    if (read_file(signature_file, &sig_data, &sig_len, 256) != 0) {
                        printf("Failed to read signature file: %s\n", signature_file);
                        if (need_free && file_data) free(file_data);
                        hyperamp_linux_cleanup();
                        return 1;
                    }
                    
                    if (sig_len < 64 || sig_len > 72) {
                        printf("Invalid signature length: %zu (expected 64-72)\n", sig_len);
                        free(sig_data);
                        if (need_free && file_data) free(file_data);
                        hyperamp_linux_cleanup();
                        return 1;
                    }
                    
                    // 构造签名数据包: SignedHeader + Payload
                    size_t signed_total = sizeof(HyperampSignedHeader) + data_len;
                    final_data = malloc(signed_total);
                    if (!final_data) {
                        printf("Failed to allocate signed data buffer\n");
                        free(sig_data);
                        if (need_free && file_data) free(file_data);
                        hyperamp_linux_cleanup();
                        return 1;
                    }
                    
                    HyperampSignedHeader *hdr = (HyperampSignedHeader *)final_data;
                    hdr->magic = SIG_MAGIC;
                    hdr->sig_len = (uint16_t)sig_len;
                    hdr->reserved = 0;
                    hdr->payload_len = (uint32_t)data_len;
                    memcpy(hdr->signature, sig_data, sig_len);
                    memcpy(final_data + sizeof(HyperampSignedHeader), data_to_send, data_len);
                    
                    final_len = signed_total;
                    free_final_data = 1;
                    free(sig_data);
                    
                    // 修改 service_id 为签名验证版本
                    if (service_call_id == SERVICE_ENCRYPT) {
                        final_service_id = SERVICE_VERIFY_ENCRYPT;
                    } else if (service_call_id == SERVICE_DECRYPT) {
                        final_service_id = SERVICE_VERIFY_DECRYPT;
                    } else {
                        final_service_id = SERVICE_VERIFY_ONLY;
                    }
                    
                    printf("[HyperAMP] Constructed signed payload: %zu bytes (sig=%zu, data=%zu)\n",
                           final_len, sig_len, data_len);
                }
                
                printf("\nCalling Service ID %d [BULK MODE] with %zu bytes\n", final_service_id, final_len);
                
                void *bulk_response_buf = malloc(BULK_BUFFER_SIZE);
                if (!bulk_response_buf) {
                    printf("Failed to allocate bulk response buffer\n");
                } else {
                    size_t out_len = 0;
                    if (hyperamp_linux_bulk_transfer(final_data, final_len, bulk_response_buf, &out_len, final_service_id) == HYPERAMP_OK) {
                        printf("\nReceived Bulk response: %zu bytes\n", out_len);
                        if (output_file) {
                            if (write_file(output_file, bulk_response_buf, out_len) == 0) {
                                printf("✓ Response saved to %s\n", output_file);
                            }
                        } else {
                            printf("(Output file not specified, skipping save)\n");
                        }
                    } else {
                        printf("Bulk transfer failed\n");
                    }
                    free(bulk_response_buf);
                }
                
                if (free_final_data && final_data) {
                    free(final_data);
                }
            } else {
                // === Normal Message Mode ===
                printf("\nCalling Service ID %d with %zu bytes (ch: %s)\n",
                       service_call_id, data_len,
                       target_ch < 0 ? "all" : (char[4]){(char)('1'+target_ch),0});
                int send_ret = (target_ch < 0)
                    ? hyperamp_linux_call_service_all(service_call_id, data_to_send, data_len)
                    : hyperamp_linux_call_service(service_call_id, data_to_send, data_len);
                if (send_ret == HYPERAMP_OK) {
                    printf("Service request sent successfully\n");
                    
                    // 如果指定了 -w 或 -o，等待响应
                    if (do_wait || output_file) {
                        printf("\nWaiting for response from seL4...\n");
                        HyperampMsgHeader hdr;
                        uint8_t payload[HYPERAMP_MSG_MAX_SIZE];
                        uint16_t payload_len;
                        
                        // 简单轮询等待响应 (最多等待 5 秒)
                        int timeout = 50;  // 50 * 100ms = 5s
                        int received = 0;
                        while (timeout-- > 0 && !received) {
                            if (hyperamp_linux_recv(&hdr, payload, sizeof(payload), &payload_len) == HYPERAMP_OK) {
                                printf("\nReceived response: %u bytes\n", payload_len);
                                
                                // 保存到文件
                                if (output_file) {
                                    if (write_file(output_file, payload, payload_len) == 0) {
                                        printf("✓ Response saved to %s\n", output_file);
                                    }
                                } else {
                                    // 打印到控制台
                                    printf("Response data:\n");
                                    for (uint16_t i = 0; i < payload_len && i < 64; i++) {
                                        printf("%02x ", payload[i]);
                                        if ((i + 1) % 16 == 0) printf("\n");
                                    }
                                    if (payload_len > 64) printf("... (%u bytes total)\n", payload_len);
                                    printf("\n");
                                }
                                received = 1;
                            } else {
                                usleep(100000);  // 100ms
                            }
                        }
                        
                        if (!received) {
                            printf("Timeout waiting for response\n");
                        }
                    }
                } else {
                    printf("Failed to send service request\n");
                }
            }
        } else {
            printf("\nSending Data message: %zu bytes (ch: %s)\n",
                   data_len, target_ch < 0 ? "all" : (char[4]){(char)('1'+target_ch),0});
            int send_ret = (target_ch < 0)
                ? hyperamp_linux_send_data_all(1, data_to_send, data_len)
                : hyperamp_linux_send_data(1, data_to_send, data_len);
            if (send_ret == HYPERAMP_OK) {
                printf("Message sent successfully\n");
            } else {
                printf("Failed to send message\n");
            }
        }
        
        if (need_free && file_data) {
            free(file_data);
            file_data = NULL;
        }
    }
    
    // 接收消息
    if (do_recv) {
        printf("\nReceiving messages...\n");
        HyperampMsgHeader hdr;
        uint8_t payload[4096];
        uint16_t payload_len;
        
        while (hyperamp_linux_recv(&hdr, payload, sizeof(payload) - 1, &payload_len) == HYPERAMP_OK) {
            payload[payload_len] = '\0';
            printf("Received: type=%u, len=%u, data=[%s]\n", 
                   hdr.proxy_msg_type, payload_len, payload);
        }
        printf("No more messages\n");
    }
    
    // 交互测试
    if (do_test) {
        printf("\nInteractive test mode\n");
        printf("Commands: s <msg> (send), r (receive), q (quit), ? (status)\n");
        
        char line[256];
        while (1) {
            printf("> ");
            fflush(stdout);
            
            if (!fgets(line, sizeof(line), stdin)) break;
            
            // 去除换行符
            size_t len = strlen(line);
            if (len > 0 && line[len-1] == '\n') line[len-1] = '\0';
            
            if (line[0] == 'q') {
                break;
            } else if (line[0] == '?') {
                hyperamp_linux_get_status();
            } else if (line[0] == 's' && line[1] == ' ') {
                char *msg = line + 2;
                hyperamp_linux_send_data(1, msg, strlen(msg));
            } else if (line[0] == 'r') {
                HyperampMsgHeader hdr;
                uint8_t payload[4096];
                uint16_t payload_len;
                
                if (hyperamp_linux_recv(&hdr, payload, sizeof(payload) - 1, &payload_len) == HYPERAMP_OK) {
                    payload[payload_len] = '\0';
                    printf("Received: type=%u, len=%u, data=[%s]\n", 
                           hdr.proxy_msg_type, payload_len, payload);
                } else {
                    printf("No message available\n");
                }
            } else {
                printf("Unknown command. Use ? for status, s <msg> to send, r to receive, q to quit\n");
            }
        }
    }
    
    // 显示最终状态
    hyperamp_linux_get_status();
    
    // 清理
    hyperamp_linux_cleanup();
    
    return 0;
}

#endif /* HYPERAMP_TEST_MAIN */
