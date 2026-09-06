; boot.asm
; NASM Intel syntax, elf32
; Компиляция: nasm -f elf32 boot.asm -o boot.o

section .multiboot
align 4
    dd 0x1BADB002            ; MAGIC
    dd (1 << 0) | (1 << 1)   ; FLAGS: ALIGN + MEMINFO
    dd -(0x1BADB002 + ((1 << 0) | (1 << 1))) ; CHECKSUM

section .bss
align 16
stack_bottom:
    resb 16384               ; 16 KB Stack
stack_top:

; ---------------------------------------------------------
; IDT & Interrupt Stubs
; ---------------------------------------------------------
section .text

; Внешние C-функции
extern kernel_early_init
extern kernel_main
extern isr_handler
extern irq_handler

global _start
global load_idt
global gdt_flush

; --- GDT (Flat Model) ---
gdt_start:
gdt_null:
    dq 0x0
gdt_code:
    dw 0xFFFF, 0x0, 0x9A00, 0x00CF
gdt_data:
    dw 0xFFFF, 0x0, 0x9200, 0x00CF
gdt_end:
gdt_pointer:
    dw gdt_end - gdt_start - 1
    dd gdt_start

CODE_SEG equ gdt_code - gdt_start
DATA_SEG equ gdt_data - gdt_start

; --- IDT Pointer (заполняется из C) ---
idt_pointer:
    dw 0
    dd 0

; ---------------------------------------------------------
; MACRO: ISR Stubs
; ---------------------------------------------------------
; Исключения БЕЗ кода ошибки: пушаем dummy 0, потом вектор
%macro ISR_NO_ERR 1
global isr_stub_%1
isr_stub_%1:
    cli
    push byte 0
    push byte %1
    jmp isr_common_stub
%endmacro

; Исключения С кодом ошибки: CPU уже за 푸шил Error Code, пушаем только вектор
%macro ISR_ERR 1
global isr_stub_%1
isr_stub_%1:
    cli
    push byte %1
    jmp isr_common_stub
%endmacro

; ---------------------------------------------------------
; 32 ЯВНЫХ определения стабов (без циклов, чтобы избежать дублей)
; Векторы с Error Code: 8, 10, 11, 12, 13, 14, 17
; ---------------------------------------------------------
ISR_NO_ERR 0    ; #DE
ISR_NO_ERR 1    ; #DB
ISR_NO_ERR 2    ; NMI
ISR_NO_ERR 3    ; #BP
ISR_NO_ERR 4    ; #OF
ISR_NO_ERR 5    ; #BR
ISR_NO_ERR 6    ; #UD
ISR_NO_ERR 7    ; #NM
ISR_ERR    8    ; #DF (Error Code)
ISR_NO_ERR 9    ; Coprocessor Segment Overrun
ISR_ERR    10   ; #TS (Error Code)
ISR_ERR    11   ; #NP (Error Code)
ISR_ERR    12   ; #SS (Error Code)
ISR_ERR    13   ; #GP (Error Code)
ISR_ERR    14   ; #PF (Error Code)
ISR_NO_ERR 15   ; Reserved
ISR_NO_ERR 16   ; #MF
ISR_ERR    17   ; #AC (Error Code)
ISR_NO_ERR 18   ; #MC
ISR_NO_ERR 19   ; #XM
ISR_NO_ERR 20   ; #VE
ISR_NO_ERR 21   ; #CP
ISR_NO_ERR 22   ; Reserved
ISR_NO_ERR 23   ; Reserved
ISR_NO_ERR 24   ; Reserved
ISR_NO_ERR 25   ; Reserved
ISR_NO_ERR 26   ; Reserved
ISR_NO_ERR 27   ; Reserved
ISR_NO_ERR 28   ; Reserved
ISR_NO_ERR 29   ; Reserved
ISR_NO_ERR 30   ; Reserved
ISR_NO_ERR 31   ; Reserved

; ---------------------------------------------------------
; MACRO: IRQ Stubs (IRQ 0-15 -> Vectors 32-47)
; ---------------------------------------------------------
%macro IRQ_STUB 1
global irq_stub_%1
irq_stub_%1:
    cli
    push byte 0
    push byte %1 + 32
    jmp irq_common_stub
%endmacro

IRQ_STUB 0
IRQ_STUB 1
IRQ_STUB 2
IRQ_STUB 3
IRQ_STUB 4
IRQ_STUB 5
IRQ_STUB 6
IRQ_STUB 7
IRQ_STUB 8
IRQ_STUB 9
IRQ_STUB 10
IRQ_STUB 11
IRQ_STUB 12
IRQ_STUB 13
IRQ_STUB 14
IRQ_STUB 15

; ---------------------------------------------------------
; Common Stubs -> Call C Handlers
; ---------------------------------------------------------
isr_common_stub:
    pusha
    mov ax, DATA_SEG
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    push esp
    call isr_handler
    add esp, 4
    popa
    add esp, 8
    iret

irq_common_stub:
    pusha
    mov ax, DATA_SEG
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    push esp
    call irq_handler
    add esp, 4
    popa
    add esp, 8
    iret

; ---------------------------------------------------------
; Assembly Helpers for C
; ---------------------------------------------------------
global load_idt, gdt_flush, inb, outb, io_wait

load_idt:
    mov eax, [esp + 4]
    lidt [eax]
    ret

gdt_flush:
    lgdt [gdt_pointer]
    mov ax, DATA_SEG
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax
    jmp CODE_SEG:.flush
.flush:
    ret

inb:
    mov edx, [esp + 4]
    xor eax, eax
    in al, dx
    ret

outb:
    mov edx, [esp + 4]
    mov eax, [esp + 8]
    out dx, al
    ret

io_wait:
    outb 0x80, al
    ret

; ---------------------------------------------------------
; Entry Point
; ---------------------------------------------------------
_start:
    cli
    call gdt_flush
    mov esp, stack_top
    call kernel_early_init
    call kernel_main

.hang:
    hlt
    jmp .hang
