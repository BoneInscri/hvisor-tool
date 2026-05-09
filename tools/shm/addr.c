#include "cJSON.h"
#include "virtio.h"
#include "hvisor.h"
#include "shm/addr.h"
#include "shm/config/config_addr.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

void parse_global_addr(char *shm_json_path) {
    (void)shm_json_path; /* 旧接口保留，实际解析已移至 hyperamp_linux_shm.c */
}