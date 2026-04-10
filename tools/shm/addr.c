#include "cJSON.h"
#include "virtio.h"
#include "hvisor.h"
#include "shm/addr.h"
#include "shm/config/config_addr.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

void parse_global_addr(char *shm_json_path) {
//   char* buffer_shm = open_json_file(shm_json_path);
//   if(buffer_shm == NULL) {
//     goto err_out;
//   }

//   // parse ram JSON
//   cJSON *root_shm = cJSON_Parse(buffer_shm);
//   cJSON *shm_regions_json = cJSON_GetObjectItem(root_shm, "shm_regions");
//   CHECK_JSON_NULL_ERR_OUT(shm_regions_json, "shm_regions");

//   // set addr array
//   int num_memory_regions_shm = cJSON_GetArraySize(shm_regions_json);

//   unsigned long long zone0_ram_ipa;// zone0_ram_ipa = zone0_ram_hpa
//   unsigned long long zonex_ram_ipa;// defined in zonex 
//   unsigned long long mem_size;
//   printf("[Trace] parse_global_addr, num_memory_regions_shm: %d\n", num_memory_regions_shm);
//   for (int j = 0; j < num_memory_regions_shm; j++) {
//     cJSON *shm_region = cJSON_GetArrayItem(shm_regions_json, j);

//     char *region_flag = cJSON_GetObjectItem(shm_region, "region_flag")->valuestring;      
//     zone0_ram_ipa = strtoull(cJSON_GetObjectItem(shm_region, "zone0_ram_ipa")->valuestring, NULL, 16);
//     zonex_ram_ipa = strtoull(cJSON_GetObjectItem(shm_region, "zonex_ram_ipa")->valuestring, NULL, 16);
    
//     mem_size = strtoull(cJSON_GetObjectItem(shm_region, "mem_size")->valuestring, NULL, 16);
//     if (mem_size == 0) {
//       printf("[WARN] parse_global_addr, invalid memory size");
//       continue;
//     }
//     if (!strcmp(region_flag, "linux-2-npucore-buf")) {
//       addr_infos[0].start = zone0_ram_ipa;
//       addr_infos[0].len = mem_size;
//       printf("[Attention] linux-2-npucore-buf: start: 0x%llx, len: 0x%llx\n", addr_infos[0].start, addr_infos[0].len);
//     } else if(!strcmp(region_flag, "linux-2-npucore-msg")) {
//       // special for msg queue (sender and receiver)
//       addr_infos[1].start = zone0_ram_ipa;
//       addr_infos[1].len = mem_size;
//       addr_infos[2].start = zonex_ram_ipa;
//       addr_infos[2].len = mem_size;
//       printf("[Attention] linux-2-npucore-msg: zone0_ram_ipa: 0x%llx, len: 0x%llx, zonex_ram_ipa: 0x%llx, len: 0x%llx\n", 
//          addr_infos[1].start, addr_infos[1].len,
//          addr_infos[2].start, addr_infos[2].len
//        );
//     } else {
//       printf("[Error] parse_global_addr: Invalid region flag: %s\n", region_flag);
//       while(1) {}
//     }
//   }
//   printf("[Trace] parse_global_addr success\n");
//   return;

// err_out:
//   printf("[Error] shm_json open fail, check it\n");
//   while(1) {}
}