#ifndef _CONFIG_MSGQUEUE_H_
#define _CONFIG_MSGQUEUE_H_

#define MSG_QUEUE_MUTEX_SIZE (128U)       /* 位于内核中的消息队列互斥区域将按照这个大小进行划分 */
#define MSG_QUEUE_MARK_BUSY (0xFFFFFFFFU) /* 负责处理该消息队列的核心正在处理（16位） */
#define MSG_QUEUE_MARK_IDLE (0xBBBBBBBBU) /* 负责处理该消息队列的核心正在闲置（16位） */
#define MSG_QUEUE_MAX_ENTRY_CNT (0xFFFFU) /* 消息队列中能够管理的最大消息数量：uint16_t */

#endif // _CONFIG_MSGQUEUE_H_