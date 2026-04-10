#ifndef _CONFIG_COMMON_H_
#define _CONFIG_COMMON_H_

#define B  (1U)          /* 1 Byte   = 0x00000001 */
#define KB ((B) << 10)   /* 1 KB     = 0x00000400 (1024 Bytes) */
#define MB ((KB) << 10)  /* 1 MB     = 0x00100000 (1024 KB) */
#define GB ((MB) << 10)  /* 1 GB     = 0x40000000 (1024 MB) */

#define MEMORY_ALIGN_SIZE (4U)

#define MAX_NAME_LENGTH (20U)

#define INIT_MARK_INITIALIZED  (0xEEEEEEEEU) /* 已正确初始化 */
#define INIT_MARK_RAW_STATE    (0x00000000U) /* 未被初始化 */
#define INIT_MARK_INITIALIZING (0xAAAAAAAAU) /* 正在初始化 */
#define INIT_MARK_DESTORYED    (0xDDDDDDDDU) /* 初始化失败 */

#define MEM_DRIVE    ("/dev/mem")
#define HVISOR_DRIVE ("/dev/hvisor")

#define MEM_PAGE_SIZE (4096U)

#define MAX(a, b) ((a) > (b) ? (a) : (b))
#define MIN(a, b) ((a) < (b) ? (a) : (b))

#endif // _CONFIG_COMMON_H_