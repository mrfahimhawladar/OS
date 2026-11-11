#include "memory.h"

#define FDT_MAGIC      0xD00DFEEDu
#define FDT_BEGIN_NODE 1u
#define FDT_END_NODE   2u
#define FDT_PROP       3u
#define FDT_NOP        4u
#define FDT_END        9u
#define FDT_MAX_DEPTH  16u

typedef struct {
    const uint8_t *reg;
    uint32_t reg_length;
    const uint8_t *status;
    uint32_t status_length;
    uint32_t parent_address_cells;
    uint32_t parent_size_cells;
    uint32_t child_address_cells;
    uint32_t child_size_cells;
    int is_memory;
} fdt_node_state_t;

static uint32_t read_be32(const uint8_t *p) {
    return ((uint32_t)p[0] << 24) |
           ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8) |
           (uint32_t)p[3];
}

static const uint8_t *align4(const uint8_t *p) {
    return (const uint8_t *)(((uintptr_t)p + 3u) & ~(uintptr_t)3u);
}

static int property_name_equals(const char *strings,
                                uint32_t strings_size,
                                uint32_t name_offset,
                                const char *expected) {
    if (name_offset >= strings_size) return 0;

    const char *name = strings + name_offset;
    uint32_t remaining = strings_size - name_offset;
    uint32_t length = 0;

    while (length < remaining && name[length] != '\0') ++length;
    if (length == remaining) return 0;

    uint32_t i = 0;
    while (i < length && expected[i] != '\0') {
        if (name[i] != expected[i]) return 0;
        ++i;
    }
    return i == length && expected[i] == '\0';
}

static int property_value_equals(const uint8_t *value,
                                 uint32_t length,
                                 const char *expected) {
    uint32_t i = 0;

    while (i < length && expected[i] != '\0') {
        if ((char)value[i] != expected[i]) return 0;
        ++i;
    }

    return i + 1u == length && value[i] == '\0';
}

static int read_cells(const uint8_t *value,
                      uint32_t cell_count,
                      uint64_t *result) {
    if (cell_count == 1u) {
        *result = read_be32(value);
        return 0;
    }

    if (cell_count == 2u) {
        *result = ((uint64_t)read_be32(value) << 32) |
                  (uint64_t)read_be32(value + 4);
        return 0;
    }

    return -1;
}

static int node_is_enabled(const fdt_node_state_t *node) {
    if (node->status == NULL) return 1;
    return property_value_equals(node->status, node->status_length, "okay") ||
           property_value_equals(node->status, node->status_length, "ok");
}

static int append_memory_node(const fdt_node_state_t *node,
                              memory_region_t *regions,
                              size_t capacity,
                              size_t *count) {
    if (!node->is_memory || node->reg == NULL || !node_is_enabled(node)) {
        return 0;
    }

    const uint32_t address_cells = node->parent_address_cells;
    const uint32_t size_cells = node->parent_size_cells;
    if ((address_cells != 1u && address_cells != 2u) ||
        (size_cells != 1u && size_cells != 2u)) {
        return -2;
    }

    const uint32_t tuple_bytes = (address_cells + size_cells) * 4u;
    if (node->reg_length == 0u || node->reg_length % tuple_bytes != 0u) {
        return -2;
    }

    for (uint32_t offset = 0; offset < node->reg_length;
         offset += tuple_bytes) {
        uint64_t base = 0;
        uint64_t size = 0;

        if (read_cells(node->reg + offset, address_cells, &base) != 0 ||
            read_cells(node->reg + offset + address_cells * 4u,
                       size_cells, &size) != 0) {
            return -2;
        }

        if (size == 0u) continue;
        if (base > UINT64_MAX - size) return -2;
        if (*count >= capacity) return -4;

        regions[*count].base = base;
        regions[*count].size = size;
        ++(*count);
    }

    return 0;
}

int fdt_memory_map_from_dtb(const void *dtb,
                            memory_region_t *regions,
                            size_t capacity,
                            size_t *region_count) {
    if (dtb == NULL || regions == NULL || region_count == NULL ||
        capacity == 0u) {
        return -1;
    }

    *region_count = 0;

    const uint8_t *base = (const uint8_t *)dtb;
    if (read_be32(base) != FDT_MAGIC) return -1;

    const uint32_t total_size = read_be32(base + 4);
    const uint32_t struct_offset = read_be32(base + 8);
    const uint32_t strings_offset = read_be32(base + 12);
    const uint32_t strings_size = read_be32(base + 32);
    const uint32_t struct_size = read_be32(base + 36);

    if (total_size < 40u ||
        struct_offset > total_size ||
        strings_offset > total_size ||
        struct_size > total_size - struct_offset ||
        strings_size > total_size - strings_offset) {
        return -1;
    }

    const uint8_t *cursor = base + struct_offset;
    const uint8_t *struct_end = cursor + struct_size;
    const char *strings = (const char *)(base + strings_offset);

    fdt_node_state_t stack[FDT_MAX_DEPTH];
    uint32_t depth = 0;
    int found_end = 0;

    while ((size_t)(struct_end - cursor) >= 4u) {
        const uint32_t token = read_be32(cursor);
        cursor += 4;

        if (token == FDT_BEGIN_NODE) {
            if (depth >= FDT_MAX_DEPTH) return -2;

            while (cursor < struct_end && *cursor != '\0') ++cursor;
            if (cursor >= struct_end) return -2;
            cursor = align4(cursor + 1);
            if (cursor > struct_end) return -2;

            fdt_node_state_t node;
            node.reg = NULL;
            node.reg_length = 0;
            node.status = NULL;
            node.status_length = 0;
            node.is_memory = 0;
            node.parent_address_cells =
                depth == 0u ? 2u : stack[depth - 1u].child_address_cells;
            node.parent_size_cells =
                depth == 0u ? 2u : stack[depth - 1u].child_size_cells;
            node.child_address_cells = node.parent_address_cells;
            node.child_size_cells = node.parent_size_cells;
            stack[depth++] = node;
        } else if (token == FDT_END_NODE) {
            if (depth == 0u) return -2;

            const int result = append_memory_node(&stack[depth - 1u],
                                                  regions,
                                                  capacity,
                                                  region_count);
            if (result != 0) return result;
            --depth;
        } else if (token == FDT_PROP) {
            if ((size_t)(struct_end - cursor) < 8u || depth == 0u) {
                return -2;
            }

            const uint32_t length = read_be32(cursor);
            const uint32_t name_offset = read_be32(cursor + 4);
            cursor += 8;

            if (length > (uint32_t)(struct_end - cursor) ||
                name_offset >= strings_size) {
                return -2;
            }

            fdt_node_state_t *node = &stack[depth - 1u];
            if (property_name_equals(strings, strings_size,
                                     name_offset, "device_type")) {
                node->is_memory = property_value_equals(cursor, length,
                                                        "memory");
            } else if (property_name_equals(strings, strings_size,
                                            name_offset, "reg")) {
                node->reg = cursor;
                node->reg_length = length;
            } else if (property_name_equals(strings, strings_size,
                                            name_offset, "status")) {
                node->status = cursor;
                node->status_length = length;
            } else if (property_name_equals(strings, strings_size,
                                            name_offset, "#address-cells")) {
                if (length != 4u) return -2;
                /* Other buses may legally declare zero address cells. */
                node->child_address_cells = read_be32(cursor);
            } else if (property_name_equals(strings, strings_size,
                                            name_offset, "#size-cells")) {
                if (length != 4u) return -2;
                /* Validate supported cell counts only on actual RAM nodes. */
                node->child_size_cells = read_be32(cursor);
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
    return *region_count == 0u ? -3 : 0;
}
