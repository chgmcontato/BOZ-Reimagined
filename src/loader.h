#ifndef CODBOZ_PC_LOADER_H
#define CODBOZ_PC_LOADER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

struct s3e_header {
    uint32_t ident;
    uint32_t version;
    uint16_t flags;
    uint16_t arch;
    uint32_t fixup_offset;
    uint32_t fixup_size;
    uint32_t code_offset;
    uint32_t code_file_size;
    uint32_t code_mem_size;
    uint32_t entry_offset;
    uint32_t config_offset;
    uint32_t config_size;
    uint32_t base_addr_orig;
};

struct s3e_image {
    uint8_t *file_data;
    size_t file_size;
    struct s3e_header header;
    char **symbols;
    size_t symbol_count;
    uint32_t base;
    uint32_t entry;
};

bool s3e_image_load(const char *path, struct s3e_image *image);
bool s3e_image_map(struct s3e_image *image, uint32_t (*resolve)(const char *symbol));
void s3e_image_free(struct s3e_image *image);

#endif
