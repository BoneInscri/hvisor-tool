#ifndef _CONFIG_CHANNEL_H_
#define _CONFIG_CHANNEL_H_

#include "config_zone.h"

struct ChannelInfo
{
  uint32_t channel_id; // id

  uint32_t irq_req; /* 通道接受 消息中断：中断号（SWI in LoongArch），由通道接收方负责实现 */ 
  // Request Interrupt   (放好请求的数据后，触发请求中断，告知dst)
  uint32_t irq_rsp; /* 通道响应 消息中断：中断号（SWI in LoongArch），由通道发送方负责实现 */ 
  // Response Interrupt （请求的数据放好后，触发响应中断，告知src）

  struct ZoneInfo* src_zone;  /* 通道发送zone */ 
  struct ZoneInfo* dst_zone;  /* 通道接收zone */ 

  struct AddrInfo* src_queue; /* 位于发送端地址空间：发送端向该消息队列发送信息，接收端从这个消息队列里面接收并处理信息，由接收端负责初始化 */
  struct AddrInfo* dst_queue; /* 位于接收端地址空间：接收端从该消息队列获取消息，由接收端负责初始化 */
};

/* 所有的核间通信通道信息，每个通道都存在一个发送端和接收端，同时，每个核心既可以是发送端也可以是接收端*/
extern struct ChannelInfo channel_infos[];

// alias for convenience
#define LINUX_2_NPUCore_CHANNEL_INFO (&channel_infos[0])

#endif // _CONFIG_CHANNEL_H_