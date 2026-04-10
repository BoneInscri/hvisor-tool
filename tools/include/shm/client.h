#ifndef _CLIENT_H_
#define _CLIENT_H_

#include <stdint.h>
#include "channel.h"
#include "msg.h"

#define ASSERT(cond)                                                           \
    do {                                                                       \
        if ((cond) == NULL) {                                                    \
            printf("%s:%d assert failed\n", __FILE__, __LINE__);               \
            while(1) {}                                                        \
        }                                                                      \
    } while (0);

struct Client
{
  uint32_t init_mark; /* 初始化标记 */
  struct Channel* remote_channel; /* 当前客户端是与那个核心的连接通道在通信 */
  uint32_t msg_cnt; /* 当前客户端申请了几个消息缓冲区 */
  uint32_t shm_cnt; /* 当前客户端申请了几块内存 */
  uint32_t request_cnt;
};

struct ClientOps
{
  int32_t (*client_init)(struct Client *raw_client, uint32_t remote_zone_id); // client init
  int32_t (*client_destory)(struct Client *client);   // client destory
  struct Msg *(*empty_msg_get)(struct Client *client, uint32_t remote_service_id); /* 获取一个空闲的消息缓冲区 */
  int32_t (*empty_msg_put)(struct Client *client, struct Msg *empty_msg);          /* 归还空闲消息缓冲区：归任务管理的缓冲区才需要手动归还 */
  int32_t (*msg_send)(struct Client *client, struct Msg *full_msg);                /* 将消息放入待处理消息队列*/
  int32_t (*msg_send_and_notify)(struct Client *client, struct Msg *full_msg);     /* 将消息放入待处理消息队列，并通知远程Zone立即进行处理 */
  int32_t (*msg_notify)(struct Client *client);                                    /* 通知远程Zone处理待处理消息队列 */
  // int32_t (*msg_poll)(struct Msg* aim_msg, uint32_t request_idx);                  /* 查看指定消息是否已经被处理过了 */
  int32_t (*msg_poll)(struct Msg* aim_msg);                  /* 查看指定消息是否已经被处理过了 */

  void *(*shm_malloc)(struct Client *client, uint32_t size, enum MallocType type); /* 从共享内存中分配内存，可以分配长期内存和短期内存 */
  void (*shm_free)(struct Client *client, void *ptr );                             /* 释放短期共享内存 */

  uint64_t (*shm_addr_to_offset)(void *ptr);                                       /* 地址转偏移量 */
  void *(*shm_offset_to_addr)(uint64_t offset);                                    /* 偏移量转地址 */
  uint32_t (*get_client_request_cnt)(struct Client* client);
  void (*set_client_request_cnt)(struct Client* client);
};
extern struct ClientOps client_ops;

#endif // _CLIENT_H_