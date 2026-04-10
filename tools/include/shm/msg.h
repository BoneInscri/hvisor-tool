#ifndef _MSG_H_
#define _MSG_H_

#include "config/config_common.h"
#include <stdint.h>

enum MsgDealState
{
  MSG_DEAL_STATE_NO = 0, // not dealt yet
  MSG_DEAL_STATE_YES = 1 // has dealt
};

enum MsgServiceResult /* 消息是否被正确处理 */
{
  MSG_SERVICE_RET_NONE = 0,     /* 消息还未被处理 */
  MSG_SERVICE_RET_SUCCESS = 1,  /* 服务正确响应 */
  MSG_SERVICE_RET_FAIL = 2,     /* 服务未曾正确服务，或参数错误等 */
  MSG_SERVICE_RET_NOT_EXITS = 3, /* 请求的服务不存在 */
  MSG_SERVICE_RET_WAIT = 4 /* 被引入用户态，等待处理 */
};

// Message
struct Msg
{
  /* 消息标记：用于标记消息状态信息，共 16 位可使用位域进行扩展 */
  struct MsgFlag 
  {
    uint16_t deal_state : 1;     /* 消息是否被处理 */
    uint16_t service_result : 2; /* 消息对应的服务是否被正确服务 */
  } flag;
  
  uint16_t service_id; /* 请求的服务端服务ID */
 
  // for amp protocol 
  // TODO: check the usage of offset? can we use virtual address?
  uint32_t offset; /* 共享内存起始偏移量 */
  uint32_t length; /* 共享内存长度 */

  // for PROTOCOL_PRIVATE (for compare)
  // uint32_t data_size; /* 本条消息携带的数据量 */
  // uint8_t data[MSG_PAYLOAD_SIZE];    /* 数据本身：配置消息大小时最好四字节对齐 */

  // TODO： check the type of protocol 
}__attribute__((aligned(MEMORY_ALIGN_SIZE)));


struct MsgOps
{
  // reset msg
  void (*msg_reset)(struct Msg *msg);  
  // msg has dealt
  int32_t (*msg_is_dealt)(struct Msg *msg);
};

extern struct MsgOps msg_ops;

#endif // _MSG_H_