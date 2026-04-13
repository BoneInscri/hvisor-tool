// SPDX-License-Identifier: GPL-2.0-only
/**
 * acpi_builder.c
 *
 * 从 /sys/firmware/acpi/tables 读取所有 ACPI 表，
 * 为每张表（RSDP / XSDT / 每个子表）独立分配新的 HPA，
 * GPA 复用原始固件的物理地址（从 /sys/firmware/efi/systab 和表头解析），
 * 每对 HPA→GPA 追加一条 memory_region 到 zone_config。
 *
 * 最终 Guest 看到的 GPA 与原始固件完全一致，无需修改 Guest 配置。
 */

#include "acpi_builder.h"
#include "../include/hvisor.h"
#include "log.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <unistd.h>

/* ------------------------------------------------------------------ */
/* 类型定义                                                             */
/* ------------------------------------------------------------------ */

typedef struct {
    uint64_t signature;
    uint32_t revision;
    uint32_t header_size;
    uint32_t crc32;
    uint32_t reserved;
} efi_table_hdr_t;

typedef struct {
    uint32_t data1;
    uint16_t data2;
    uint16_t data3;
    uint8_t  data4[8];
} efi_guid_t;

typedef struct {
    efi_guid_t guid;
    uint64_t   table; /* GPA */
} efi_config_table_t;

typedef struct {
    efi_table_hdr_t hdr;
    uint64_t fw_vendor;
    uint32_t fw_revision;
    uint32_t _pad;
    uint64_t con_in_handle;
    uint64_t con_in;
    uint64_t con_out_handle;
    uint64_t con_out;
    uint64_t stderr_handle;
    uint64_t stderr_field;
    uint64_t runtime_services;
    uint64_t boot_services;
    uint64_t nr_tables;
    uint64_t tables; /* GPA of efi_config_table_t array */
} efi_system_table_t;

typedef struct {
    char     signature[8]; /* "RSD PTR " */
    uint8_t  checksum;
    char     oem_id[6];
    uint8_t  revision;
    uint32_t rsdt_address;
    uint32_t length;
    uint64_t xsdt_address;
    uint8_t  extended_checksum;
    uint8_t  reserved[3];
} __attribute__((packed)) acpi_rsdp_t;

typedef struct {
    char     signature[4];
    uint32_t length;
    uint8_t  revision;
    uint8_t  checksum;
    char     oem_id[6];
    char     oem_table_id[8];
    uint32_t oem_revision;
    char     asl_compiler_id[4];
    uint32_t asl_compiler_revision;
} __attribute__((packed)) acpi_table_hdr_t;

/* ------------------------------------------------------------------ */
/* 常量                                                                 */
/* ------------------------------------------------------------------ */

#define PAGE_SIZE_4K   0x1000ULL
#define ALIGN_4K(x)    (((uint64_t)(x) + PAGE_SIZE_4K - 1) & ~(PAGE_SIZE_4K - 1))
#define MAX_ACPI_TABLES 64

static const efi_guid_t ACPI_20_TABLE_GUID = {
    0x8868e871, 0xe4f1, 0x11d3,
    {0xbc, 0x22, 0x00, 0x80, 0xc7, 0x3c, 0x88, 0x81}
};

/* ------------------------------------------------------------------ */
/* 内部结构：一张 ACPI 表的完整信息                                      */
/* ------------------------------------------------------------------ */

typedef struct {
    char     sig[5];     /* null-terminated 4-char signature */
    uint8_t *data;       /* malloc'd table content */
    uint32_t size;
    uint64_t orig_gpa;   /* 原始固件中的 GPA */
    uint64_t new_hpa;    /* 新分配的 HPA */
} acpi_entry_t;

/* ------------------------------------------------------------------ */
/* 辅助函数                                                             */
/* ------------------------------------------------------------------ */

static void fix_checksum(uint8_t *table, uint32_t length, int offset) {
    uint8_t sum = 0;
    table[offset] = 0;
    for (uint32_t i = 0; i < length; i++)
        sum += table[i];
    table[offset] = (uint8_t)(0x100u - sum);
}

/* 读取单个文件到 malloc 缓冲区 */
static uint8_t *read_sysfs_table(const char *path, uint32_t *out_size) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (sz <= 0 || sz > 16 * 1024 * 1024) { fclose(f); return NULL; }
    uint8_t *buf = malloc((size_t)sz);
    if (!buf) { fclose(f); return NULL; }
    if ((long)fread(buf, 1, (size_t)sz, f) != sz) {
        free(buf); fclose(f); return NULL;
    }
    fclose(f);
    *out_size = (uint32_t)sz;
    return buf;
}

/* 从 /sys/firmware/efi/systab 解析 ACPI20 的 GPA */
static uint64_t read_rsdp_gpa_from_systab(void) {
    FILE *f = fopen("/sys/firmware/efi/systab", "r");
    if (!f) {
        log_error("acpi_builder: cannot open /sys/firmware/efi/systab");
        return 0;
    }
    char line[256];
    uint64_t gpa = 0;
    while (fgets(line, sizeof(line), f)) {
        if (strncmp(line, "ACPI20=", 7) == 0) {
            gpa = strtoull(line + 7, NULL, 16);
            break;
        }
    }
    fclose(f);
    return gpa;
}

/* 通过 HVISOR_ZONE_M_ALLOC 分配物理内存，返回 HPA，0 表示失败 */
static uint64_t alloc_phys(int dev_fd, uint64_t size) {
    kmalloc_info_t info = { .pa = 0, .size = size };
    if (ioctl(dev_fd, HVISOR_ZONE_M_ALLOC, &info) != 0) {
        perror("acpi_builder: HVISOR_ZONE_M_ALLOC");
        return 0;
    }
    return info.pa;
}

/* 通过 HVISOR_LOAD_IMAGE 把 buf 写到 hpa */
static int load_to_hpa(int dev_fd, const void *buf, uint64_t size, uint64_t hpa) {
    struct hvisor_load_image_args args = {
        .user_buffer = (uint64_t)(uintptr_t)buf,
        .size        = size,
        .load_paddr  = hpa,
    };
    if (ioctl(dev_fd, HVISOR_LOAD_IMAGE, &args) != 0) {
        perror("acpi_builder: HVISOR_LOAD_IMAGE");
        return -1;
    }
    return 0;
}

/* 追加一条 memory_region，HPA→GPA，失败返回 -1 */
static int append_region(zone_config_t *config,
                         uint64_t hpa, uint64_t gpa, uint64_t size) {
    if (config->num_memory_regions >= CONFIG_MAX_MEMORY_REGIONS) {
        log_error("acpi_builder: memory_regions full");
        return -1;
    }
    memory_region_t *mr = &config->memory_regions[config->num_memory_regions++];
    mr->type           = MEM_TYPE_RAM;
    mr->physical_start = hpa;
    mr->virtual_start  = gpa;
    mr->size           = ALIGN_4K(size);
    log_info("acpi_builder: region HPA=0x%llx GPA=0x%llx size=0x%llx",
             (unsigned long long)hpa,
             (unsigned long long)gpa,
             (unsigned long long)mr->size);
    return 0;
}

/* ------------------------------------------------------------------ */
/* 公开接口                                                             */
/* ------------------------------------------------------------------ */

int acpi_build_and_load(int dev_fd, const char *acpi_dir, zone_config_t *config)
{
    acpi_entry_t entries[MAX_ACPI_TABLES];
    int n = 0;
    int ret = -1;

    /* ---- Step 1: 从 systab 拿 RSDP 的原始 GPA ---- */
    uint64_t rsdp_gpa = read_rsdp_gpa_from_systab();
    if (!rsdp_gpa) {
        log_error("acpi_builder: failed to read RSDP GPA from systab");
        return -1;
    }
    log_info("acpi_builder: RSDP GPA = 0x%llx", (unsigned long long)rsdp_gpa);

    /* ---- Step 2: 读 RSDP 文件，解析 XSDT GPA ---- */
    char path[512];
    snprintf(path, sizeof(path), "%s/RSDP", acpi_dir);
    /* /sys/firmware/acpi/tables 里没有 RSDP 文件，需要从 FACP 等推导，
     * 但 RSDP 本身可以从 systab 地址通过 /dev/mem 读取。
     * 更简单的方式：直接读 XSDT 文件，从其头部拿 GPA 不可行（sysfs 不暴露 GPA）。
     * 因此：从 /proc/iomem 解析各表的原始 GPA。                          */

    /* ---- Step 2 (实际): 从 /proc/iomem 建立 sig→GPA 映射 ---- */
    /* /proc/iomem 格式示例:
     *   f9bb0000-f9bb0023 : ACPI Tables   (RSDP 在这里，但标签不含 sig)
     * 更可靠的方式：读 sysfs 表文件，从表头拿 signature，
     * 再从 XSDT 的 entry 数组拿每张子表的原始 GPA。
     * XSDT 自身的 GPA 从 RSDP 的 xsdt_address 字段读取。
     * RSDP 通过 /dev/mem 在 rsdp_gpa 处读取。                            */

    /* 打开 /dev/mem 读 RSDP 和 XSDT */
    int memfd = open("/dev/mem", O_RDONLY | O_SYNC);
    if (memfd < 0) {
        log_error("acpi_builder: cannot open /dev/mem: %s", strerror(errno));
        return -1;
    }

    /* 读 RSDP */
    acpi_rsdp_t rsdp_orig;
    {
        uint64_t aligned = rsdp_gpa & ~(PAGE_SIZE_4K - 1);
        uint64_t offset  = rsdp_gpa - aligned;
        uint64_t map_sz  = ALIGN_4K(offset + sizeof(acpi_rsdp_t));
        void *base = mmap(NULL, map_sz, PROT_READ, MAP_SHARED, memfd, (off_t)aligned);
        if (base == MAP_FAILED) {
            log_error("acpi_builder: mmap RSDP failed: %s", strerror(errno));
            close(memfd);
            return -1;
        }
        memcpy(&rsdp_orig, (uint8_t *)base + offset, sizeof(acpi_rsdp_t));
        munmap(base, map_sz);
    }
    log_info("acpi_builder: RSDP xsdt_address=0x%llx",
             (unsigned long long)rsdp_orig.xsdt_address);

    uint64_t xsdt_gpa = rsdp_orig.xsdt_address;
    if (!xsdt_gpa) {
        log_error("acpi_builder: RSDP has no XSDT address");
        close(memfd);
        return -1;
    }

    /* 读 XSDT 头部拿 length */
    uint32_t xsdt_len = 0;
    {
        uint64_t aligned = xsdt_gpa & ~(PAGE_SIZE_4K - 1);
        uint64_t offset  = xsdt_gpa - aligned;
        uint64_t map_sz  = ALIGN_4K(offset + sizeof(acpi_table_hdr_t));
        void *base = mmap(NULL, map_sz, PROT_READ, MAP_SHARED, memfd, (off_t)aligned);
        if (base == MAP_FAILED) {
            log_error("acpi_builder: mmap XSDT hdr failed: %s", strerror(errno));
            close(memfd);
            return -1;
        }
        acpi_table_hdr_t *hdr = (acpi_table_hdr_t *)((uint8_t *)base + offset);
        xsdt_len = hdr->length;
        munmap(base, map_sz);
    }

    /* 读完整 XSDT，提取子表 GPA 列表 */
    int n_xsdt_entries = 0;
    uint64_t child_gpas[MAX_ACPI_TABLES];
    {
        uint64_t aligned = xsdt_gpa & ~(PAGE_SIZE_4K - 1);
        uint64_t offset  = xsdt_gpa - aligned;
        uint64_t map_sz  = ALIGN_4K(offset + xsdt_len);
        void *base = mmap(NULL, map_sz, PROT_READ, MAP_SHARED, memfd, (off_t)aligned);
        if (base == MAP_FAILED) {
            log_error("acpi_builder: mmap XSDT full failed: %s", strerror(errno));
            close(memfd);
            return -1;
        }
        uint8_t *xsdt_raw = (uint8_t *)base + offset;
        n_xsdt_entries = (xsdt_len - sizeof(acpi_table_hdr_t)) / 8;
        if (n_xsdt_entries > MAX_ACPI_TABLES)
            n_xsdt_entries = MAX_ACPI_TABLES;
        uint64_t *ep = (uint64_t *)(xsdt_raw + sizeof(acpi_table_hdr_t));
        for (int i = 0; i < n_xsdt_entries; i++)
            child_gpas[i] = ep[i];
        munmap(base, map_sz);
    }
    close(memfd);

    log_info("acpi_builder: XSDT GPA=0x%llx len=%u entries=%d",
             (unsigned long long)xsdt_gpa, xsdt_len, n_xsdt_entries);

    /* ---- Step 3: 扫描 acpi_dir，按 sig 建立文件内容映射 ---- */
    /* 用 sig→data 的简单线性表 */
    typedef struct { char sig[8]; uint8_t *data; uint32_t size; } sig_map_t;
    sig_map_t sig_map[MAX_ACPI_TABLES * 2];
    int sig_map_n = 0;

    DIR *dir = opendir(acpi_dir);
    if (!dir) {
        log_error("acpi_builder: cannot open %s: %s", acpi_dir, strerror(errno));
        return -1;
    }
    struct dirent *ent;
    while ((ent = readdir(dir)) != NULL && sig_map_n < (int)(sizeof(sig_map)/sizeof(sig_map[0]))) {
        if (ent->d_name[0] == '.') continue;
        snprintf(path, sizeof(path), "%s/%s", acpi_dir, ent->d_name);
        struct stat st;
        if (stat(path, &st) != 0 || S_ISDIR(st.st_mode)) continue;
        uint32_t sz = 0;
        uint8_t *buf = read_sysfs_table(path, &sz);
        if (!buf || sz < sizeof(acpi_table_hdr_t)) { free(buf); continue; }
        strncpy(sig_map[sig_map_n].sig, ent->d_name, 7);
        sig_map[sig_map_n].sig[7] = '\0';
        sig_map[sig_map_n].data = buf;
        sig_map[sig_map_n].size = sz;
        sig_map_n++;
    }
    closedir(dir);

    /* 查找 sig_map 的辅助 lambda（用函数指针模拟） */
    #define FIND_SIG(s, out_data, out_size) do { \
        (out_data) = NULL; (out_size) = 0; \
        for (int _i = 0; _i < sig_map_n; _i++) { \
            if (strncmp(sig_map[_i].sig, (s), 4) == 0) { \
                (out_data) = sig_map[_i].data; \
                (out_size) = sig_map[_i].size; \
                break; \
            } \
        } \
    } while(0)

    /* ---- Step 4: 为每张子表分配 HPA，构造 entries[] ---- */
    /* 先处理 XSDT 的子表 */
    for (int i = 0; i < n_xsdt_entries; i++) {
        uint64_t cgpa = child_gpas[i];
        if (!cgpa) continue;

        /* 从 /dev/mem 读子表头部拿 signature */
        char sig[5] = {0};
        {
            int mfd = open("/dev/mem", O_RDONLY | O_SYNC);
            if (mfd < 0) goto err;
            uint64_t al = cgpa & ~(PAGE_SIZE_4K - 1);
            uint64_t of = cgpa - al;
            uint64_t ms = ALIGN_4K(of + sizeof(acpi_table_hdr_t));
            void *b = mmap(NULL, ms, PROT_READ, MAP_SHARED, mfd, (off_t)al);
            if (b != MAP_FAILED) {
                memcpy(sig, (uint8_t *)b + of, 4);
                munmap(b, ms);
            }
            close(mfd);
        }

        /* 从 sysfs 找对应文件内容 */
        uint8_t *tdata = NULL; uint32_t tsize = 0;
        FIND_SIG(sig, tdata, tsize);
        if (!tdata) {
            log_warn("acpi_builder: no sysfs file for sig '%.4s' GPA=0x%llx, skip",
                     sig, (unsigned long long)cgpa);
            continue;
        }

        /* 分配 HPA */
        uint64_t hpa = alloc_phys(dev_fd, ALIGN_4K(tsize));
        if (!hpa) goto err;

        /* 写入 */
        uint8_t *copy = malloc(ALIGN_4K(tsize));
        if (!copy) goto err;
        memset(copy, 0, ALIGN_4K(tsize));
        memcpy(copy, tdata, tsize);
        fix_checksum(copy, tsize, 9);
        int r = load_to_hpa(dev_fd, copy, ALIGN_4K(tsize), hpa);
        free(copy);
        if (r != 0) goto err;

        /* 记录 */
        acpi_entry_t *e = &entries[n++];
        memcpy(e->sig, sig, 4); e->sig[4] = '\0';
        e->data     = NULL; /* 已写入，不需要保留 */
        e->size     = tsize;
        e->orig_gpa = cgpa;
        e->new_hpa  = hpa;

        if (append_region(config, hpa, cgpa, ALIGN_4K(tsize)) != 0) goto err;
        log_info("acpi_builder: [child] %.4s GPA=0x%llx HPA=0x%llx",
                 sig, (unsigned long long)cgpa, (unsigned long long)hpa);
    }

    /* ---- Step 5: 处理 DSDT（FADT offset 0x8C 引用，GPA 可能不在 XSDT 里）---- */
    {
        uint8_t *facp_data = NULL; uint32_t facp_size = 0;
        FIND_SIG("FACP", facp_data, facp_size);
        if (facp_data && facp_size >= 0x94) {
            uint64_t dsdt_gpa = 0;
            memcpy(&dsdt_gpa, facp_data + 0x8C, 8);
            if (dsdt_gpa) {
                /* 检查是否已经在 entries 里 */
                int found = 0;
                for (int i = 0; i < n; i++)
                    if (entries[i].orig_gpa == dsdt_gpa) { found = 1; break; }
                if (!found) {
                    uint8_t *ddata = NULL; uint32_t dsize = 0;
                    FIND_SIG("DSDT", ddata, dsize);
                    if (ddata) {
                        uint64_t hpa = alloc_phys(dev_fd, ALIGN_4K(dsize));
                        if (!hpa) goto err;
                        uint8_t *copy = malloc(ALIGN_4K(dsize));
                        if (!copy) goto err;
                        memset(copy, 0, ALIGN_4K(dsize));
                        memcpy(copy, ddata, dsize);
                        fix_checksum(copy, dsize, 9);
                        int r = load_to_hpa(dev_fd, copy, ALIGN_4K(dsize), hpa);
                        free(copy);
                        if (r != 0) goto err;
                        acpi_entry_t *e = &entries[n++];
                        memcpy(e->sig, "DSDT", 4); e->sig[4] = '\0';
                        e->data = NULL; e->size = dsize;
                        e->orig_gpa = dsdt_gpa; e->new_hpa = hpa;
                        if (append_region(config, hpa, dsdt_gpa, ALIGN_4K(dsize)) != 0) goto err;
                        log_info("acpi_builder: [DSDT] GPA=0x%llx HPA=0x%llx",
                                 (unsigned long long)dsdt_gpa, (unsigned long long)hpa);
                    }
                }
            }
        }
    }

    /* ---- Step 6: 重建 XSDT，GPA 不变，HPA 新分配 ---- */
    {
        uint32_t new_xsdt_size = sizeof(acpi_table_hdr_t) + (uint32_t)n * 8;
        uint8_t *xsdt_buf = calloc(1, ALIGN_4K(new_xsdt_size));
        if (!xsdt_buf) goto err;

        /* 从 sysfs 拷贝原始 XSDT 头部字段 */
        uint8_t *orig_xsdt_data = NULL; uint32_t orig_xsdt_size = 0;
        FIND_SIG("XSDT", orig_xsdt_data, orig_xsdt_size);
        if (orig_xsdt_data)
            memcpy(xsdt_buf, orig_xsdt_data, sizeof(acpi_table_hdr_t));

        acpi_table_hdr_t *xhdr = (acpi_table_hdr_t *)xsdt_buf;
        memcpy(xhdr->signature, "XSDT", 4);
        xhdr->length = new_xsdt_size;

        /* 填入子表 GPA（GPA 不变，Guest 用 GPA 访问） */
        uint64_t *ep = (uint64_t *)(xsdt_buf + sizeof(acpi_table_hdr_t));
        for (int i = 0; i < n; i++)
            ep[i] = entries[i].orig_gpa;

        fix_checksum(xsdt_buf, new_xsdt_size, 9);

        uint64_t xsdt_hpa = alloc_phys(dev_fd, ALIGN_4K(new_xsdt_size));
        if (!xsdt_hpa) { free(xsdt_buf); goto err; }
        if (load_to_hpa(dev_fd, xsdt_buf, ALIGN_4K(new_xsdt_size), xsdt_hpa) != 0) {
            free(xsdt_buf); goto err;
        }
        free(xsdt_buf);

        if (append_region(config, xsdt_hpa, xsdt_gpa, ALIGN_4K(new_xsdt_size)) != 0) goto err;
        log_info("acpi_builder: [XSDT] GPA=0x%llx HPA=0x%llx",
                 (unsigned long long)xsdt_gpa, (unsigned long long)xsdt_hpa);
    }

    /* ---- Step 7: 重建 RSDP，GPA 不变，HPA 新分配 ---- */
    {
        acpi_rsdp_t new_rsdp;
        memcpy(&new_rsdp, &rsdp_orig, sizeof(acpi_rsdp_t));
        /* xsdt_address GPA 不变 */
        new_rsdp.xsdt_address = xsdt_gpa;
        new_rsdp.rsdt_address = 0;
        fix_checksum((uint8_t *)&new_rsdp, 20, 8);
        fix_checksum((uint8_t *)&new_rsdp, sizeof(acpi_rsdp_t), 32);

        uint64_t rsdp_hpa = alloc_phys(dev_fd, PAGE_SIZE_4K);
        if (!rsdp_hpa) goto err;

        uint8_t *rsdp_page = calloc(1, PAGE_SIZE_4K);
        if (!rsdp_page) goto err;
        memcpy(rsdp_page, &new_rsdp, sizeof(acpi_rsdp_t));
        int r = load_to_hpa(dev_fd, rsdp_page, PAGE_SIZE_4K, rsdp_hpa);
        free(rsdp_page);
        if (r != 0) goto err;

        if (append_region(config, rsdp_hpa, rsdp_gpa, PAGE_SIZE_4K) != 0) goto err;
        log_info("acpi_builder: [RSDP] GPA=0x%llx HPA=0x%llx",
                 (unsigned long long)rsdp_gpa, (unsigned long long)rsdp_hpa);

        /* 写入 arch_config */
        // config->arch_config.acpi_rsdp_gpa = rsdp_gpa;
        // log_info("acpi_builder: acpi_rsdp_gpa = 0x%llx",
        //          (unsigned long long)rsdp_gpa);
    }

    ret = 0;

err:
    for (int i = 0; i < sig_map_n; i++)
        free(sig_map[i].data);
    #undef FIND_SIG
    return ret;
}
