; boot.asm
; NASM Intel syntax, elf32
; Компиляция: nasm -f elf32 boot.asm -o boot.o

section .multiboot
align 4
    ; Multiboot Header
    dd 0x1BADB002            ; MAGIC
    dd (1 << 0) | (1 << 1)   ; FLAGS: ALIGN (1<<0) + MEMINFO (1<<1)
    dd -(0x1BADB002 + ((1 << 0) | (1 << 1))) ; CHECKSUM = -(MAGIC + FLAGS)

section .bss
align 16
stack_bottom:
    resb 16384               ; 16 КБ стека
stack_top:

section .text
global _start
extern kernel_main

_start:
    ; Указатель стека уже установлен линкером в stack_top,
    ; но для надежности и явной инициализации (требование задачи):
    mov esp, stack_top
    
    ; Вызов ядра на C
    call kernel_main
    
    ; Если kernel_main вернул управление — вечный цикл
.hang:
    hlt
    jmp .hang
