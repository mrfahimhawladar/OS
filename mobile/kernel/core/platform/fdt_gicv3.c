#include <stddef.h>
#include <stdint.h>
#include <limits.h>

#include "gicv3_platform.h"

#define FDT_MAGIC       0xD00DFEEDu
#define FDT_HEADER_SIZE 40u
#define FDT_BEGIN_NODE  1u
#define FDT_END_NODE    2u
#define FDT_PROP        3u
#define FDT_NOP         4u
#define FDT_END         9u
#define FDT_MAX_DEPTH   32u

typedef struct {
    const uint8_t *compatible;
    uint32_t compatible_length;
    const uint8_t *status;
    uint32_t status_length;
    const uint8_t *reg;
    uint32_t reg_length;
    uint32_t parent_address_cells;
    uint32_t parent_size_cells;
    uint32_t child_address_cells;
    uint32_t child_size_cells;
    uint32_t declared_redistributor_regions;
    uint64_t redistributor_stride;
    int has_redistributor_count;
    int has_redistributor_stride;
} fdt_node_t;

static uint32_t read_be32(const uint8_t *p) {
    return ((uint32_t)p[0] << 24) |
           ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8) |
           (uint32_t)p[3];
}

static const uint8_t *align4(const uint8_t *p) {
    return (const uint8_t *)(((uintptr_t)p + 3u) & ~(uintptr_t)3u);
}

static int range_is_valid(uint32_t offset, uint32_t length,
                          uint32_t total_size) {
    return offset <= total_size && length <= total_size - offset;
}

static int strings_name_equals(const char *strings, uint32_t strings_size,
                               uint32_t offset, const char *expected) {
    if (offset >= strings_size) return 0;

    const char *name = strings + offset;
    const uint32_t remaining = strings_size - offset;

    for (uint32_t i = 0u; i < remaining; ++i) {
        if (name[i] != expected[i]) return 0;
        if (expected[i] == '\0') return 1;
    }

    return 0;
}

static int compatible_list_contains(const uint8_t *list,
                                    uint32_t length,
                                    const char *expected) {
    if (list == NULL || length == 0u) return 0;

    uint32_t offset = 0u;
    while (offset < length) {
        const uint32_t remaining = length - offset;
        uint32_t entry_length = 0u;

        while (entry_length < remaining && list[offset + entry_length] != 0u) {
            ++entry_length;
        }
        if (entry_length == remaining) return 0;

        const char *candidate = (const char *)(list + offset);
        uint32_t i = 0u;
        while (i < entry_length && expected[i] != '\0') {
            if (candidate[i] != expected[i]) break;
            ++i;
        }
        if (i == entry_length && expected[i] == '\0') return 1;

        offset += entry_length + 1u;
    }

    return 0;
}

static int property_string_equals(const uint8_t *value, uint32_t length,
                                  const char *expected) {
    uint32_t i = 0u;
    while (expected[i] != '\0') {
        if (i >= length || value[i] != (uint8_t)expected[i]) return 0;
        ++i;
    }
    return i + 1u == length && value[i] == 0u;
}

static int node_is_enabled(const fdt_node_t *node) {
    if (node->status == NULL) return 1;
    return property_string_equals(node->status, node->status_length, "okay") ||
           property_string_equals(node->status, node->status_length, "ok");
}

static int read_cells(const uint8_t *value, uint32_t cells,
                      uint64_t *result) {
    if (cells == 1u) {
        *result = read_be32(value);
        return 0;
    }

    if (cells == 2u) {
        *result = ((uint64_t)read_be32(value) << 32) |
                  read_be32(value + 4u);
        return 0;
    }

    return -1;
}

static int append_gic_regions(const fdt_node_t *node,
                              gicv3_platform_info_t *info) {
    const uint32_t address_cells = node->parent_address_cells;
    const uint32_t size_cells = node->parent_size_cells;

    if ((address_cells != 1u && address_cells != 2u) ||
        (size_cells != 1u && size_cells != 2u) ||
        node->reg == NULL || node->reg_length == 0u) {
        return -2;
    }

    const uint32_t tuple_size = (address_cells + size_cells) * 4u;
    if (node->reg_length % tuple_size != 0u) return -2;

    const uint32_t tuple_count = node->reg_length / tuple_size;
    if (tuple_count < 2u) return -2;
    if (tuple_count - 1u > GICV3_MAX_REDISTRIBUTOR_REGIONS) return -5;

    const uint32_t redistributor_count = tuple_count - 1u;
    if (node->has_redistributor_count &&
        node->declared_redistributor_regions != redistributor_count) {
        return -4;
    }

    for (uint32_t i = 0u; i < tuple_count; ++i) {
        const uint8_t *tuple = node->reg + i * tuple_size;
        uint64_t address = 0u;
        uint64_t size = 0u;

        if (read_cells(tuple, address_cells, &address) != 0 ||
            read_cells(tuple + address_cells * 4u, size_cells, &size) != 0 ||
            size == 0u || address > UINT64_MAX - size) {
            return -2;
        }

        memory_region_t *region = i == 0u
            ? &info->distributor
            : &info->redistributors[i - 1u];

        region->base = address;
        region->size = size;
    }

    info->redistributor_region_count = redistributor_count;
    info->redistributor_stride = node->has_redistributor_stride
        ? node->redistributor_stride : 0u;

    return 0;
}

int gicv3_discover_from_fdt(const void *dtb,
                            gicv3_platform_info_t *info) {
    if (dtb == NULL || info == NULL) return -1;

    info->distributor.base = 0u;
    info->distributor.size = 0u;
    info->redistributor_region_count = 0u;
    info->redistributor_stride = 0u;
    for (size_t i = 0; i < GICV3_MAX_REDISTRIBUTOR_REGIONS; ++i) {
        info->redistributors[i].base = 0u;
        info->redistributors[i].size = 0u;
    }

    const uint8_t *base = (const uint8_t *)dtb;
    if (read_be32(base) != FDT_MAGIC) return -1;

    const uint32_t total_size = read_be32(base + 4u);
    const uint32_t struct_offset = read_be32(base + 8u);
    const uint32_t strings_offset = read_be32(base + 12u);
    const uint32_t version = read_be32(base + 20u);
    const uint32_t strings_size = read_be32(base + 32u);
    const uint32_t struct_size = read_be32(base + 36u);

    if (total_size < FDT_HEADER_SIZE || version < 17u ||
        (uintptr_t)base > UINTPTR_MAX - total_size ||
        !range_is_valid(struct_offset, struct_size, total_size) ||
        !range_is_valid(strings_offset, strings_size, total_size) ||
        struct_offset < FDT_HEADER_SIZE ||
        strings_offset < FDT_HEADER_SIZE) {
        return -1;
    }

    const uint32_t struct_end_offset = struct_offset + struct_size;
    const uint32_t strings_end_offset = strings_offset + strings_size;
    if (struct_offset < strings_end_offset &&
        strings_offset < struct_end_offset) {
        return -1;
    }

    const uint8_t *cursor = base + struct_offset;
    const uint8_t *struct_end = cursor + struct_size;
    const char *strings = (const char *)(base + strings_offset);

    fdt_node_t stack[FDT_MAX_DEPTH];
    uint32_t depth = 0u;
    int found_gic = 0;
    int found_end = 0;

    while ((size_t)(struct_end - cursor) >= 4u) {
        const uint32_t token = read_be32(cursor);
        cursor += 4u;

        if (token == FDT_BEGIN_NODE) {
            if (depth >= FDT_MAX_DEPTH) return -2;

            while (cursor < struct_end && *cursor != 0u) ++cursor;
            if (cursor >= struct_end) return -2;
            cursor = align4(cursor + 1u);
            if (cursor > struct_end) return -2;

            fdt_node_t node;
            node.compatible = NULL;
            node.compatible_length = 0u;
            node.status = NULL;
            node.status_length = 0u;
            node.reg = NULL;
            node.reg_length = 0u;
            node.parent_address_cells =
                depth == 0u ? 2u : stack[depth - 1u].child_address_cells;
            node.parent_size_cells =
                depth == 0u ? 2u : stack[depth - 1u].child_size_cells;
            node.child_address_cells = node.parent_address_cells;
            node.child_size_cells = node.parent_size_cells;
            node.declared_redistributor_regions = 0u;
            node.redistributor_stride = 0u;
            node.has_redistributor_count = 0;
            node.has_redistributor_stride = 0;
            stack[depth++] = node;
        } else if (token == FDT_END_NODE) {
            if (depth == 0u) return -2;

            const fdt_node_t *node = &stack[depth - 1u];
            if (node_is_enabled(node) &&
                compatible_list_contains(node->compatible,
                                          node->compatible_length,
                                          "arm,gic-v3")) {
                if (found_gic) return -4;
                const int result = append_gic_regions(node, info);
                if (result != 0) return result;
                found_gic = 1;
            }

            --depth;
        } else if (token == FDT_PROP) {
            if ((size_t)(struct_end - cursor) < 8u || depth == 0u) return -2;

            const uint32_t length = read_be32(cursor);
            const uint32_t name_offset = read_be32(cursor + 4u);
            cursor += 8u;

            if (length > (uint32_t)(struct_end - cursor) ||
                name_offset >= strings_size) {
                return -2;
            }

            fdt_node_t *node = &stack[depth - 1u];
            if (strings_name_equals(strings, strings_size,
                                    name_offset, "compatible")) {
                node->compatible = cursor;
                node->compatible_length = length;
            } else if (strings_name_equals(strings, strings_size,
                                           name_offset, "status")) {
                node->status = cursor;
                node->status_length = length;
            } else if (strings_name_equals(strings, strings_size,
                                           name_offset, "reg")) {
                node->reg = cursor;
                node->reg_length = length;
            } else if (strings_name_equals(strings, strings_size,
                                           name_offset, "#address-cells")) {
                if (length != 4u) return -2;
                node->child_address_cells = read_be32(cursor);
            } else if (strings_name_equals(strings, strings_size,
                                           name_offset, "#size-cells")) {
                if (length != 4u) return -2;
                node->child_size_cells = read_be32(cursor);
            } else if (strings_name_equals(strings, strings_size,
                                           name_offset, "#redistributor-regions")) {
                if (length != 4u) return -2;
                node->declared_redistributor_regions = read_be32(cursor);
                node->has_redistributor_count = 1;
            } else if (strings_name_equals(strings, strings_size,
                                           name_offset, "redistributor-stride")) {
                if (length == 4u) {
                    node->redistributor_stride = read_be32(cursor);
                } else if (length == 8u) {
                    node->redistributor_stride =
                        ((uint64_t)read_be32(cursor) << 32) |
                        read_be32(cursor + 4u);
                } else {
                    return -2;
                }
                if (node->redistributor_stride == 0u) return -2;
                node->has_redistributor_stride = 1;
            }

            cursor = align4(cursor + length);
            if (cursor > struct_end) return -2;
        } else if (token == FDT_NOP) {
            continue;
        } else if (token == FDT_END) {
            if (depth != 0u) return -2;
            found_end = 1;
            break;
        } else {
            return -2;
        }
    }

    if (!found_end) return -2;
    return found_gic ? 0 : -3;
}
