/* Loads an uncompressed Marmalade S3E image into guest memory and links its imports. */
#include "loader.h"
#include "emu.h"
#include "log.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define S3E_MAGIC 0x55334558u

static uint16_t rd16(const uint8_t *p) {
    return (uint16_t)(p[0] | (p[1] << 8));
}

static uint32_t rd32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) |
           ((uint32_t)p[3] << 24);
}

static bool range_ok(size_t size, uint32_t offset, uint32_t length) {
    return offset <= size && length <= size - offset;
}

bool s3e_image_load(const char *path, struct s3e_image *image) {
    memset(image, 0, sizeof(*image));
    FILE *file = fopen(path, "rb");
    if (!file) {
        log_error("unable to open %s", path);
        return false;
    }
    fseek(file, 0, SEEK_END);
    long size = ftell(file);
    fseek(file, 0, SEEK_SET);
    if (size < 76) {
        fclose(file);
        log_error("%s is too small for an S3E image", path);
        return false;
    }
    image->file_size = (size_t)size;
    image->file_data = malloc(image->file_size);
    if (!image->file_data || fread(image->file_data, 1, image->file_size, file) != image->file_size) {
        fclose(file);
        s3e_image_free(image);
        log_error("unable to read %s", path);
        return false;
    }
    fclose(file);

    const uint8_t *p = image->file_data;
    struct s3e_header *h = &image->header;
    h->ident = rd32(p + 0);
    h->version = rd32(p + 4);
    h->flags = rd16(p + 8);
    h->arch = rd16(p + 10);
    h->fixup_offset = rd32(p + 12);
    h->fixup_size = rd32(p + 16);
    h->code_offset = rd32(p + 20);
    h->code_file_size = rd32(p + 24);
    h->code_mem_size = rd32(p + 28);
    h->entry_offset = rd32(p + 40);
    h->config_offset = rd32(p + 44);
    h->config_size = rd32(p + 48);
    h->base_addr_orig = rd32(p + 52);

    if (h->ident != S3E_MAGIC) {
        log_error("%s is not an uncompressed S3E image (run the installer first)", path);
        s3e_image_free(image);
        return false;
    }
    if (!range_ok(image->file_size, h->fixup_offset, h->fixup_size) ||
        !range_ok(image->file_size, h->code_offset, h->code_file_size) ||
        !range_ok(image->file_size, h->config_offset, h->config_size) ||
        h->code_file_size > h->code_mem_size || h->entry_offset >= h->code_mem_size) {
        log_error("S3E header ranges are invalid");
        s3e_image_free(image);
        return false;
    }
    return true;
}

void s3e_image_free(struct s3e_image *image) {
    for (size_t i = 0; i < image->symbol_count; ++i) {
        free(image->symbols[i]);
    }
    free(image->symbols);
    free(image->file_data);
    memset(image, 0, sizeof(*image));
}

static bool parse_symbols(struct s3e_image *image) {
    const uint8_t *data = image->file_data;
    uint32_t pos = image->header.fixup_offset;
    uint32_t end = pos + image->header.fixup_size;
    while (pos + 8 <= end) {
        uint32_t type = rd32(data + pos);
        uint32_t size = rd32(data + pos + 4);
        if (size < 8 || size > end - pos) {
            return false;
        }
        if (type == 0) {
            uint16_t count = rd16(data + pos + 8);
            image->symbols = calloc(count, sizeof(char *));
            uint32_t cursor = pos + 10;
            for (uint16_t i = 0; i < count; ++i) {
                const char *start = (const char *)data + cursor;
                size_t len = strnlen(start, pos + size - cursor);
                if (cursor + len >= pos + size) {
                    return false;
                }
                image->symbols[i] = strdup(start);
                cursor += (uint32_t)len + 1;
            }
            image->symbol_count = count;
        }
        pos += size;
    }
    return image->symbol_count > 0;
}

bool s3e_image_map(struct s3e_image *image, uint32_t (*resolve)(const char *symbol)) {
    const struct s3e_header *h = &image->header;
    if (!parse_symbols(image)) {
        log_error("S3E import table is invalid");
        return false;
    }
    uint32_t base = h->base_addr_orig;
    uint32_t map_size = (h->code_mem_size + 0xfffu) & ~0xfffu;
    if (!emu_map_identity(base, map_size)) {
        return false;
    }
    uint8_t *mem = G2H(base);
    memcpy(mem, image->file_data + h->code_offset, h->code_file_size);
    memset(mem + h->code_file_size, 0, h->code_mem_size - h->code_file_size);

    /* The image is loaded at its preferred base, so internal relocations are no-ops. */
    const uint8_t *data = image->file_data;
    uint32_t pos = h->fixup_offset;
    uint32_t end = pos + h->fixup_size;
    size_t unresolved = 0;
    while (pos + 8 <= end) {
        uint32_t type = rd32(data + pos);
        uint32_t size = rd32(data + pos + 4);
        if (type == 2 || type == 3 || type == 4) {
            uint32_t count = rd32(data + pos + 8);
            uint32_t cursor = pos + 12;
            if (count > (size - 12) / 6) {
                return false;
            }
            for (uint32_t i = 0; i < count; ++i, cursor += 6) {
                uint32_t offset = ((uint32_t)rd16(data + cursor) << 16) | rd16(data + cursor + 2);
                uint16_t index = rd16(data + cursor + 4);
                if (offset > h->code_mem_size - 4 || index >= image->symbol_count) {
                    return false;
                }
                uint32_t addr = resolve(image->symbols[index]);
                if (!addr) {
                    unresolved++;
                }
                memcpy(mem + offset, &addr, sizeof(addr));
            }
        }
        pos += size;
    }
    if (unresolved) {
        log_error("%zu import slots could not be resolved", unresolved);
        return false;
    }
    image->base = base;
    image->entry = base + h->entry_offset;
    return true;
}
