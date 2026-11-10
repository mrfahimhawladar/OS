// Minimal freestanding C++ kernel for the first Fahim OS milestone.
using u8 = unsigned char;
using u16 = unsigned short;
using u32 = unsigned int;

namespace {
constexpr u32 WIDTH = 80;
constexpr u32 HEIGHT = 25;
constexpr u8 WHITE_ON_BLACK = 0x0F;
volatile u16* const VGA = reinterpret_cast<volatile u16*>(0xB8000);

u32 column = 0;
u32 row = 0;

void clear_screen() {
    const u16 blank = static_cast<u16>((WHITE_ON_BLACK << 8) | ' ');
    for (u32 i = 0; i < WIDTH * HEIGHT; ++i) {
        VGA[i] = blank;
    }
    column = 0;
    row = 0;
}

void scroll_if_needed() {
    if (row < HEIGHT) return;

    for (u32 y = 1; y < HEIGHT; ++y) {
        for (u32 x = 0; x < WIDTH; ++x) {
            VGA[(y - 1) * WIDTH + x] = VGA[y * WIDTH + x];
        }
    }

    const u16 blank = static_cast<u16>((WHITE_ON_BLACK << 8) | ' ');
    for (u32 x = 0; x < WIDTH; ++x) {
        VGA[(HEIGHT - 1) * WIDTH + x] = blank;
    }
    row = HEIGHT - 1;
}

void put_char(char ch) {
    if (ch == '\n') {
        column = 0;
        ++row;
        scroll_if_needed();
        return;
    }

    VGA[row * WIDTH + column] =
        static_cast<u16>((WHITE_ON_BLACK << 8) | static_cast<u8>(ch));

    ++column;
    if (column >= WIDTH) {
        column = 0;
        ++row;
        scroll_if_needed();
    }
}

void print(const char* text) {
    for (u32 i = 0; text[i] != '\0'; ++i) {
        put_char(text[i]);
    }
}
} // namespace

extern "C" void kernel_main(u32 multiboot_magic, u32 multiboot_info) {
    (void)multiboot_info;

    clear_screen();
    print("========================================\n");
    print("             FAHIM OS                   \n");
    print("========================================\n\n");
    print("The C++ kernel has started successfully.\n\n");

    if (multiboot_magic == 0x2BADB002) {
        print("Boot protocol: Multiboot 1 - OK\n");
    } else {
        print("Warning: unexpected bootloader magic.\n");
    }

    print("\nMilestone 1: bootable kernel + VGA text output.\n");
    print("This is an early hobby OS, not a complete OS yet.\n\n");
    print("Next: keyboard input, interrupts, memory management,\n");
    print("and a basic command line.\n");

    for (;;) {
        asm volatile("hlt");
    }
}
