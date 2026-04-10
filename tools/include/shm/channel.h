#ifndef _CHANNEL_H_
#define _CHANNEL_H_

struct Channel /* 核间通道结构：应用层结构 */
{
  struct ChannelInfo* channel_info;//  info
  struct AmpMsgQueue* msg_queue;   //  message queue
  // TODO: add mutex to protect the channel
  struct MsgQueueMutex* msg_queue_mutex; /* 消息队列互斥体 */
  
  // TODO: Mutex ? a lock in hypervisor? (between two vms) ? not need?
  void* msg_queue_mutex_start;
  uint64_t msg_queue_mutex_start_pa;
};

struct ChannelOps
{
  int32_t (*channels_init)(void);
  int32_t (*channel_is_ready)(struct Channel* channel);
  /* 获取发往目标核心的通道 */
  struct Channel* (*target_channel_get)(uint32_t target_zone_id);
  /* 向指定通道发送核间消息通知 */
  int32_t (*channel_notify)(struct Channel* channel);
  /* 从通道中获取一个空闲消息缓冲区 */
  struct Msg* (*empty_msg_get)(struct Channel* channel);
  /* 将一个不再使用的消息缓冲区归还到通道 */
  int32_t (*empty_msg_put)(struct Channel* channel,struct Msg* msg);
  /* 将一个填充过的消息缓冲区放入待处理消息队列 */
  int32_t (*msg_send)(struct Channel* channel,struct Msg* msg);
  /* 将一个填充过的消息缓冲区放入待处理消息队列并直接通知 */
  int32_t (*msg_send_and_notify)(struct Channel* channel,struct Msg* msg);
  int32_t (*channels_destroy)(void);
};
extern struct ChannelOps channel_ops;


#endif // _CHANNEL_H_