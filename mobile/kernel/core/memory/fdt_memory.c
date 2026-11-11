#include "memory.h"

#define FDT_MAGIC      0xD00DFEEDu
#define FDT_BEGIN_NODE 1u
#define FDT_END_NODE   2u
#define FDT_PROP       3u
#define FDT_NOP        4u
#define FDT_END        9u
#define FDT_MAX_DEPTH  16u
#define FDT_HEADER_SIZE 40u

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
    int is_reserved_parent;
    int is_reserved_child;
    int has_dynamic_size;
} fdt_node_state_t;

static uint32_t read_be32(const uint8_t *p) {
    return ((uint32_t)p[0] << 24) |
           ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8) |
           (uint32_t)p[3];
}

static uint64_t read_be64(const uint8_t *p) {
    return ((uint64_t)read_be32(p) << 32) | read_be32(p + 4);
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
    uint32_t expected_length = 0;
    while (expected[expected_length] != '\0') ++expected_length;
    if (length != expected_length + 1u) return 0;

    for (uint32_t i = 0; i < expected_length; ++i) {
        if ((char)value[i] != expected[i]) return 0;
    }
    return value[expected_length] == '\0';
}

static int node_name_is_reserved_memory(const uint8_t *name,
                                        uint32_t length) {
    static const char prefix[] = "reserved-memory";
    const uint32_t prefix_length = (uint32_t)(sizeof(prefix) - 1u);

    if (length < prefix_length) return 0;
    for (uint32_t i = 0; i < prefix_length; ++i) {
        if ((char)name[i] != prefix[i]) return 0;
    }
    return length == prefix_length || name[prefix_length] == '@';
}

static int read_cells(const uint8_t *value,
                      uint32_t cell_count,
                      uint64_t *result) {
    if (cell_count == 1u) {
        *result = read_be32(value);
        return 0;
    }

    if (cell_count == 2u) {
        *result = read_be64(value);
        return 0;
    }

    return -1;
}

static int node_is_enabled(const fdt_node_state_t *node) {
    if (node->status == NULL) return 1;
    return property_value_equals(node->status, node->status_length, "okay") ||
           property_value_equals(node->status, node->status_length, "ok");
}

static int append_region(memory_region_t *regions,
                         size_t capacity,
                         size_t *count,
                         uint64_t base,
                         uint64_t size) {
    if (size == 0u) return 0;
    if (base > UINT64_MAX - size) return -2;
    if (*count >= capacity) return -4;

    regions[*count].base = base;
    regions[*count].size = size;
    ++(*count);
    return 0;
}

static int append_reg_tuples(const uint8_t *reg,
                             uint32_t reg_length,
                             uint32_t address_cells,
                             uint32_t size_cells,
                             memory_region_t *regions,
                             size_t capacity,
                             size_t *count) {
    if ((address_cells != 1u && address_cells != 2u) ||
        (size_cells != 1u && size_cells != 2u)) {
        return -2;
    }

    const uint32_t tuple_cells = address_cells + size_cells;
    const uint32_t tuple_bytes = tuple_cells * 4u;
    if (reg == NULL || reg_length == 0u ||
        reg_length % tuple_bytes != 0u) {
        return -2;
    }

    for (uint32_t offset = 0; offset < reg_length; offset += tuple_bytes) {
        uint64_t region_base = 0;
        uint64_t region_size = 0;

        if (read_cells(reg + offset, address_cells, &region_base) != 0 ||
            read_cells(reg + offset + address_cells * 4u,
                       size_cells, &region_size) != 0) {
            return -2;
        }

        const int result = append_region(regions, capacity, count,
                                         region_base, region_size);
        if (result != 0) return result;
    }

    return 0;
}

static void sort_and_merge(memory_region_t *regions, size_t *count) {
    for (size_t i = 1; i < *count; ++i) {
        memory_region_t value = regions[i];
        size_t j = i;

        while (j > 0u && regions[j - 1u].base > value.base) {
            regions[j] = regions[j - 1u];
            --j;
        }
        regions[j] = value;
    }

    size_t output = 0;
    for (size_t i = 0; i < *count; ++i) {
        memory_region_t current = regions[i];
        if (current.size == 0u) continue;

        if (output == 0u) {
            regions[output++] = current;
            continue;
        }

        memory_region_t *previous = &regions[output - 1u];
        const uint64_t previous_end = previous->base + previous->size;
        const uint64_t current_end = current.base + current.size;

        if (current.base <= previous_end) {
            if (current_end > previous_end) {
                previous->size = current_end - previous->base;
            }
        } else {
            regions[output++] = current;
        }
    }

    *count = output;
}

static int append_fdt_reserve_map(const uint8_t *base,
                                  uint32_t total_size,
                                  uint32_t reserve_offset,
                                  uint32_t struct_offset,
                                  uint32_t struct_size,
                                  uint32_t strings_offset,
                                  uint32_t strings_size,
                                  memory_layout_t *layout) {
    if (reserve_offset < FDT_HEADER_SIZE || reserve_offset >= total_size ||
        (reserve_offset & 7u) != 0u) {
        return -2;
    }

    uint32_t cursor = reserve_offset;
    while (cursor <= total_size && total_size - cursor >= 16u) {
        const uint64_t address = read_be64(base + cursor);
        const uint64_t size = read_be64(base + cursor + 8u);

        if (address == 0u && size == 0u) return 0;

        const uint32_t entry_end = cursor + 16u;
        const uint32_t struct_end = struct_offset + struct_size;
        const uint32_t strings_end = strings_offset + strings_size;

        if ((cursor < struct_end && entry_end > struct_offset) ||
            (cursor < strings_end && entry_end > strings_offset)) {
            return -2;
        }

        const int result = append_region(layout->reserved,
                                         MEMORY_MAP_MAX_RESERVED_REGIONS,
                                         &layout->reserved_count,
                                         address, size);
        if (result != 0) return result;

        cursor = entry_end;
    }

    return -2;
}

static int append_reserved_node(const fdt_node_state_t *node,
                                memory_layout_t *layout) {
    if (!node->is_reserved_child || !node_is_enabled(node)) return 0;

    if (node->reg == NULL) {
        /* Dynamic reserved-memory needs a placement allocator; fail closed. */
        return node->has_dynamic_size ? -5 : 0;
    }

    return append_reg_tuples(node->reg, node->reg_length,
                              node->parent_address_cells,
                              node->parent_size_cells,
                              layout->reserved,
                              MEMORY_MAP_MAX_RESERVED_REGIONS,
                              &layout->reserved_count);
}

static int parse_fdt_nodes(const uint8_t *base,
                           uint32_t struct_offset,
                           uint32_t struct_size,
                           uint32_t strings_offset,
                           uint32_t strings_size,
                           memory_layout_t *layout) {
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

            const uint8_t *name = cursor;
            while (cursor < struct_end && *cursor != '\0') ++cursor;
            if (cursor >= struct_end) return -2;

            const uint32_t name_length = (uint32_t)(cursor - name);
            cursor = align4(cursor + 1);
            if (cursor > struct_end) return -2;

            fdt_node_state_t node;
            node.reg = NULL;
            node.reg_length = 0;
            node.status = NULL;
            node.status_length = 0;
            node.is_memory = 0;
            node.has_dynamic_size = 0;
            node.parent_address_cells =
                depth == 0u ? 2u : stack[depth - 1u].child_address_cells;
            node.parent_size_cells =
                depth == 0u ? 2u : stack[depth - 1u].child_size_cells;
            node.child_address_cells = node.parent_address_cells;
            node.child_size_cells = node.parent_size_cells;
            node.is_reserved_parent =
                depth == 1u &&
                node_name_is_reserved_memory(name, name_length);
            node.is_reserved_child =
                depth != 0u && stack[depth - 1u].is_reserved_parent;
            stack[depth++] = node;
        } else if (token == FDT_END_NODE) {
            if (depth == 0u) return -2;

            fdt_node_state_t *node = &stack[depth - 1u];
            int result = 0;

            if (node->is_memory && node_is_enabled(node)) {
                result = append_reg_tuples(
                    node->reg, node->reg_length,
                    node->parent_address_cells, node->parent_size_cells,
                    layout->ram, MEMORY_MAP_MAX_RAM_REGIONS,
                    &layout->ram_count);
            }

            if (result == 0) result = append_reserved_node(node, layout);
            if (result != 0) return result;
            --depth;
        } else if (token == FDT_PROP) {
            if ((size_t)(struct_end - cursor) < 8u || depth == 0u) {
                return -2;
            }

            const uint32_t length = read_be32(cursor);
            const uint32_t name_offset = read_be32(cursor + 4u);
            cursor += 8u;

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
                node->child_address_cells = read_be32(cursor);
            } else if (property_name_equals(strings, strings_size,
                                            name_offset, "#size-cells")) {
                if (length != 4u) return -2;
                node->child_size_cells = read_be32(cursor);
            } else if (property_name_equals(strings, strings_size,
                                            name_offset, "size")) {
                node->has_dynamic_size = length != 0u;
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

    return found_end ? 0 : -2;
}

static int build_usable_regions(memory_layout_t *layout) {
    sort_and_merge(layout->ram, &layout->ram_count);
    sort_and_merge(layout->reserved, &layout->reserved_count);

    for (size_t r = 0; r < layout->ram_count; ++r) {
        const uint64_t ram_start = layout->ram[r].base;
        const uint64_t ram_end = ram_start + layout->ram[r].size;
        uint64_t cursor = ram_start;

        for (size_t x = 0; x < layout->reserved_count; ++x) {
            const uint64_t reserved_start = layout->reserved[x].base;
            const uint64_t reserved_end =
                reserved_start + layout->reserved[x].size;

            if (reserved_end <= cursor) continue;
            if (reserved_start >= ram_end) break;

            if (reserved_start > cursor) {
                const int result = append_region(
                    layout->usable, MEMORY_MAP_MAX_USABLE_REGIONS,
                    &layout->usable_count, cursor, reserved_start - cursor);
                if (result != 0) return result;
            }

            if (reserved_end > cursor) cursor = reserved_end;
            if (cursor >= ram_end) break;
        }

        if (cursor < ram_end) {
            const int result = append_region(
                layout->usable, MEMORY_MAP_MAX_USABLE_REGIONS,
                &layout->usable_count, cursor, ram_end - cursor);
            if (result != 0) return result;
        }
    }

    return layout->usable_count == 0u ? -3 : 0;
}

int memory_layout_from_dtb(const void *dtb,
                           uintptr_t kernel_start,
                           uintptr_t kernel_end,
                           memory_layout_t *layout) {
    if (dtb == NULL || layout == NULL || kernel_start == 0u ||
        kernel_end <= kernel_start) {
        return -1;
    }

    layout->ram_count = 0;
    layout->reserved_count = 0;
    layout->usable_count = 0;

    const uint8_t *base = (const uint8_t *)dtb;
    if (read_be32(base) != FDT_MAGIC) return -1;

    const uint32_t total_size = read_be32(base + 4u);
    const uint32_t struct_offset = read_be32(base + 8u);
    const uint32_t strings_offset = read_be32(base + 12u);
    const uint32_t reserve_offset = read_be32(base + 16u);
    const uint32_t version = read_be32(base + 20u);
    const uint32_t strings_size = read_be32(base + 32u);
    const uint32_t struct_size = read_be32(base + 36u);

    if (total_size < FDT_HEADER_SIZE || version < 17u ||
        (uintptr_t)base > UINTPTR_MAX - total_size ||
        struct_offset > total_size || strings_offset > total_size ||
        struct_size > total_size - struct_offset ||
        strings_size > total_size - strings_offset) {
        return -1;
    }

    int result = append_fdt_reserve_map(
        base, total_size, reserve_offset,
        struct_offset, struct_size, strings_offset, strings_size, layout);
    if (result != 0) return result;

    result = parse_fdt_nodes(base, struct_offset, struct_size,
                             strings_offset, strings_size, layout);
    if (result != 0) return result;

    if (layout->ram_count == 0u) return -3;

    result = append_region(layout->reserved,
                           MEMORY_MAP_MAX_RESERVED_REGIONS,
                           &layout->reserved_count,
                           (uint64_t)kernel_start,
                           (uint64_t)(kernel_end - kernel_start));
    if (result != 0) return result;

    result = append_region(layout->reserved,
                           MEMORY_MAP_MAX_RESERVED_REGIONS,
                           &layout->reserved_count,
                           (uint64_t)(uintptr_t)dtb,
                           total_size);
    if (result != 0) return result;

    return build_usable_regions(layout);
}
