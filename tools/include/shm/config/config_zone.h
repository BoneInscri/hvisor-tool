#ifndef _CONFIG_CORE_H_
#define _CONFIG_CORE_H_

#include <stdint.h>
#include "config_common.h"

#define ZONE_LINUX_ID     (0U)  /* Root Linux */
#define ZONE_LINUX_NAME     "root-linux"

#define ZONE_NPUcore_ID   (1U)  /* NPUcore */
#define ZONE_NPUcore_NAME   "linux2"

#define ZONE_RT_Thread_ID (2U)  /* RT-Thread */
#define ZONE_RT_Thread_NAME "RT-Thread"

#define ZONE_SeL4_ID      (3U)  /* SeL4 */
#define ZONE_SeL4_NAME      "SeL4"

struct ZoneInfo
{
  uint32_t id;                // id
  char name[MAX_NAME_LENGTH]; // name
};

extern struct ZoneInfo* zone_info; 
extern struct ZoneInfo  zone_infos[];

#define LINUX_ZONE_INFO    (&zone_infos[0])
#define NPUCORE_ZONE_INFO  (&zone_infos[1])
#define RTTHREAD_ZONE_INFO (&zone_infos[2]) 
#define SEL4_ZONE_INFO     (&zone_infos[3]) 

#endif // _CONFIG_ZONE_H_