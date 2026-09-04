/* linker.ld */
ENTRY(_start)

SECTIONS
{
    . = 0x100000; /* 1 МБ — стандартное место загрузки ядра GRUB */

    .text ALIGN(0x1000) :
    {
        *(.multiboot)   /* Заголовок Multiboot должен быть в начале */
        *(.text)        /* Код (.text из boot.o и kernel.o) */
    }

    .rodata ALIGN(0x1000) :
    {
        *(.rodata*)
    }

    .data ALIGN(0x1000) :
    {
        *(.data)
    }

    .bss ALIGN(0x1000) :
    {
        *(COMMON)
        *(.bss)
    }

    /DISCARD/ : { *(.comment) *(.eh_frame) }
}
