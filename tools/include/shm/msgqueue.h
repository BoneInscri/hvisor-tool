#ifndef _MSGQUEUE_H_
#define _MSGQUEUE_H_

#include "shm/msg.h"
#include "shm/spinlock.h"

// TODO: add lock to protect msg_queue

struct MsgQueueMutex /* 消息队列互斥结构：内核与用户空间共享该数据结构 */
{
    // TODO: for mult-thread, it needs to add lock to protect msg_queue init
    // atomic_char32_t mutex_init_mark;

    // 在发送端通知接收端时设为：MSG_QUEUE_MARK_BUSY
    // 在发送端接收到接收端响应时设为：MSG_QUEUE_MARK_IDLE

    volatile uint32_t msg_queue_mark; /* 响应后修改 */ // TODO modify to atomic?
    // atomic_char32_t msg_queue_mark;
    volatile uint16_t msg_wait_cnt;

    // TODO: for mult-thread, it needs to add lock to protect msg_queue
    ByteFlag empty_lock; /* 空闲队列锁 */
    ByteFlag wait_lock;  /* 等待队列锁 */
};

// Message Queue entry
struct MsgEntry
{
  struct Msg msg;   /* 消息实体：必须放在第一个位置，相当于继承于 Msg */
  uint16_t cur_idx; /* 消息实体在消息队列里面的下标 */
  uint16_t nxt_idx; /* 消息实体所在消息队列的下一个消息实体的下标 */
}__attribute__((aligned(MEMORY_ALIGN_SIZE)));


// Message Queue
struct AmpMsgQueue
{
	volatile uint32_t working_mark; /* 标记该消息队列的接收者是否准备完毕 */
	uint16_t buf_size; /* 缓冲区队列的缓冲区数量，必须大于 0 才算初始化成功，初始状态为 0 */
	volatile uint16_t empty_h; /* 空闲缓冲区链头下标 */
	volatile uint16_t wait_h;  /* 待处理缓冲区链头下标 */
	volatile uint16_t proc_ing_h; /* 服务端正在处理的缓冲区链头下标 */

	struct MsgEntry entries[0];   /* 实际存放的消息 */
}__attribute__((aligned(MEMORY_ALIGN_SIZE)));


struct MsgQueueOps
{
  int32_t (*init)(struct AmpMsgQueue* msg_queue,uint32_t mem_len);
  int32_t (*is_ready)(struct AmpMsgQueue* msg_queue);
  uint16_t (*pop)(struct AmpMsgQueue* msg_queue,uint16_t* head);
  int32_t (*push)(struct AmpMsgQueue* msg_queue,uint16_t* head,uint16_t msg_index);
  int32_t (*transfer)(struct AmpMsgQueue* msg_queue, uint16_t* from_head, uint16_t* to_head);
};

extern struct MsgQueueOps msg_queue_ops;

struct MsgQueueMutexOps
{
  int32_t (*mutex_init)(struct MsgQueueMutex* mutex, uint16_t msg_queue_buf_size);
  int32_t (*mutex_is_init)(struct MsgQueueMutex* mutex);
};

extern struct MsgQueueMutexOps msg_queue_mutex_ops;

#endif // _MSGQUEUE_H_