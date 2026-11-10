// Freestanding C++ kernel: VGA text console + polled PS/2 keyboard shell.
using u8 = unsigned char;
using u16 = unsigned short;
using u32 = unsigned int;

namespace {
constexpr u32 WIDTH = 80;
constexpr u32 HEIGHT = 25;
constexpr u8 WHITE_ON_BLACK = 0x0F;
constexpr u32 MAX_COMMAND = 127;

volatile u16* const VGA = reinterpret_cast<volatile u16*>(0xB8000);
u32 column = 0;
u32 row = 0;
bool shift_pressed = false;

static inline u8 inb(u16 port) {
    u8 value;
    asm volatile("inb %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

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

void erase_last_character() {
    if (column == 0) {
        if (row == 0) return;
        --row;
        column = WIDTH;
    }
    --column;
    VGA[row * WIDTH + column] =
        static_cast<u16>((WHITE_ON_BLACK << 8) | ' ');
}

bool equals(const char* left, const char* right) {
    u32 i = 0;
    while (left[i] != '\0' && right[i] != '\0') {
        if (left[i] != right[i]) return false;
        ++i;
    }
    return left[i] == '\0' && right[i] == '\0';
}

bool starts_with(const char* value, const char* prefix) {
    u32 i = 0;
    while (prefix[i] != '\0') {
        if (value[i] != prefix[i]) return false;
        ++i;
    }
    return true;
}

char translate_scancode(u8 code, bool shift) {
    char value = 0;
    switch (code) {
        case 0x02: value = shift ? '!' : '1'; break;
        case 0x03: value = shift ? '@' : '2'; break;
        case 0x04: value = shift ? '#' : '3'; break;
        case 0x05: value = shift ? '$' : '4'; break;
        case 0x06: value = shift ? '%' : '5'; break;
        case 0x07: value = shift ? '^' : '6'; break;
        case 0x08: value = shift ? '&' : '7'; break;
        case 0x09: value = shift ? '*' : '8'; break;
        case 0x0A: value = shift ? '(' : '9'; break;
        case 0x0B: value = shift ? ')' : '0'; break;
        case 0x0C: value = shift ? '_' : '-'; break;
        case 0x0D: value = shift ? '+' : '='; break;
        case 0x10: value = 'q'; break;
        case 0x11: value = 'w'; break;
        case 0x12: value = 'e'; break;
        case 0x13: value = 'r'; break;
        case 0x14: value = 't'; break;
        case 0x15: value = 'y'; break;
        case 0x16: value = 'u'; break;
        case 0x17: value = 'i'; break;
        case 0x18: value = 'o'; break;
        case 0x19: value = 'p'; break;
        case 0x1A: value = shift ? '{' : '['; break;
        case 0x1B: value = shift ? '}' : ']'; break;
        case 0x1E: value = 'a'; break;
        case 0x1F: value = 's'; break;
        case 0x20: value = 'd'; break;
        case 0x21: value = 'f'; break;
        case 0x22: value = 'g'; break;
        case 0x23: value = 'h'; break;
        case 0x24: value = 'j'; break;
        case 0x25: value = 'k'; break;
        case 0x26: value = 'l'; break;
        case 0x27: value = shift ? ':' : ';'; break;
        case 0x28: value = shift ? '"' : '\''; break;
        case 0x29: value = shift ? '~' : '`'; break;
        case 0x2B: value = shift ? '|' : '\\'; break;
        case 0x2C: value = 'z'; break;
        case 0x2D: value = 'x'; break;
        case 0x2E: value = 'c'; break;
        case 0x2F: value = 'v'; break;
        case 0x30: value = 'b'; break;
        case 0x31: value = 'n'; break;
        case 0x32: value = 'm'; break;
        case 0x33: value = shift ? '<' : ','; break;
        case 0x34: value = shift ? '>' : '.'; break;
        case 0x35: value = shift ? '?' : '/'; break;
        case 0x39: value = ' '; break;
        default: return 0;
    }

    if (shift && value >= 'a' && value <= 'z') {
        value = static_cast<char>(value - 'a' + 'A');
    }
    return value;
}

void show_prompt() {
    print("fahim> ");
}

void execute_command(const char* command) {
    if (equals(command, "")) {
        return;
    }

    if (equals(command, "help")) {
        print("Commands: help, clear, about, version, echo <text>\n");
    } else if (equals(command, "clear")) {
        clear_screen();
    } else if (equals(command, "about")) {
        print("Fahim OS is an experimental hobby operating system.\n");
        print("Kernel: freestanding C++ | Target: 32-bit x86\n");
    } else if (equals(command, "version")) {
        print("Fahim OS 0.2 - Keyboard Shell milestone\n");
    } else if (starts_with(command, "echo ")) {
        print(command + 5);
        put_char('\n');
    } else {
        print("Unknown command: ");
        print(command);
        print("\nType 'help' to see available commands.\n");
    }
}

void shell() {
    char command[MAX_COMMAND + 1];
    u32 length = 0;

    show_prompt();

    for (;;) {
        // Read one byte from the legacy PS/2 keyboard controller when available.
        if ((inb(0x64) & 0x01) == 0) {
            asm volatile("pause");
            continue;
        }

        const u8 code = inb(0x60);

        if (code == 0x2A || code == 0x36) {
            shift_pressed = true;
            continue;
        }
        if (code == 0xAA || code == 0xB6) {
            shift_pressed = false;
            continue;
        }

        // Ignore key-release codes.
        if (code & 0x80) continue;

        if (code == 0x1C) { // Enter
            put_char('\n');
            command[length] = '\0';
            execute_command(command);
            length = 0;
            show_prompt();
            continue;
        }

        if (code == 0x0E) { // Backspace
            if (length > 0) {
                --length;
                command[length] = '\0';
                erase_last_character();
            }
            continue;
        }

        const char character = translate_scancode(code, shift_pressed);
        if (character != 0 && length < MAX_COMMAND) {
            command[length++] = character;
            command[length] = '\0';
            put_char(character);
        }
    }
}
} // namespace

extern "C" void kernel_main(u32 multiboot_magic, u32 multiboot_info) {
    (void)multiboot_info;

    clear_screen();
    print("========================================\n");
    print("             FAHIM OS                   \n");
    print("========================================\n\n");

    if (multiboot_magic == 0x2BADB002) {
        print("Boot protocol: Multiboot 1 - OK\n");
    } else {
        print("Warning: unexpected bootloader magic.\n");
    }

    print("C++ kernel started. Keyboard shell is ready.\n");
    print("Type 'help' and press Enter.\n\n");

    shell();
}
