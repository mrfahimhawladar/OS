#include <stddef.h>
#include <stdint.h>

#define FDT_MAGIC 0xD00DFEEDu
#define FDT_BEGIN_NODE 1u
#define FDT_END_NODE   2u
#define FDT_PROP       3u
#define FDT_NOP        4u
#define FDT_END        9u

#define PL011_DR   0x000u
#define PL011_FR   0x018u
#define PL011_IBRD 0x024u
#define PL011_FBRD 0x028u
#define PL011_LCRH 0x02Cu
#define PL011_CR   0x030u
#define PL011_IMSC 0x038u
#define PL011_ICR  0x044u

#define PL011_FR_TXFF (1u << 5)
#define PL011_CR_UARTEN (1u << 0)
#define PL011_CR_TXE (1u << 8)
#define PL011_CR_RXE (1u << 9)

static uintptr_t uart_base;

static uint32_t read_be32(const uint8_t *p) {
    return ((uint32_t)p[0] << 24) |
           ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8) |
           (uint32_t)p[3];
}

static int string_equals(const char *left, const char *right) {
    size_t i = 0;
    while (left[i] != '\0' && right[i] != '\0') {
        if (left[i] != right[i]) return 0;
        ++i;
    }
    return left[i] == '\0' && right[i] == '\0';
}

static int compatible_has_pl011(const char *list, uint32_t length) {
    uint32_t offset = 0;

    while (offset < length) {
        const char *entry = list + offset;
        uint32_t remaining = length - offset;
        uint32_t chars = 0;

        while (chars < remaining && entry[chars] != '\0') ++chars;
        if (chars == remaining) return 0;

        if (string_equals(entry, "arm,pl011") ||
            string_equals(entry, "arm,primecell")) {
            return 1;
        }
        offset += chars + 1;
    }
    return 0;
}

typedef struct {
    const uint8_t *reg;
    uint32_t reg_length;
    const char *compatible;
    uint32_t compatible_length;
    uint32_t parent_address_cells;
    uint32_t child_address_cells;
} node_state;

static uintptr_t find_pl011_from_fdt(const void *dtb) {
    if (dtb == NULL) return 0;

    const uint8_t *base = (const uint8_t *)dtb;
    if (read_be32(base) != FDT_MAGIC) return 0;

    const uint32_t total_size = read_be32(base + 4);
    const uint32_t struct_offset = read_be32(base + 8);
    const uint32_t strings_offset = read_be32(base + 12);
    const uint32_t strings_size = read_be32(base + 32);
    const uint32_t struct_size = read_be32(base + 36);

    if (total_size < 40 ||
        struct_offset >= total_size ||
        strings_offset >= total_size ||
        struct_size > total_size - struct_offset ||
        strings_size > total_size - strings_offset) {
        return 0;
    }

    const uint8_t *cursor = base + struct_offset;
    const uint8_t *struct_end = cursor + struct_size;
    const char *strings = (const char *)(base + strings_offset);

    node_state stack[16] = {{0}};
    uint32_t depth = 0;
    uint32_t root_address_cells = 2;

    while ((size_t)(struct_end - cursor) >= 4) {
        const uint32_t token = read_be32(cursor);
        cursor += 4;

        if (token == FDT_BEGIN_NODE) {
            if (depth >= 16) return 0;

            while (cursor < struct_end && *cursor != '\0') ++cursor;
            if (cursor >= struct_end) return 0;
            ++cursor;
            cursor = (const uint8_t *)(((uintptr_t)cursor + 3u) & ~(uintptr_t)3u);

            node_state node = {0};
            node.parent_address_cells =
                depth == 0 ? root_address_cells : stack[depth - 1].child_address_cells;
            node.child_address_cells = node.parent_address_cells;
            stack[depth++] = node;
        } else if (token == FDT_END_NODE) {
            if (depth == 0) return 0;

            node_state *node = &stack[depth - 1];
            if (node->compatible != NULL && node->reg != NULL &&
                compatible_has_pl011(node->compatible, node->compatible_length)) {
                uint64_t address = 0;
                if (node->parent_address_cells == 2 && node->reg_length >= 8) {
                    address = ((uint64_t)read_be32(node->reg) << 32) |
                              read_be32(node->reg + 4);
                } else if (node->parent_address_cells == 1 && node->reg_length >= 4) {
                    address = read_be32(node->reg);
                }
                if (address != 0 && address <= UINTPTR_MAX) return (uintptr_t)address;
            }
            --depth;
        } else if (token == FDT_PROP) {
            if ((size_t)(struct_end - cursor) < 8 || depth == 0) return 0;

            const uint32_t length = read_be32(cursor);
            const uint32_t name_offset = read_be32(cursor + 4);
            cursor += 8;

            if (length > (uint32_t)(struct_end - cursor) ||
                name_offset >= strings_size) {
                return 0;
            }

            const char *name = strings + name_offset;
            node_state *node = &stack[depth - 1];

            if (string_equals(name, "compatible")) {
                node->compatible = (const char *)cursor;
                node->compatible_length = length;
            } else if (string_equals(name, "reg")) {
                node->reg = cursor;
                node->reg_length = length;
            } else if (string_equals(name, "#address-cells") && length >= 4) {
                node->child_address_cells = read_be32(cursor);
            }

            cursor += length;
            cursor = (const uint8_t *)(((uintptr_t)cursor + 3u) & ~(uintptr_t)3u);
        } else if (token == FDT_NOP) {
            continue;
        } else if (token == FDT_END) {
            break;
        } else {
            return 0;
        }
    }

    return 0;
}

static void uart_write_register(uint32_t offset, uint32_t value) {
    volatile uint32_t *reg = (volatile uint32_t *)(uart_base + offset);
    *reg = value;
}

static uint32_t uart_read_register(uint32_t offset) {
    volatile uint32_t *reg = (volatile uint32_t *)(uart_base + offset);
    return *reg;
}

int console_init_from_fdt(const void *dtb) {
    uart_base = find_pl011_from_fdt(dtb);
    if (uart_base == 0) return -1;

    /* QEMU virt PL011 reference clock is 24 MHz; 115200 baud divisor = 13 + 1/64. */
    uart_write_register(PL011_CR, 0);
    uart_write_register(PL011_ICR, 0x7FF);
    uart_write_register(PL011_IBRD, 13);
    uart_write_register(PL011_FBRD, 1);
    uart_write_register(PL011_LCRH, 0x70); /* 8 data bits, FIFO enabled */
    uart_write_register(PL011_IMSC, 0);    /* Polling console for milestone 1 */
    uart_write_register(PL011_CR,
                        PL011_CR_UARTEN | PL011_CR_TXE | PL011_CR_RXE);
    return 0;
}

void console_putc(char character) {
    if (uart_base == 0) return;
    if (character == '\n') console_putc('\r');

    while ((uart_read_register(PL011_FR) & PL011_FR_TXFF) != 0) {
        __asm__ volatile("yield");
    }
    uart_write_register(PL011_DR, (uint32_t)(uint8_t)character);
}

void console_write(const char *text) {
    if (text == NULL) return;
    while (*text != '\0') console_putc(*text++);
}
