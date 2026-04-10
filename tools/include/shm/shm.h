#ifndef _SHM_H_
#define _SHM_H_

#include <stdint.h>
#include "shm/config/config_common.h"
#include "shm/config/config_shm.h"
#include "shm/spinlock.h"

// Persistent Block : 持久型内存块
// Volatile Block: 易失型内存块

enum MallocType
{
  MALLOC_TYPE_P = 1, /* 长期区块申请内存: 01 */
  MALLOC_TYPE_V = 2, /* 短期区块申请内存: 10 */
};

enum VBlockState /* 短期内存块当前状态 */
{
  VBLOCK_STATE_DISENABLE = 0, /* 该块已经被分配完毕或者无法满足最近的一次内存分配请求且分配出去的内存尚未归还完毕 */
  VBLOCK_STATE_ENABLE = 1,    /* 存在可分配的空闲内存 */
};

struct PBlockInfo /* 长期内存块信息 */
{
  volatile uint32_t available_size;    /* 内存块还有多少空闲未分配内存，没有空闲空间则分配失败 */
  volatile uint64_t next_alloc_offset; /* 下一次分配内存时从那个地址偏移量开始 */
} __attribute__((aligned(MEMORY_ALIGN_SIZE)));

struct VBlockInfo /* 短期内存块管理信息 */
{
    volatile uint32_t state; /* 当前内存块的分配情况：@VBlockState */

    volatile uint32_t alloc_count; /* 当前块已经分配出去的块数 */
    volatile uint32_t free_count;  /* 当前块被分配出去的内存块已经有多少已经释放了 */

    uint64_t alloc_start_offset;         /* 区块起始地址相对于共享内存起始地址的偏移量 */
    volatile uint64_t next_alloc_offset; /* 下一块内存分配起始地址相对于共享内存起始地址的偏移量 */

    volatile uint32_t available_size; /* 当前最大可分配内存大小 */
    uint32_t total_length;            /* 记录分区的总长度，用于归还后的区块恢复 */
} __attribute__((aligned(MEMORY_ALIGN_SIZE)));

enum BlockState /* 内存管理区块的状态 */
{
  BLOCK_STATE_UNUSED = 0,     /* 共享内存划分失败 */
  BLOCK_STATE_PERSISTENT = 1, /* 共享内存存在长期区块：01 */
  BLOCK_STATE_VOLATILE = 2,   /* 共享内存存在短期区块：10 */
  BLOCK_STATE_BOTH_USED = 3,  /* 共享内存存在长期和短期区块：01 | 10 = 11 */
};

struct ShmInfos /* 总的区块管理信息数据结构：放在内核中 */
{
  // atomic_char32_t block_init_mark; /* 共享内存初始化标记 */

  ByteFlag pblock_lock; /* 长期区块内存分配核间互斥标志 */
  ByteFlag vblock_lock; /* 短期区块内存分配核间互斥标志 */

  uint8_t block_state_info;      /* 标记内存块的长期和短期划分状态: @BlockState */
  struct PBlockInfo pblock_info; /* 存放长期区块的管理信息 */
  
  uint32_t vblock_each_size; /* 每个短期块所占大小 */
  volatile uint32_t vblock_current; /* 当前使用的虚拟块下标*/
  struct VBlockInfo vblock_infos[SHM_VBLOCK_CNT]; /* 短期区块管理信息组 */
} __attribute__((aligned(MEMORY_ALIGN_SIZE)));


struct Shm /* 共享内存管理信息结构 */
{
    struct ShmInfos* shm_infos; /* 共享内存的信息，该信息共享于内核中 */
    uint64_t shm_infos_pa;

    struct ShmCfg *shm_cfg; /* 共享内存配置信息 */
    
    void *shm_zone_start;   /* 当前zone共享内存起始地址 */

#if UNUSED_COUNT
    uint64_t unused_mem_size; /* 自启动开始至今所产生的内存碎片总和 */
#endif /* UNUSED_COUNT */
};

struct ShmOps /* 共享内存操作集合 */
{
    /* 通过传入的指向共享内存的地址计算出相对于总共享内存起始地址的偏移量 */
    uint64_t (*addr_to_offset)(void* ptr);
    /* 通过相对于总共享内存起始地址的偏移量，获取一个指向共享内存的指针 */
    void* (*offset_to_addr)(uint64_t offset);
    /* 根据相关配置初始化共享内存 */
    int32_t (*shm_init)(void);
    /* 销毁共享内存：只要一个线程销毁，所有的其它任务都无法再使用谨慎使用或者不使用 */
    int32_t (*shm_destory)(void);
    /* 从共享内存中分配内存空间 */
    void* (*shm_malloc)(uint32_t size,enum MallocType type);
    /* 将申请的短期共享内存归还管理池管理 */
    void (*shm_free)(void* ptr);
};

extern struct ShmOps shm_ops;

#endif // _SHM_H_