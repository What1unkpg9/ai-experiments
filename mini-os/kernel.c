/* kernel.c */
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

/* ============================================================
 *  VGA Text Mode Driver (0xB8000)
 * ============================================================ */
#define VGA_WIDTH 80
#define VGA_HEIGHT 25
#define VGA_MEMORY ((volatile uint16_t*)0xB8000)

static uint8_t  vga_color = 0x0F;
static size_t   vga_row = 0;
static size_t   vga_col = 0;

static inline uint16_t vga_entry(char c, uint8_t color) {
    return (uint16_t)c | ((uint16_t)color << 8);
}

void vga_clear(void) {
    for (size_t y = 0; y < VGA_HEIGHT; y++)
        for (size_t x = 0; x < VGA_WIDTH; x++)
            VGA_MEMORY[y * VGA_WIDTH + x] = vga_entry(' ', vga_color);
    vga_row = 0; vga_col = 0;
}

void vga_scroll_up(void) {
    for (size_t y = 1; y < VGA_HEIGHT; y++)
        for (size_t x = 0; x < VGA_WIDTH; x++)
            VGA_MEMORY[(y-1) * VGA_WIDTH + x] = VGA_MEMORY[y * VGA_WIDTH + x];
    for (size_t x = 0; x < VGA_WIDTH; x++)
        VGA_MEMORY[(VGA_HEIGHT-1) * VGA_WIDTH + x] = vga_entry(' ', vga_color);
    vga_row = VGA_HEIGHT - 1;
}

void vga_putchar(char c) {
    if (c == '\n') {
        vga_col = 0;
        if (++vga_row >= VGA_HEIGHT) vga_scroll_up();
    } else if (c == '\b') { /* Backspace */
        if (vga_col > 0) {
            vga_col--;
            VGA_MEMORY[vga_row * VGA_WIDTH + vga_col] = vga_entry(' ', vga_color);
        }
    } else {
        VGA_MEMORY[vga_row * VGA_WIDTH + vga_col] = vga_entry(c, vga_color);
        if (++vga_col >= VGA_WIDTH) {
            vga_col = 0;
            if (++vga_row >= VGA_HEIGHT) vga_scroll_up();
        }
    }
}

void vga_puts(const char* s) {
    while (*s) vga_putchar(*s++);
}

void vga_puthex(uint32_t val) {
    char buf[9];
    for (int i = 7; i >= 0; i--) {
        uint8_t nib = val & 0xF;
        buf[i] = (nib < 10) ? '0' + nib : 'A' + nib - 10;
        val >>= 4;
    }
    buf[8] = 0;
    vga_puts("0x"); vga_puts(buf);
}

void vga_putdec(uint32_t val) {
    char buf[11];
    int i = 10; buf[i] = 0;
    if (val == 0) { vga_putchar('0'); return; }
    while (val > 0 && i >= 0) {
        buf[--i] = '0' + (val % 10);
        val /= 10;
    }
    vga_puts(&buf[i]);
}

/* ============================================================
 *  Port I/O (inline asm)
 * ============================================================ */
static inline void outb(uint16_t port, uint8_t val) {
    asm volatile ("outb %0, %1" :: "a"(val), "Nd"(port));
}
static inline uint8_t inb(uint16_t port) {
    uint8_t ret;
    asm volatile ("inb %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}
static inline void io_wait(void) { outb(0x80, 0); }

/* ============================================================
 *  GDT / IDT Structures
 * ============================================================ */
struct idt_entry {
    uint16_t base_low;
    uint16_t sel;
    uint8_t  always0;
    uint8_t  flags;
    uint16_t base_high;
} __attribute__((packed));

struct idt_ptr {
    uint16_t limit;
    uint32_t base;
} __attribute__((packed));

struct registers {
    uint32_t edi, esi, ebp, esp_dummy, ebx, edx, ecx, eax;
    uint32_t int_no, err_code;
    uint32_t eip, cs, eflags, useresp, ss;
};

#define IDT_ENTRIES 256
static struct idt_entry idt[IDT_ENTRIES];
static struct idt_ptr idtp;

extern void load_idt(struct idt_ptr*);
extern void isr_stub_0();  /* Forward declarations for stubs addresses */
extern void irq_stub_0();

/* ------------------------------------------------------------
 *  IDT Gate Setter
 * ------------------------------------------------------------ */
static void idt_set_gate(uint8_t num, uint32_t base, uint16_t sel, uint8_t flags) {
    idt[num].base_low  = base & 0xFFFF;
    idt[num].base_high = (base >> 16) & 0xFFFF;
    idt[num].sel       = sel;
    idt[num].always0   = 0;
    idt[num].flags     = flags;
}

/* ------------------------------------------------------------
 *  Exception / ISR Handler (C)
 * ------------------------------------------------------------ */
static const char* exception_messages[] = {
    "Division By Zero", "Debug", "Non Maskable Interrupt", "Breakpoint",
    "Into Detected Overflow", "Out of Bounds", "Invalid Opcode", "No Coprocessor",
    "Double Fault", "Coprocessor Segment Overrun", "Bad TSS", "Segment Not Present",
    "Stack Fault", "General Protection Fault", "Page Fault", "Unknown Interrupt",
    "Coprocessor Fault", "Alignment Check", "Machine Check", "SIMD Exception",
    "Virtualization Exception", "Control Protection Exception"
};

void isr_handler(struct registers* r) {
    vga_puts("\n[EXCEPTION] ");
    if (r->int_no < 22) vga_puts(exception_messages[r->int_no]);
    else vga_puts("Unknown");
    vga_puts(" (Vector: "); vga_putdec(r->int_no); vga_puts(")");
    if (r->err_code) { vga_puts(" Error Code: "); vga_puthex(r->err_code); }
    vga_puts("\nSystem Halted.\n");
    for(;;) asm volatile("hlt");
}

/* ============================================================
 *  PIC Remapping & IRQ Handling
 * ============================================================ */
#define PIC1_CMD 0x20
#define PIC1_DATA 0x21
#define PIC2_CMD 0xA0
#define PIC2_DATA 0xA1
#define PIC_EOI 0x20

#define IRQ_BASE 0x20

void pic_remap(void) {
    uint8_t mask1 = inb(PIC1_DATA);
    uint8_t mask2 = inb(PIC2_DATA);

    outb(PIC1_CMD, 0x11); io_wait();
    outb(PIC2_CMD, 0x11); io_wait();

    outb(PIC1_DATA, IRQ_BASE);     io_wait(); /* Master offset 0x20 */
    outb(PIC2_DATA, IRQ_BASE + 8); io_wait(); /* Slave offset 0x28 */

    outb(PIC1_DATA, 0x04); io_wait(); /* Master has slave on IRQ2 */
    outb(PIC2_DATA, 0x02); io_wait(); /* Slave cascade identity */

    outb(PIC1_DATA, 0x01); io_wait(); /* 8086 mode */
    outb(PIC2_DATA, 0x01); io_wait();

    outb(PIC1_DATA, mask1); /* Restore masks */
    outb(PIC2_DATA, mask2);
}

void pic_send_eoi(uint8_t irq) {
    if (irq >= 8) outb(PIC2_CMD, PIC_EOI);
    outb(PIC1_CMD, PIC_EOI);
}

/* ------------------------------------------------------------
 *  IRQ Handler (C) -> Dispatch
 * ------------------------------------------------------------ */
extern void keyboard_callback(uint8_t scancode); /* Forward decl */

void irq_handler(struct registers* r) {
    uint8_t irq = r->int_no - IRQ_BASE;

    switch (irq) {
        case 1: /* Keyboard */
            keyboard_callback(inb(0x60));
            break;
        case 0: /* Timer - ignore for now */
            break;
        default:
            break;
    }
    pic_send_eoi(irq);
}

/* ============================================================
 *  Keyboard Driver (Scan Code Set 1)
 * ============================================================ */
#define KB_BUFFER_SIZE 256
static char kb_buffer[KB_BUFFER_SIZE];
static uint32_t kb_head = 0, kb_tail = 0;

static bool shift_pressed = false;
static bool caps_lock = false;

/* Scancode -> ASCII Map (No Shift / Shift) */
static const char keymap[128][2] = {
    [0x01] = {'\033', '\033'},       // ESC
    [0x02] = {'1', '!'}, [0x03] = {'2', '@'}, [0x04] = {'3', '#'}, [0x05] = {'4', '$'},
    [0x06] = {'5', '%'}, [0x07] = {'6', '^'}, [0x08] = {'7', '&'}, [0x09] = {'8', '*'},
    [0x0A] = {'9', '('}, [0x0B] = {'0', ')'}, [0x0C] = {'-', '_'}, [0x0D] = {'=', '+'},
    [0x0E] = {'\b', '\b'},           // Backspace
    [0x0F] = {'\t', '\t'},           // Tab
    [0x10] = {'q', 'Q'}, [0x11] = {'w', 'W'}, [0x12] = {'e', 'E'}, [0x13] = {'r', 'R'},
    [0x14] = {'t', 'T'}, [0x15] = {'y', 'Y'}, [0x16] = {'u', 'U'}, [0x17] = {'i', 'I'},
    [0x18] = {'o', 'O'}, [0x19] = {'p', 'P'}, [0x1A] = {'[', '{'}, [0x1B] = {']', '}'},
    [0x1C] = {'\n', '\n'},           // Enter
    [0x1D] = {0, 0},                 // L_Ctrl
    [0x1E] = {'a', 'A'}, [0x1F] = {'s', 'S'}, [0x20] = {'d', 'D'}, [0x21] = {'f', 'F'},
    [0x22] = {'g', 'G'}, [0x23] = {'h', 'H'}, [0x24] = {'j', 'J'}, [0x25] = {'k', 'K'},
    [0x26] = {'l', 'L'}, [0x27] = {';', ':'}, [0x28] = {'\'', '"'}, [0x29] = {'`', '~'},
    [0x2A] = {0, 0},                 // L_Shift
    [0x2B] = {'\\', '|'},
    [0x2C] = {'z', 'Z'}, [0x2D] = {'x', 'X'}, [0x2E] = {'c', 'C'}, [0x2F] = {'v', 'V'},
    [0x30] = {'b', 'B'}, [0x31] = {'n', 'N'}, [0x32] = {'m', 'M'},
    [0x33] = {',', '<'}, [0x34] = {'.', '>'}, [0x35] = {'/', '?'},
    [0x36] = {0, 0},                 // R_Shift
    [0x37] = {'*', '*'},             // KP *
    [0x38] = {0, 0},                 // L_Alt
    [0x39] = {' ', ' '},             // Space
    [0x3A] = {0, 0},                 // CapsLock
    /* F1-F10 ignored */
};

void keyboard_callback(uint8_t scancode) {
    bool release = scancode & 0x80;
    uint8_t code = scancode & 0x7F;

    if (code == 0x2A || code == 0x36) { shift_pressed = !release; return; }
    if (code == 0x3A && !release) { caps_lock = !caps_lock; return; }

    if (release) return; /* Only process key press */

    char c = 0;
    if (code < 128) {
        int idx = shift_pressed ^ caps_lock ? 1 : 0;
        c = keymap[code][idx];
    }

    if (c) {
        /* Echo to screen immediately */
        vga_putchar(c);

        /* Buffer for Shell */
        if (c == '\n') {
            kb_buffer[kb_head++] = '\0'; /* Null terminate */
            kb_head %= KB_BUFFER_SIZE;
        } else if (c == '\b') {
            if (kb_head != kb_tail) {
                kb_head = (kb_head - 1 + KB_BUFFER_SIZE) % KB_BUFFER_SIZE;
                kb_buffer[kb_head] = 0;
            }
        } else {
            kb_buffer[kb_head++] = c;
            kb_head %= KB_BUFFER_SIZE;
        }
    }
}

/* Get line from keyboard buffer (blocking) */
void keyboard_getline(char* buf, uint32_t maxlen) {
    uint32_t i = 0;
    while (1) {
        if (kb_tail != kb_head) {
            char c = kb_buffer[kb_tail++];
            kb_tail %= KB_BUFFER_SIZE;
            if (c == '\0') { buf[i] = 0; return; }
            if (i < maxlen - 1) buf[i++] = c;
        } else {
            asm volatile("hlt"); /* Wait for interrupt */
        }
    }
}

/* ============================================================
 *  Device Manager (from previous version)
 * ============================================================ */
typedef struct device {
    const char* name;
    uint32_t    id;
    int (*init)(struct device* dev);
    int (*read)(struct device* dev, void* buf, size_t count);
} device_t;

#define MAX_DEVICES 16
static device_t* device_registry[MAX_DEVICES];
static uint32_t  device_count = 0;

int register_device(device_t* dev) {
    if (device_count >= MAX_DEVICES) return -1;
    device_registry[device_count++] = dev;
    return 0;
}

int keyboard_init(device_t* dev) {
    vga_puts("[DEV] Keyboard controller initialized (IRQ1).\n");
    return 0;
}
int keyboard_read(device_t* dev, void* buf, size_t count) { (void)dev; (void)buf; (void)count; return 0; }

static device_t keyboard_dev = {
    .name = "PS/2 Keyboard", .id = 0x0001,
    .init = keyboard_init, .read = keyboard_read
};

/* ============================================================
 *  RamFS (Virtual File System in Memory)
 * ============================================================ */
#define MAX_DIRS 64
#define MAX_NAME_LEN 32

typedef struct directory {
    char name[MAX_NAME_LEN];
    uint32_t id;
    bool used;
} directory_t;

static directory_t ramfs_dirs[MAX_DIRS];
static uint32_t dir_count = 0;
static uint32_t next_dir_id = 1;

int ramfs_mkdir(const char* name) {
    if (!name || name[0] == 0) return -1;
    if (dir_count >= MAX_DIRS) return -2;

    for (uint32_t i = 0; i < dir_count; i++) {
        if (ramfs_dirs[i].used && strcmp(ramfs_dirs[i].name, name) == 0) return -3;
    }

    directory_t* d = &ramfs_dirs[dir_count++];
    d->id = next_dir_id++;
    d->used = true;
    strcpy(d->name, name);
    return 0;
}

void ramfs_ls(void) {
    if (dir_count == 0) { vga_puts("  (empty)\n"); return; }
    for (uint32_t i = 0; i < dir_count; i++) {
        if (ramfs_dirs[i].used) {
            vga_puts("  [DIR] "); vga_putdec(ramfs_dirs[i].id); vga_puts("  "); vga_puts(ramfs_dirs[i].name); vga_putchar('\n');
        }
    }
}

/* String helpers (no libc) */
int strcmp(const char* a, const char* b) {
    while (*a && *a == *b) { a++; b++; }
    return *(unsigned char*)a - *(unsigned char*)b;
}
int strncmp(const char* a, const char* b, size_t n) {
    while (n-- && *a && *a == *b) { a++; b++; }
    return n == (size_t)-1 ? 0 : *(unsigned char*)a - *(unsigned char*)b;
}
char* strcpy(char* dest, const char* src) {
    char* d = dest;
    while ((*d++ = *src++));
    return dest;
}
size_t strlen(const char* s) { size_t l=0; while(s[l]) l++; return l; }

/* ============================================================
 *  Shell Commands
 * ============================================================ */
void cmd_help(void) {
    vga_puts("Available commands:\n");
    vga_puts("  help       - Show this help\n");
    vga_puts("  devices    - List registered devices\n");
    vga_puts("  mkdir <n>  - Create virtual directory in RamFS\n");
    vga_puts("  ls         - List RamFS directories\n");
    vga_puts("  alien      - Show ASCII art\n");
    vga_puts("  clear      - Clear screen\n");
}

void cmd_devices(void) {
    vga_puts("Registered Devices:\n");
    for (uint32_t i = 0; i < device_count; i++) {
        vga_puts("  ["); vga_puthex(device_registry[i]->id); vga_puts("] "); vga_puts(device_registry[i]->name); vga_putchar('\n');
    }
}

void cmd_mkdir(char* args) {
    while (*args == ' ') args++;
    if (*args == 0) { vga_puts("Error: mkdir requires a name.\n"); return; }
    int res = ramfs_mkdir(args);
    if (res == 0) { vga_puts("Directory created: "); vga_puts(args); vga_putchar('\n'); }
    else if (res == -1) vga_puts("Error: Invalid name.\n");
    else if (res == -2) vga_puts("Error: RamFS full.\n");
    else if (res == -3) vga_puts("Error: Directory already exists.\n");
}

void cmd_ls(void) { ramfs_ls(); }

void cmd_alien(void) {
    vga_puts("\n");
    vga_puts("       .--.\n");
    vga_puts("      /    \\\n");
    vga_puts("     |  @ @ |\n");
    vga_puts("     |      |\n");
    vga_puts("     |  \\/  |\n");
    vga_puts("      '.__.'\n");
    vga_puts("       |  |\n");
    vga_puts("      _'  '_\n");
    vga_puts("     (______)\n");
    vga_puts("    Take me to your leader!\n\n");
}

void cmd_clear(void) { vga_clear(); }

/* ============================================================
 *  Shell Main Loop
 * ============================================================ */
void shell_process(char* line) {
    while (*line == ' ') line++;
    if (*line == 0) return;

    if (strcmp(line, "help") == 0) cmd_help();
    else if (strcmp(line, "devices") == 0) cmd_devices();
    else if (strncmp(line, "mkdir", 5) == 0) cmd_mkdir(line + 5);
    else if (strcmp(line, "ls") == 0) cmd_ls();
    else if (strcmp(line, "alien") == 0) cmd_alien();
    else if (strcmp(line, "clear") == 0) cmd_clear();
    else {
        vga_puts("Unknown command: "); vga_puts(line); vga_putchar('\n');
    }
}

void shell_run(void) {
    char cmd_buf[256];
    while (1) {
        vga_puts("\nroot@myos:/# ");
        keyboard_getline(cmd_buf, sizeof(cmd_buf));
        shell_process(cmd_buf);
    }
}

/* ============================================================
 *  Kernel Early Init (Called from _start before main)
 * ============================================================ */
void kernel_early_init(void) {
    /* 1. Setup IDT */
    idtp.limit = sizeof(struct idt_entry) * IDT_ENTRIES - 1;
    idtp.base  = (uint32_t)&idt;

    /* Exceptions (0-31): Present, Ring0, 32-bit Interrupt Gate (0x8E) */
    for (int i = 0; i < 32; i++) {
        /* Address of stubs: isr_stub_0 ... isr_stub_31 */
        /* We need the addresses. Since they are global labels in asm, we can't easily get address in C without a table. */
        /* TRICK: The stubs are generated sequentially in assembly? No, macros generate labels. */
        /* Better: Define an array of function pointers in ASM or use a known offset? */
        /* SIMPLEST: In boot.asm, the stubs are global. We declare them as extern void* here. */
    }
    /* We will populate IDT in C using a helper array of addresses passed from asm? 
       NO. Standard way: Declare the stub symbols as extern functions in C. 
       The macro `ISR_NO_ERR` creates `isr_stub_0`, `isr_stub_1` etc. 
       We can declare `extern void isr_stub_0();` etc. But 48 externs is messy.
       ALTERNATIVE: In boot.asm, create a jump table or just write the IDT loading in ASM.
       CONSTRAINT: User wants kernel.c to contain logic. 
       Let's declare the stubs as `extern void isr_stub_0();` ... `isr_stub_31();` and `irq_stub_0`...`irq_stub_15`.
       It's verbose but standard for OS dev in C. 
    */
}

/* Forward declarations for stub addresses (Generated by NASM macros in boot.asm) */
extern void isr_stub_0();  extern void isr_stub_1();  extern void isr_stub_2();  extern void isr_stub_3();
extern void isr_stub_4();  extern void isr_stub_5();  extern void isr_stub_6();  extern void isr_stub_7();
extern void isr_stub_8();  extern void isr_stub_9();  extern void isr_stub_10(); extern void isr_stub_11();
extern void isr_stub_12(); extern void isr_stub_13(); extern void isr_stub_14(); extern void isr_stub_15();
extern void isr_stub_16(); extern void isr_stub_17(); extern void isr_stub_18(); extern void isr_stub_19();
extern void isr_stub_20(); extern void isr_stub_21(); extern void isr_stub_22(); extern void isr_stub_23();
extern void isr_stub_24(); extern void isr_stub_25(); extern void isr_stub_26(); extern void isr_stub_27();
extern void isr_stub_28(); extern void isr_stub_29(); extern void isr_stub_30(); extern void isr_stub_31();

extern void irq_stub_0();  extern void irq_stub_1();  extern void irq_stub_2();  extern void irq_stub_3();
extern void irq_stub_4();  extern void irq_stub_5();  extern void irq_stub_6();  extern void irq_stub_7();
extern void irq_stub_8();  extern void irq_stub_9();  extern void irq_stub_10(); extern void irq_stub_11();
extern void irq_stub_12(); extern void irq_stub_13(); extern void irq_stub_14(); extern void irq_stub_15();

void idt_install(void) {
    /* ISRs */
    void* isr_stubs[32] = {
        isr_stub_0, isr_stub_1, isr_stub_2, isr_stub_3, isr_stub_4, isr_stub_5, isr_stub_6, isr_stub_7,
        isr_stub_8, isr_stub_9, isr_stub_10, isr_stub_11, isr_stub_12, isr_stub_13, isr_stub_14, isr_stub_15,
        isr_stub_16, isr_stub_17, isr_stub_18, isr_stub_19, isr_stub_20, isr_stub_21, isr_stub_22, isr_stub_23,
        isr_stub_24, isr_stub_25, isr_stub_26, isr_stub_27, isr_stub_28, isr_stub_29, isr_stub_30, isr_stub_31
    };
    for (int i = 0; i < 32; i++) idt_set_gate(i, (uint32_t)isr_stubs[i], CODE_SEG, 0x8E);

    /* IRQs (32-47) */
    void* irq_stubs[16] = {
        irq_stub_0, irq_stub_1, irq_stub_2, irq_stub_3, irq_stub_4, irq_stub_5, irq_stub_6, irq_stub_7,
        irq_stub_8, irq_stub_9, irq_stub_10, irq_stub_11, irq_stub_12, irq_stub_13, irq_stub_14, irq_stub_15
    };
    for (int i = 0; i < 16; i++) idt_set_gate(IRQ_BASE + i, (uint32_t)irq_stubs[i], CODE_SEG, 0x8E);

    load_idt(&idtp);
}

/* This function is called from _start (boot.asm) */
void kernel_early_init(void) {
    idt_install();
    pic_remap();
    
    /* Register Keyboard Device */
    register_device(&keyboard_dev);
    keyboard_dev.init(&keyboard_dev);
    
    /* Enable Keyboard IRQ (IRQ 1) on Master PIC */
    outb(PIC1_DATA, inb(PIC1_DATA) & ~(1 << 1)); 
}

/* ============================================================
 *  Kernel Main
 * ============================================================ */
void kernel_main(void) {
    vga_clear();
    vga_puts("========================================\n");
    vga_puts("      MyOS 32-bit Kernel v2.0\n");
    vga_puts("   Interactive Shell + RamFS + IRQs\n");
    vga_puts("========================================\n\n");
    
    vga_puts("Type 'help' for commands.\n");

    /* Enable Interrupts */
    asm volatile("sti");

    /* Run Shell (blocks forever) */
    shell_run();
}
