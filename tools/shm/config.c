
#include "shm/config/config_common.h"
#include "shm/config/config_zone.h"
#include "shm/config/config_addr.h"
#include "shm/config/config_channel.h"
#include "shm/config/config_shm.h"
#include <stdio.h>

/* core_cfg.h */
struct ZoneInfo zone_infos[] = 
{
  { /* 0: Root Linux 节点 */
    .name = ZONE_LINUX_NAME,
    .id = ZONE_LINUX_ID
  },
  { /* 1： NPUcore 节点 */
    .name = ZONE_NPUcore_NAME,
    .id = ZONE_NPUcore_ID
  },
  { /* 2: RT-Thread 节点 */
    .name = ZONE_RT_Thread_ID,
    .id = ZONE_RT_Thread_ID
  },
  { /* 3: ZONE_SeL4 节点 */
    .name = ZONE_SeL4_NAME,
    .id = ZONE_SeL4_ID
  }
};


// GLOBAL ADDRESS INFOS
struct AddrInfo addr_infos[] =
{
  { /* 0： Linux 地址空间下使用的共享内存地址信息 4MB */
      .start = 0x0, // TODO: get from .json
      .len = 0
  },
  { /* 1: Linux 地址空间下发送给 NPUCore 的环形缓冲区 4KB */
      .start = 0x0U,// TODO: get from .json
      .len = 0
  },
};


/* channel_cfg.h */
struct ChannelInfo channel_infos[] = 
{
    { /* 通道1： RootLinux -> NPUcore */
      .channel_id = 1,
      .irq_req = 74, // SWI1
      .irq_rsp = 74, // SWI1 // TODO: modify it 
      .src_zone = LINUX_ZONE_INFO,
      .dst_zone = NPUCORE_ZONE_INFO,
      .src_queue = LINUX_2_NPUCore_MSG_QUEUE_ADDR_INFO,
      .dst_queue = NPUCore_2_NPUCore_MSG_QUEUE_ADDR_INFO
      // used in address space of NPUCore
    },
    { /* null */
      .src_zone = NULL
    }   
};

/* shm_cfg.h */
struct ShmCfg shm_cfgs[] = 
{
  { /* Linux  共享内存配置信息 */
    .zone = LINUX_ZONE_INFO,
    .zone_shm = LINUX_SHM_BUF_ADDR_INFO,
    .pblock_size = (2 * MB),
    .vblock_size = (2 * MB),
    .min_block_size = (512 * B),// TODO: check it
    .bit_align = MEMORY_ALIGN_SIZE
  },
  {
    .zone = NULL
  }
};

static struct ShmCfg* shm_cfg_get_by_id(uint16_t target_id)
{
    int i = 0;
    for (i = 0; shm_cfgs[i].zone != NULL; i++)
    {
        if (shm_cfgs[i].zone->id == target_id)
        {
            // printf("shm_cfg_get_by_id_info: find shm cfg success, target id = %u\n", target_id);
            return &shm_cfgs[i];
        }
    }
    return NULL;
}

struct ShmCfgOps shm_cfg_ops = 
{
    .get_by_id = shm_cfg_get_by_id
};

struct ZoneInfo* zone_info = LINUX_ZONE_INFO;


