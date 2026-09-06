; boot.asm
; NASM Intel syntax, elf32
; Содержит: Multiboot Header, IDT Stubs (ISR/IRQ), GDT (flat), Entry Point (_start)

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
extern kernel_early_init     ; Инициализация IDT/PIC (до main)
extern kernel_main           ; Главный цикл
extern isr_handler           ; Общий обработчик исключений (C)
extern irq_handler           ; Общий обработчик IRQ (C)

global _start
global load_idt              ; Функция загрузки IDT (вызывается из C)
global gdt_flush             ; Загрузка GDT (вызывается из C)

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
    dw 0                      ; Limit (заполнится в C)
    dd 0                      ; Base  (заполнится в C)

; ---------------------------------------------------------
; MACRO: ISR Stub (Exceptions 0-31)
; Некоторые исключения пушат код ошибки (Error Code), некоторые нет.
; Чтобы стек был единообразен, для тех, что не пушат - пушим dummy 0.
; Формат стека при входе в isr_handler:
; [ESP] = Vector Number
; [ESP+4] = Error Code (или 0)
; [ESP+8] = EIP, CS, EFLAGS, ESP, SS (пushed by CPU)
; ---------------------------------------------------------
%macro ISR_NO_ERR 1
global isr_stub_%1
isr_stub_%1:
    cli
    push byte 0               ; Dummy Error Code
    push byte %1              ; Vector Number
    jmp isr_common_stub
%endmacro

%macro ISR_ERR 1
global isr_stub_%1
isr_stub_%1:
    cli
    push byte %1              ; Vector Number (CPU уже пушнул Error Code)
    jmp isr_common_stub
%endmacro

; Исключения без кода ошибки
ISR_NO_ERR 0   ; #DE Divide Error
ISR_NO_ERR 1   ; #DB Debug
ISR_NO_ERR 2   ; NMI
ISR_NO_ERR 3   ; #BP Breakpoint
ISR_NO_ERR 4   ; #OF Overflow
ISR_NO_ERR 5   ; #BR Bound Range
ISR_NO_ERR 6   ; #UD Invalid Opcode
ISR_NO_ERR 7   ; #NM Device Not Available
ISR_NO_ERR 8   ; #DF Double Fault (HAS ERR CODE! see below)
ISR_NO_ERR 9   ; Coprocessor Segment Overrun
ISR_NO_ERR 10  ; #TS Invalid TSS (HAS ERR CODE!)
ISR_NO_ERR 11  ; #NP Segment Not Present (HAS ERR CODE!)
ISR_NO_ERR 12  ; #SS Stack Segment (HAS ERR CODE!)
ISR_NO_ERR 13  ; #GP General Protection (HAS ERR CODE!)
ISR_NO_ERR 14  ; #PF Page Fault (HAS ERR CODE!)
ISR_NO_ERR 15  ; Reserved
ISR_NO_ERR 16  ; #MF x87 FPU
ISR_NO_ERR 17  ; #AC Alignment Check (HAS ERR CODE!)
ISR_NO_ERR 18  ; #MC Machine Check
ISR_NO_ERR 19  ; #XM SIMD
ISR_NO_ERR 20  ; #VE Virtualization
ISR_NO_ERR 21  ; #CP Control Protection
ISR_NO_ERR 22  ; Reserved
ISR_NO_ERR 23  ; Reserved
ISR_NO_ERR 24  ; Reserved
ISR_NO_ERR 25  ; Reserved
ISR_NO_ERR 26  ; Reserved
ISR_NO_ERR 27  ; Reserved
ISR_NO_ERR 28  ; Reserved
ISR_NO_ERR 29  ; Reserved
ISR_NO_ERR 30  ; Reserved
ISR_NO_ERR 31  ; Reserved

; Исправляем те, что имеют Error Code (8, 10, 11, 12, 13, 14, 17)
; Переопределяем их с макросом ISR_ERR
global isr_stub_8
isr_stub_8:
    cli
    push byte 8
    jmp isr_common_stub

global isr_stub_10
isr_stub_10:
    cli
    push byte 10
    jmp isr_common_stub

global isr_stub_11
isr_stub_11:
    cli
    push byte 11
    jmp isr_common_stub

global isr_stub_12
isr_stub_12:
    cli
    push byte 12
    jmp isr_common_stub

global isr_stub_13
isr_stub_13:
    cli
    push byte 13
    jmp isr_common_stub

global isr_stub_14
isr_stub_14:
    cli
    push byte 14
    jmp isr_common_stub

global isr_stub_17
isr_stub_17:
    cli
    push byte 17
    jmp isr_common_stub


; ---------------------------------------------------------
; MACRO: IRQ Stubs (IRQ 0-15 -> Vectors 32-47)
; CPU не пушает Error Code для IRQ.
; ---------------------------------------------------------
%macro IRQ_STUB 1
global irq_stub_%1
irq_stub_%1:
    cli
    push byte 0               ; Dummy Error Code
    push byte %1 + 32         ; Vector Number (32-47)
    jmp irq_common_stub
%endmacro

IRQ_STUB 0   ; Timer
IRQ_STUB 1   ; Keyboard
IRQ_STUB 2   ; Cascade
IRQ_STUB 3   ; COM2
IRQ_STUB 4   ; COM1
IRQ_STUB 5   ; LPT2
IRQ_STUB 6   ; Floppy
IRQ_STUB 7   ; LPT1
IRQ_STUB 8   ; RTC
IRQ_STUB 9   ; ACPI
IRQ_STUB 10  ; Free
IRQ_STUB 11  ; Free
IRQ_STUB 12  ; PS/2 Mouse
IRQ_STUB 13  ; FPU
IRQ_STUB 14  ; Primary ATA
IRQ_STUB 15  ; Secondary ATA

; ---------------------------------------------------------
; Common Stubs -> Call C Handlers
; ---------------------------------------------------------
extern isr_handler
extern irq_handler

isr_common_stub:
    pusha                     ; Push EAX, ECX, EDX, EBX, ESP, EBP, ESI, EDI
    mov ax, DATA_SEG
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    push esp                  ; Pass pointer to stack (regs_t*)
    call isr_handler
    add esp, 4
    popa
    add esp, 8                ; Pop Error Code + Vector
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
; void load_idt(idt_ptr_t* ptr)
load_idt:
    mov eax, [esp + 4]
    lidt [eax]
    ret

; void gdt_flush()
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

; I/O Ports
global inb, outb, io_wait
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
    ; 1. Setup Segments (Flat model)
    cli
    call gdt_flush

    ; 2. Setup Stack
    mov esp, stack_top

    ; 3. Call Early Kernel Init (IDT, PIC, Drivers)
    call kernel_early_init

    ; 4. Call Main Kernel
    call kernel_main

    ; 5. Halt
.hang:
    hlt
    jmp .hang
