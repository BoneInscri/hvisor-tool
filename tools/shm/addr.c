#include "shm/addr.h"
#include "cJSON.h"
#include "hvisor.h"
#include "shm/config/config_addr.h"
#include "virtio.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void parse_global_addr(char *shm_json_path) {
    (void)shm_json_path; /* 旧接口保留，实际解析已移至 hyperamp_linux_shm.c */
}