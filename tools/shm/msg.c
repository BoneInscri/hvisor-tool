#include <stdlib.h>
#include <stdio.h>

#include "shm/msg.h"


static void msg_reset(struct Msg *msg)
{
  // printf("msg_reset\n");

  /* 设置当前消息未被处理 */
  msg->flag.deal_state = MSG_DEAL_STATE_NO; 

  /* 设置当前消息未被服务 */
  msg->flag.service_result = MSG_SERVICE_RET_NONE;

  /* 重置消息数据标记 */
  msg->length = 0; /* 数据标记清空 */
  // TODO: compare with OpenAMP
}


static void printBinary(uint16_t value, int bits) {
  for (int i = bits - 1; i >= 0; i--) {
    printf("%d", (value >> i) & 1);
  }
  printf("\n");
}

static int32_t msg_is_dealt(struct Msg *msg)
{
  // printf("msg_is_dealt\n");
  // printBinary(msg->flag.deal_state, 2 * 8);
  // printBinary(msg->flag.service_result, 2 * 8);
  return msg->flag.deal_state == MSG_DEAL_STATE_YES ? 0 : -1;
}

struct MsgOps msg_ops = 
{
    .msg_reset = msg_reset,
    .msg_is_dealt = msg_is_dealt
};