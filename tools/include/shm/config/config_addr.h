#ifndef _CONFIG_ADDR_H_
#define _CONFIG_ADDR_H_

struct AddrInfo
{
  uint64_t start; /* 起始地址：兼容32/64位 */
  uint32_t len;   /* 长度 */
};

// TODO: define a global addr_infos array (get from .json)
// .json just allocate and map region 
// the actual addr should be written in the .h and .c

/* 所有核间通信可能会用到的内存块地址信息 */
extern struct AddrInfo addr_infos[];

#define LINUX_SHM_BUF_ADDR_INFO               (&addr_infos[0])
#define LINUX_2_NPUCore_MSG_QUEUE_ADDR_INFO   (&addr_infos[1])
#define NPUCore_2_NPUCore_MSG_QUEUE_ADDR_INFO (&addr_infos[2])

#endif // _CONFIG_ADDR_H_