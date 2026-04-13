#ifndef __ACPI_BUILDER_H
#define __ACPI_BUILDER_H

#include "../include/zone_config.h"

/**
 * acpi_build_and_load - 从 acpi_tables_dir 读取所有 ACPI 表，
 *   GPA 复用原始固件地址（从 /sys/firmware/efi/systab + /dev/mem 解析），
 *   为每张表独立分配新的 HPA，通过 HVISOR_LOAD_IMAGE 写入，
 *   每对 HPA→GPA 追加一条 memory_region 到 config，
 *   并将 RSDP 的 GPA 写入 config->arch_config.acpi_rsdp_gpa。
 *
 * @param dev_fd     已打开的 /dev/hvisor fd
 * @param acpi_dir   ACPI 表目录，通常为 /sys/firmware/acpi/tables
 * @param config     zone_config，函数会追加 memory_regions 并填写 acpi_rsdp_gpa
 * @return 0 成功，-1 失败
 */
int acpi_build_and_load(int dev_fd, const char *acpi_dir, zone_config_t *config);

#endif /* __ACPI_BUILDER_H */
