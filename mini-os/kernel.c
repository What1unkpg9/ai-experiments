/* kernel.c */
/* Компиляция: gcc -m32 -ffreestanding -c kernel.c -o kernel.o */

#include <stdint.h>
#include <stddef.h>

/* ==================== VGA Text Mode Driver ==================== */

#define VGA_WIDTH 80
#define VGA_HEIGHT 25
#define VGA_MEMORY ((volatile uint16_t*)0xB8000)

static uint8_t  vga_color = 0x0F; /* Белый на черном */
static size_t   vga_row = 0;
static size_t   vga_col = 0;

static inline uint16_t vga_entry(char c, uint8_t color) {
    return (uint16_t)c | ((uint16_t)color << 8);
}

void vga_clear(void) {
    for (size_t y = 0; y < VGA_HEIGHT; y++) {
        for (size_t x = 0; x < VGA_WIDTH; x++) {
            VGA_MEMORY[y * VGA_WIDTH + x] = vga_entry(' ', vga_color);
        }
    }
    vga_row = 0; vga_col = 0;
}

void vga_putchar(char c) {
    if (c == '\n') {
        vga_col = 0;
        vga_row++;
    } else {
        VGA_MEMORY[vga_row * VGA_WIDTH + vga_col] = vga_entry(c, vga_color);
        vga_col++;
    }

    if (vga_col >= VGA_WIDTH) { vga_col = 0; vga_row++; }
    if (vga_row >= VGA_HEIGHT) { vga_row = 0; } /* Простой скролл: затирание сверху */
}

void vga_puts(const char* s) {
    while (*s) vga_putchar(*s++);
}

/* ==================== Device Manager ==================== */

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

/* ==================== Fake Keyboard Device ==================== */

int keyboard_init(device_t* dev) {
    vga_puts("[DEV] Keyboard controller initialized.\n");
    return 0;
}

int keyboard_read(device_t* dev, void* buf, size_t count) {
    /* Заглушка: реальное чтение требует прерываний и портов I/O */
    (void)dev; (void)buf; (void)count;
    return 0;
}

static device_t keyboard_dev = {
    .name = "PS/2 Keyboard",
    .id   = 0x0001,
    .init = keyboard_init,
    .read = keyboard_read
};

/* ==================== Kernel Entry Point ==================== */

void kernel_main(void) {
    vga_clear();
    vga_puts("=== MyOS Kernel Loaded ===\n");
    vga_puts("Multiboot OK. VGA Driver Active.\n\n");

    /* Регистрация и инициализация устройств */
    vga_puts("Initializing Device Manager...\n");
    if (register_device(&keyboard_dev) == 0) {
        vga_puts("[OK] Device registered: PS/2 Keyboard\n");
        if (keyboard_dev.init(&keyboard_dev) == 0) {
            vga_puts("[OK] Device init successful.\n");
        }
    } else {
        vga_puts("[ERR] Device registry full!\n");
    }

    vga_puts("\nSystem halted. Keyboard demo registered (no IRQ handler).\n");
    
    /* Вечный цикл */
    for (;;) asm volatile("hlt");
}
