#ifndef _CONFIG_SHM_H_
#define _CONFIG_SHM_H_

struct ShmCfg
{
  struct ZoneInfo *zone;       /* zone的共享内存配置 */
  struct AddrInfo *zone_shm;   /* 共享内存地址空间：用于内存分配(BUF) */

  uint32_t pblock_size;        /* 长期区块总大小 */
  uint32_t vblock_size;        /* 短期区块总大小 */
  uint32_t min_block_size;     /* 最小区块大小，配置的块太多每个块小于该值  时，初始化失败 */

  uint32_t bit_align;          /* 该共享内存分配时的字节对齐配置： 2 4 8 16 ... 2^n*/
};

/* 短期块数量会影响到共享内存的使用效率，需要酌情考虑 */
#define SHM_VBLOCK_CNT (8U) /* 短期区块数量 */
#define UNUSED_COUNT (1) /* 碎片记录 */

struct ShmCfgOps /* 共享内存配置信息操作集合 */
{
    /* 通过ZoneID获取Zone的配置信息 */
    struct ShmCfg* (*get_by_id)(uint16_t target_id);
};
extern struct ShmCfgOps shm_cfg_ops;

#endif // _CONFIG_SHM_H_