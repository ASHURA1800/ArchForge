# 🚀 ARCHFORGE OS: ULTRA-DETAILED ITERATIVE DEVELOPMENT LOOP PROMPT (v1.0)

## 📜 META-INSTRUCTIONS FOR THE AI ASSISTANT (STRICT PROTOCOL)
You are now acting as an Expert OS Development Architect, x86_64 Systems Programmer, and Kernel Debugging Specialist. Your sole purpose is to guide the developer through the systematic, iterative, and bulletproof development of **ArchForge OS**. You must adhere to the following strict protocols without deviation:

1. **Iterative Execution Protocol**: NEVER generate all code for multiple phases at once. Focus on ONE sub-phase at a time. Wait for the developer to confirm successful compilation, QEMU testing, and serial output verification before proceeding to the next sub-phase.
2. **Exhaustive Detail Mandate**: Provide complete, production-ready, copy-pasteable code for every file created or modified. Include line-by-line comments explaining the x86_64 architectural rationale (e.g., why a specific bit is set in a page table entry, why a memory barrier is needed, why a specific register is clobbered).
3. **Safety & Validation Mandate**: Always provide the exact `make` commands, QEMU invocation flags, and expected serial output for each step. Include error-handling, panic routines, and assertion macros for every critical failure point.
4. **Context Retention Mandate**: Continuously reference the current state of the OS (Limine 12.6.1 bootloader, higher-half kernel at 0xffffffff80000000, early serial debug active, framebuffer detected) to ensure absolute compatibility. Never suggest code that contradicts the established baseline.
5. **Loop Protocol Mandate**: At the end of EVERY response, explicitly ask the developer: *"Have you successfully compiled, booted, and verified the expected serial output for this phase? Reply 'YES' to proceed to the next phase, or provide the exact QEMU serial output/error for debugging."*
6. **No Hallucination Mandate**: Do not invent Limine protocol features or x86_64 instructions. Stick strictly to the Limine 12.6.1 specification and the Intel 64 and IA-32 Architectures Software Developer's Manual.

---

## 📊 CURRENT STATE ANALYSIS (BASELINE - VERIFIED WORKING)
- **Bootloader**: Limine 12.6.1 (BIOS track fully working, UEFI track structurally fixed).
- **Kernel Entry**: `kernel/start.S` successfully sets up stack, reloads data segments (0x10), and calls `kernel_main`.
- **Early Debug**: Direct assembly serial output ('A') and C `serial_init()` are working.
- **Serial I/O**: `kernel/serial.c` implements COM1 (0x3F8) at 38400 baud, 8N1, with proper FIFO and transmitter-empty polling.
- **Memory Model**: Higher-half kernel (`0xffffffff80000000`) as defined in `kernel/linker.ld`, with proper `.limine_requests` section placement.
- **Framebuffer**: Limine framebuffer request successful, returning valid address, width, and height.
- **Build System**: Clang/LLD toolchain with `-ffreestanding`, `-fno-stack-protector`, `-mno-red-zone`, and proper Limine deployment.

---

## 🔄 PHASE 1: GLOBAL DESCRIPTOR TABLE (GDT) & TASK STATE SEGMENT (TSS)
**Objective**: Replace the temporary Limine-provided GDT with a robust, kernel-owned GDT that includes a Task State Segment (TSS) for Interrupt Stack Table (IST) support. This is mandatory for safe exception handling (e.g., Page Faults, Double Faults) in x86_64, as it prevents stack overflow corruption during critical interrupts.

### Step 1.1: Architectural Context
In x86_64, the GDT is still required, but segment base/limits are mostly ignored (flat memory model, base=0, limit=0xFFFFFFFFFFFFFFFF). However, the TSS descriptor is critical. The TSS tells the CPU where to find the IST (Interrupt Stack Table), allowing us to switch to a known-good, pre-allocated stack when handling critical exceptions, rather than using the potentially corrupted current stack.

### Step 1.2: File Creation - `include/gdt.h`
```c
#ifndef ARCHFORGE_GDT_H
#define ARCHFORGE_GDT_H

#include <stdint.h>

// GDT Descriptor Structure (16 bytes per entry)
// Packed to ensure no compiler padding interferes with CPU expectations
struct gdt_descriptor {
    uint16_t limit_low;      // Bits 0-15 of segment limit
    uint16_t base_low;       // Bits 0-15 of segment base
    uint8_t  base_mid;       // Bits 16-23 of segment base
    uint8_t  access;         // Access flags (Present, DPL, Type, etc.)
    uint8_t  granularity;    // Flags (Limit 16-19, 64-bit flag, etc.)
    uint8_t  base_high;      // Bits 24-31 of segment base
} __attribute__((packed));

// TSS Structure (x86_64 requires 104 bytes minimum)
// This structure defines the hardware state saved during task switches or IST usage
struct tss_entry {
    uint32_t reserved0;      // Reserved, must be 0
    uint64_t rsp0;           // Stack pointer for ring 0 (used on privilege level changes)
    uint64_t rsp1;           // Stack pointer for ring 1 (unused in typical kernels)
    uint64_t rsp2;           // Stack pointer for ring 2 (unused in typical kernels)
    uint64_t ist1;           // Interrupt Stack Table 1 (e.g., for Double Fault)
    uint64_t ist2;           // IST 2 (e.g., for NMI)
    uint64_t ist3;           // IST 3 (e.g., for Page Fault - CRITICAL)
    uint64_t ist4;           // IST 4
    uint64_t ist5;           // IST 5
    uint64_t ist6;           // IST 6
    uint64_t ist7;           // IST 7
    uint64_t reserved1;      // Reserved
    uint16_t reserved2;      // Reserved
    uint16_t iomap_base;     // I/O Map Base Address (0xFFFF means no I/O map)
} __attribute__((packed));

// GDT Pointer structure for the lgdt instruction
// The CPU expects a 16-bit limit followed by a 64-bit base address
struct gdt_ptr {
    uint16_t limit;
    uint64_t base;
} __attribute__((packed));

// Function prototypes
void gdt_init(void);
void tss_set_rsp0(uint64_t rsp);
void tss_set_ist3(uint64_t rsp);

#endif // ARCHFORGE_GDT_H
```

### Step 1.3: File Creation - `kernel/gdt.c`
```c
#include "gdt.h"
#include "serial.h"

// Define the GDT array (6 entries: Null, Kernel Code, Kernel Data, User Code, User Data, TSS)
static struct gdt_descriptor gdt[6];
static struct gdt_ptr gdt_ptr;

// Define the TSS (placed in .bss, will be initialized)
static struct tss_entry tss __attribute__((aligned(16)));

// External assembly function to load GDT
extern void gdt_load(struct gdt_ptr *ptr);
// External assembly function to load TR register (Task Register)
extern void tss_load(uint16_t selector);

// Helper to set a standard GDT entry
static void gdt_set_gate(int num, uint64_t base, uint32_t limit, uint8_t access, uint8_t gran) {
    gdt[num].base_low    = (base & 0xFFFF);
    gdt[num].base_mid    = (base >> 16) & 0xFF;
    gdt[num].base_high   = (base >> 24) & 0xFF;
    gdt[num].limit_low   = (limit & 0xFFFF);
    gdt[num].granularity = (limit >> 16) & 0x0F;
    gdt[num].granularity |= (gran & 0xF0);
    gdt[num].access      = access;
}

void gdt_init(void) {
    serial_write("[GDT] Initializing Global Descriptor Table...\n");

    // 1. Null Descriptor (Required by x86 architecture)
    gdt_set_gate(0, 0, 0, 0, 0);

    // 2. Kernel Code Segment (Ring 0, Executable, Readable, 64-bit)
    // Access: 1(Present) 00(Ring 0) 1(Code) 1(Readable) 0(Accessed=0) -> 0x9A
    // Granularity: 1(64-bit) 0(Reserved) 0(Reserved) 0(Reserved) 0000(Limit) -> 0x20
    gdt_set_gate(1, 0, 0, 0x9A, 0x20);

    // 3. Kernel Data Segment (Ring 0, Writable)
    // Access: 1(Present) 00(Ring 0) 0(Data) 1(Writable) 0(Accessed=0) -> 0x92
    // Granularity: 0(Not 64-bit, data segments ignore this in 64-bit mode) -> 0x00
    gdt_set_gate(2, 0, 0, 0x92, 0x00);

    // 4. User Code Segment (Ring 3, Executable, Readable, 64-bit)
    // Access: 1(Present) 11(Ring 3) 1(Code) 1(Readable) 0(Accessed=0) -> 0xFA
    gdt_set_gate(3, 0, 0, 0xFA, 0x20);

    // 5. User Data Segment (Ring 3, Writable)
    // Access: 1(Present) 11(Ring 3) 0(Data) 1(Writable) 0(Accessed=0) -> 0xF2
    gdt_set_gate(4, 0, 0, 0xF2, 0x00);

    // 6. TSS Descriptor (64-bit TSS is 16 bytes, but requires a specific 16-byte layout split across two 64-bit words)
    uint64_t tss_base = (uint64_t)&tss;
    uint32_t tss_limit = sizeof(struct tss_entry) - 1;
    
    // Lower 64 bits of the TSS descriptor
    gdt[5].limit_low = tss_limit & 0xFFFF;
    gdt[5].base_low  = tss_base & 0xFFFF;
    gdt[5].base_mid  = (tss_base >> 16) & 0xFF;
    gdt[5].access    = 0x89; // Present (1), Ring 0 (00), 64-bit TSS type (1001 = 0x9)
    gdt[5].granularity = ((tss_limit >> 16) & 0x0F); // No 4KB granularity for TSS
    
    // Upper 64 bits of the TSS descriptor (contains base bits 32-63)
    uint64_t *tss_desc_high = (uint64_t *)&gdt[5] + 1;
    *tss_desc_high = (tss_base >> 32) & 0xFFFFFFFF;

    // Setup GDT Pointer
    gdt_ptr.limit = sizeof(gdt) - 1;
    gdt_ptr.base  = (uint64_t)&gdt;

    // Load GDT using assembly
    gdt_load(&gdt_ptr);
    serial_write("[GDT] GDT loaded successfully.\n");

    // Initialize TSS (Zero out first to ensure no garbage values)
    for (size_t i = 0; i < sizeof(struct tss_entry); i++) {
        ((uint8_t *)&tss)[i] = 0;
    }
    
    // Set I/O Map Base to 0xFFFF to indicate no I/O permission bitmap
    tss.iomap_base = 0xFFFF;

    // Load the TSS selector (Index 5 * 8 bytes per entry = 0x28)
    tss_load(0x28);
    serial_write("[GDT] TSS loaded successfully.\n");
}

void tss_set_rsp0(uint64_t rsp) {
    tss.rsp0 = rsp;
}

void tss_set_ist3(uint64_t rsp) {
    tss.ist3 = rsp;
}
```

### Step 1.4: File Creation - `kernel/gdt_asm.S`
```assembly
.global gdt_load
.type gdt_load, @function
gdt_load:
    // %rdi contains pointer to gdt_ptr
    lgdt (%rdi)
    
    // Reload data segment registers to ensure they use the new GDT
    // Selector 0x10 = Index 2 (Kernel Data) * 8 + RPL 0
    movw $0x10, %ax
    movw %ax, %ds
    movw %ax, %es
    movw %ax, %fs
    movw %ax, %gs
    movw %ax, %ss
    
    // Far jump to reload CS (Code Segment)
    // This is required because the CPU caches the CS descriptor, and we must force it to fetch the new one
    pushq $0x08          // Kernel Code Segment selector (Index 1 * 8 = 0x08)
    leaq .Lreload_cs(%rip), %rax
    pushq %rax
    lretq                // Long return, pops RIP then CS

.Lreload_cs:
    // After lretq, execution continues here with the new CS
    ret

.global tss_load
.type tss_load, @function
tss_load:
    // %di contains the TSS selector (0x28)
    // ltr loads the Task Register, marking the TSS as "busy" in the GDT
    ltr %di
    ret
```

### Step 1.5: Integration into `kernel/kernel.c`
Add `#include "gdt.h"` at the top of `kernel.c`.
In `kernel_main`, immediately after the framebuffer check and before the welcome message, add:
```c
    serial_write("[KERNEL] Initializing GDT and TSS...\n");
    gdt_init();
    serial_write("[KERNEL] GDT and TSS initialization complete.\n");
```

### Step 1.6: Makefile Update
Ensure `gdt.c` and `gdt_asm.S` are added to the dependency list for `$(KERNEL_ELF)`:
```makefile
$(KERNEL_ELF): $(BUILD_DIR)/start.o $(BUILD_DIR)/kernel.o $(BUILD_DIR)/serial.o $(BUILD_DIR)/gdt.o $(BUILD_DIR)/gdt_asm.o
	@mkdir -p $(BUILD_DIR)
	$(LD) $(LDFLAGS) -o $@ $^
```

**🔁 LOOP CHECKPOINT 1**: *Have you added these files, updated the Makefile, and verified that the serial output shows "[GDT] Initializing..." and "[GDT] TSS loaded successfully." without a Triple Fault? Reply 'YES' to proceed to Phase 2 (IDT), or provide the exact error output.*

---

## 🔄 PHASE 2: INTERRUPT DESCRIPTOR TABLE (IDT) & EXCEPTION HANDLING
**Objective**: Implement the IDT to handle CPU exceptions (e.g., Page Faults, General Protection Faults) and hardware IRQs. This requires PIC remapping, assembly ISR stubs, and a C-side handler dispatcher.

### Step 2.1: Architectural Context
The IDT is an array of 256 16-byte descriptors. Each descriptor points to an Interrupt Service Routine (ISR). When an interrupt occurs, the CPU pushes `SS`, `RSP`, `RFLAGS`, `CS`, and `RIP` (and an `ERROR_CODE` for specific exceptions like Page Fault) onto the stack. If a TSS IST is defined for that interrupt, the CPU switches to that stack *before* pushing these values, which is why we set up the TSS in Phase 1.

### Step 2.2: File Creation - `include/idt.h`
```c
#ifndef ARCHFORGE_IDT_H
#define ARCHFORGE_IDT_H

#include <stdint.h>

// IDT Entry Structure (16 bytes)
struct idt_entry {
    uint16_t offset_low;    // Lower 16 bits of handler address
    uint16_t selector;      // Kernel code segment selector (0x08)
    uint8_t  ist;           // Interrupt Stack Table (bits 0-2), zeroed (bits 3-7)
    uint8_t  type_attr;     // Type and attributes (e.g., 0x8E for 64-bit interrupt gate)
    uint16_t offset_mid;    // Middle 16 bits of handler address
    uint32_t offset_high;   // Upper 32 bits of handler address
    uint32_t zero;          // Reserved, must be 0
} __attribute__((packed));

// IDT Pointer structure for the lidt instruction
struct idt_ptr {
    uint16_t limit;
    uint64_t base;
} __attribute__((packed));

// Interrupt frame pushed by CPU (and our assembly stub)
struct interrupt_frame {
    uint64_t rip;
    uint64_t cs;
    uint64_t rflags;
    uint64_t rsp;
    uint64_t ss;
} __attribute__((packed));

// Interrupt frame with error code (for exceptions like Page Fault)
struct interrupt_frame_error {
    uint64_t error_code;
    uint64_t rip;
    uint64_t cs;
    uint64_t rflags;
    uint64_t rsp;
    uint64_t ss;
} __attribute__((packed));

void idt_init(void);
void idt_set_gate(uint8_t num, uint64_t base, uint16_t selector, uint8_t ist, uint8_t type_attr);

// External assembly ISR stubs
extern void isr0(void);   // Divide by zero
extern void isr14(void);  // Page fault
extern void irq0(void);   // Timer (IRQ0)
extern void irq1(void);   // Keyboard (IRQ1)

#endif // ARCHFORGE_IDT_H
```

### Step 2.3: File Creation - `kernel/idt.c`
```c
#include "idt.h"
#include "serial.h"
#include "io.h"

static struct idt_entry idt[256];
static struct idt_ptr idt_ptr;

extern void idt_load(struct idt_ptr *ptr);

// PIC Remapping (8259A)
// The PIC defaults to IRQ 0-7 mapping to vectors 0x08-0x0F, which conflicts with CPU exceptions (0x00-0x1F).
// We must remap them to 0x20-0x2F to avoid this conflict.
static void pic_remap(void) {
    serial_write("[IDT] Remapping PIC...\n");
    
    // ICW1: Initialization command, ICW4 needed, edge-triggered
    outb(0x20, 0x11);
    outb(0xA0, 0x11);
    
    // ICW2: Offset vectors. IRQ 0-7 -> 0x20-0x27, IRQ 8-15 -> 0x28-0x2F
    outb(0x21, 0x20);
    outb(0xA1, 0x28);
    
    // ICW3: Tell master there is a slave at IRQ2 (0x04), tell slave its cascade identity (0x02)
    outb(0x21, 0x04);
    outb(0xA1, 0x02);
    
    // ICW4: 8086 mode, normal EOI (End of Interrupt)
    outb(0x21, 0x01);
    outb(0xA1, 0x01);
    
    // Mask all interrupts initially
    outb(0x21, 0xFF);
    outb(0xA1, 0xFF);
    
    serial_write("[IDT] PIC remapped to 0x20-0x2F.\n");
}

void idt_init(void) {
    serial_write("[IDT] Initializing Interrupt Descriptor Table...\n");
    pic_remap();

    idt_ptr.limit = sizeof(idt) - 1;
    idt_ptr.base = (uint64_t)&idt;

    // Set up critical gates
    // Type 0x8E = 1 (Present) | 00 (Ring 0) | 0 (Reserved) | 1110 (64-bit Interrupt Gate)
    idt_set_gate(0, (uint64_t)isr0, 0x08, 0, 0x8E);    // Divide by zero (No error code)
    idt_set_gate(14, (uint64_t)isr14, 0x08, 3, 0x8E);  // Page Fault (Has error code, uses IST3)
    idt_set_gate(32, (uint64_t)irq0, 0x08, 0, 0x8E);   // Timer (IRQ0)
    idt_set_gate(33, (uint64_t)irq1, 0x08, 0, 0x8E);   // Keyboard (IRQ1)

    idt_load(&idt_ptr);
    serial_write("[IDT] IDT loaded successfully.\n");
    
    // Enable timer interrupt (IRQ0) and keyboard (IRQ1) on master PIC
    uint8_t master_mask = inb(0x21);
    master_mask &= ~0x03; // Unmask IRQ0 and IRQ1
    outb(0x21, master_mask);
    serial_write("[IDT] Timer and Keyboard interrupts enabled.\n");
}

void idt_set_gate(uint8_t num, uint64_t base, uint16_t selector, uint8_t ist, uint8_t type_attr) {
    idt[num].offset_low = (uint16_t)(base & 0xFFFF);
    idt[num].selector = selector;
    idt[num].ist = ist & 0x07; // Lower 3 bits for IST
    idt[num].type_attr = type_attr;
    idt[num].offset_mid = (uint16_t)((base >> 16) & 0xFFFF);
    idt[num].offset_high = (uint32_t)((base >> 32) & 0xFFFFFFFF);
    idt[num].zero = 0;
}
```

### Step 2.4: File Creation - `kernel/isrs.S`
```assembly
.global isr0
.global isr14
.global irq0
.global irq1
.global idt_load

.type idt_load, @function
idt_load:
    lidt (%rdi)
    ret

// Macro for exceptions without an error code
.macro ISR_NOERR num
.global isr\num
.type isr\num, @function
isr\num:
    pushq $0              // Dummy error code
    pushq $\num           // Interrupt number
    jmp isr_common_stub
.endm

// Macro for exceptions with an error code
.macro ISR_ERR num
.global isr\num
.type isr\num, @function
isr\num:
    pushq $\num           // Interrupt number (error code is already on stack below this)
    jmp isr_common_stub
.endm

// Macro for hardware IRQs
.macro IRQ num
.global irq\num
.type irq\num, @function
irq\num:
    pushq $0              // Dummy error code
    pushq $\num           // Interrupt number
    jmp irq_common_stub
.endm

ISR_NOERR 0
ISR_ERR 14
IRQ 32
IRQ 33

isr_common_stub:
    // Save all general-purpose registers
    pushq %rax
    pushq %rcx
    pushq %rdx
    pushq %rbx
    pushq %rbp
    pushq %rsi
    pushq %rdi
    pushq %r8
    pushq %r9
    pushq %r10
    pushq %r11
    pushq %r12
    pushq %r13
    pushq %r14
    pushq %r15
    
    movw %ds, %ax
    pushq %rax
    
    movw $0x10, %ax
    movw %ax, %ds
    movw %ax, %es
    movw %ax, %fs
    movw %ax, %gs
    
    movq %rsp, %rdi
    call isr_handler
    
    popq %rax
    movw %ax, %ds
    movw %ax, %es
    movw %ax, %fs
    movw %ax, %gs
    
    popq %r15
    popq %r14
    popq %r13
    popq %r12
    popq %r11
    popq %r10
    popq %r9
    popq %r8
    popq %rdi
    popq %rsi
    popq %rbp
    popq %rbx
    popq %rdx
    popq %rcx
    popq %rax
    
    addq $16, %rsp
    iretq

irq_common_stub:
    pushq %rax
    pushq %rcx
    pushq %rdx
    pushq %rbx
    pushq %rbp
    pushq %rsi
    pushq %rdi
    pushq %r8
    pushq %r9
    pushq %r10
    pushq %r11
    pushq %r12
    pushq %r13
    pushq %r14
    pushq %r15
    
    movw %ds, %ax
    pushq %rax
    movw $0x10, %ax
    movw %ax, %ds
    movw %ax, %es
    movw %ax, %fs
    movw %ax, %gs
    
    movq %rsp, %rdi
    call irq_handler
    
    popq %rax
    movw %ax, %ds
    movw %ax, %es
    movw %ax, %fs
    movw %ax, %gs
    
    popq %r15
    popq %r14
    popq %r13
    popq %r12
    popq %r11
    popq %r10
    popq %r9
    popq %r8
    popq %rdi
    popq %rsi
    popq %rbp
    popq %rbx
    popq %rdx
    popq %rcx
    popq %rax
    
    addq $16, %rsp
    iretq
```

### Step 2.5: File Creation - `kernel/isr_handler.c`
```c
#include "idt.h"
#include "serial.h"
#include "io.h"

static const char *exception_messages[] = {
    "Division By Zero", "Debug", "Non-Maskable Interrupt", "Breakpoint",
    "Overflow", "Bound Range Exceeded", "Invalid Opcode", "Device Not Available",
    "Double Fault", "Coprocessor Segment Overrun", "Invalid TSS", "Segment Not Present",
    "Stack-Segment Fault", "General Protection Fault", "Page Fault", "Reserved",
    "x87 Floating-Point Exception", "Alignment Check", "Machine Check", "SIMD Floating-Point Exception"
};

struct cpu_registers {
    uint64_t r15, r14, r13, r12, r11, r10, r9, r8;
    uint64_t rdi, rsi, rbp, rbx, rdx, rcx, rax;
};

struct interrupt_context {
    struct cpu_registers regs;
    uint64_t ds;
    uint64_t error_code;
    uint64_t interrupt_number;
    struct interrupt_frame_error frame;
};

void isr_handler(struct interrupt_context *ctx, uint64_t interrupt_number) {
    uint8_t interrupt = (uint8_t)interrupt_number;
    
    if (interrupt == 14) {
        uint64_t cr2;
        __asm__ volatile("mov %%cr2, %0" : "=r"(cr2));
        serial_write("\n!!! PAGE FAULT !!!\n");
        serial_write("Faulting Address: 0x");
        serial_write_hex(cr2);
        serial_write("\nError Code: 0x");
        serial_write_hex(ctx->error_code);
        serial_write("\nInstruction Pointer: 0x");
        serial_write_hex(ctx->frame.rip);
        serial_write("\n");
        
        serial_write("  - Present: ");
        serial_write((ctx->error_code & 0x1) ? "Yes\n" : "No\n");
        serial_write("  - Write: ");
        serial_write((ctx->error_code & 0x2) ? "Yes\n" : "No\n");
        serial_write("  - User: ");
        serial_write((ctx->error_code & 0x4) ? "Yes\n" : "No\n");
        
        serial_write("Halting system...\n");
        for(;;) { __asm__ volatile("hlt"); }
    } else {
        serial_write("\n!!! UNHANDLED EXCEPTION: ");
        if (interrupt < 20) {
            serial_write(exception_messages[interrupt]);
        } else {
            serial_write("Unknown");
        }
        serial_write(" (Vector 0x");
        char hex[3];
        hex[0] = "0123456789abcdef"[(interrupt >> 4) & 0xF];
        hex[1] = "0123456789abcdef"[interrupt & 0xF];
        hex[2] = '\0';
        serial_write(hex);
        serial_write(") !!!\n");
        
        for(;;) { __asm__ volatile("hlt"); }
    }
}

void irq_handler(struct interrupt_context *ctx, uint64_t interrupt_number) {
    uint8_t irq = (uint8_t)interrupt_number - 32;
    
    if (irq == 0) {
        // Timer tick
    } else if (irq == 1) {
        // Keyboard
        uint8_t scancode = inb(0x60);
    }
    
    outb(0x20, 0x20);
    if (irq >= 8) {
        outb(0xA0, 0x20);
    }
}
```
*(Note: Update `isr_common_stub` and `irq_common_stub` in `isrs.S` to pass the interrupt number in `%rsi` before calling the C function: `movq 120(%rsp), %rsi` then `call isr_handler`)*.

**🔁 LOOP CHECKPOINT 2**: *Have you implemented the IDT, PIC remapping, and verified that the system boots without triple-faulting? Test by intentionally triggering a divide-by-zero in `kernel_main` to confirm the ISR catches it. Reply 'YES' to proceed to Phase 3 (PMM).*

---

## 🔄 PHASE 3: PHYSICAL MEMORY MANAGEMENT (PMM)
**Objective**: Parse the Limine Memory Map request and implement a Physical Page Bitmap Allocator to track free and used 4KiB pages.

### Step 3.1: Architectural Context
The PMM is responsible for tracking which physical pages (4096 bytes each) are available for allocation. We will use a bitmap where each bit represents one 4KiB page (0 = free, 1 = used). This is highly memory-efficient: 1 byte manages 8 pages = 32KiB of memory.

### Step 3.2: File Creation - `include/pmm.h`
```c
#ifndef ARCHFORGE_PMM_H
#define ARCHFORGE_PMM_H

#include <stdint.h>
#include <stdbool.h>

#define PAGE_SIZE 4096

void pmm_init(void);
void *pmm_alloc_page(void);
void pmm_free_page(void *page);
uint64_t pmm_get_free_page_count(void);
uint64_t pmm_get_total_page_count(void);

#endif // ARCHFORGE_PMM_H
```

### Step 3.3: File Creation - `kernel/pmm.c`
```c
#include "pmm.h"
#include "serial.h"
#include "limine.h"

__attribute__((used, section(".limine_requests")))
static volatile struct limine_memmap_request memmap_request = {
    .id = LIMINE_MEMMAP_REQUEST_ID,
    .revision = 0
};

static uint8_t *bitmap = NULL;
static uint64_t total_pages = 0;
static uint64_t free_pages = 0;

static inline void set_bit(uint64_t index) {
    bitmap[index / 8] |= (1 << (index % 8));
}

static inline void clear_bit(uint64_t index) {
    bitmap[index / 8] &= ~(1 << (index % 8));
}

static inline bool test_bit(uint64_t index) {
    return (bitmap[index / 8] & (1 << (index % 8))) != 0;
}

void pmm_init(void) {
    serial_write("[PMM] Initializing Physical Memory Manager...\n");
    
    if (memmap_request.response == NULL || memmap_request.response->entry_count == 0) {
        serial_write("[PMM] FATAL: No memory map provided by bootloader!\n");
        for(;;) { __asm__ volatile("hlt"); }
    }

    struct limine_memmap_response *resp = memmap_request.response;
    uint64_t highest_addr = 0;

    for (uint64_t i = 0; i < resp->entry_count; i++) {
        struct limine_memmap_entry *entry = resp->entries[i];
        if (entry->type == LIMINE_MEMMAP_USABLE) {
            uint64_t end = entry->base + entry->length;
            if (end > highest_addr) {
                highest_addr = end;
            }
        }
    }

    total_pages = (highest_addr / PAGE_SIZE) + 1;
    uint64_t bitmap_size = (total_pages / 8) + 1;
    free_pages = 0;

    serial_write("[PMM] Total Pages: 0x");
    serial_write_hex(total_pages);
    serial_write("\n");

    for (uint64_t i = 0; i < resp->entry_count; i++) {
        struct limine_memmap_entry *entry = resp->entries[i];
        if (entry->type == LIMINE_MEMMAP_USABLE && entry->length >= bitmap_size) {
            bitmap = (uint8_t *)(uintptr_t)entry->base;
            serial_write("[PMM] Bitmap placed at physical address: 0x");
            serial_write_hex((uint64_t)(uintptr_t)bitmap);
            serial_write("\n");
            break;
        }
    }

    if (bitmap == NULL) {
        serial_write("[PMM] FATAL: Could not find usable memory for bitmap!\n");
        for(;;) { __asm__ volatile("hlt"); }
    }

    for (uint64_t i = 0; i < bitmap_size; i++) {
        bitmap[i] = 0xFF;
    }

    for (uint64_t i = 0; i < resp->entry_count; i++) {
        struct limine_memmap_entry *entry = resp->entries[i];
        if (entry->type == LIMINE_MEMMAP_USABLE) {
            uint64_t start_page = entry->base / PAGE_SIZE;
            uint64_t page_count = entry->length / PAGE_SIZE;
            
            for (uint64_t j = 0; j < page_count; j++) {
                clear_bit(start_page + j);
                free_pages++;
            }
        }
    }

    uint64_t bitmap_start_page = (uint64_t)(uintptr_t)bitmap / PAGE_SIZE;
    uint64_t bitmap_pages = (bitmap_size / PAGE_SIZE) + 1;
    for (uint64_t i = 0; i < bitmap_pages; i++) {
        set_bit(bitmap_start_page + i);
        free_pages--;
    }

    serial_write("[PMM] Initialization complete. Free pages: 0x");
    serial_write_hex(free_pages);
    serial_write("\n");
}

void *pmm_alloc_page(void) {
    for (uint64_t i = 0; i < total_pages; i++) {
        if (!test_bit(i)) {
            set_bit(i);
            free_pages--;
            return (void *)(uintptr_t)(i * PAGE_SIZE);
        }
    }
    serial_write("[PMM] WARNING: Out of memory in pmm_alloc_page!\n");
    return NULL;
}

void pmm_free_page(void *page) {
    uint64_t index = (uint64_t)(uintptr_t)page / PAGE_SIZE;
    if (test_bit(index)) {
        clear_bit(index);
        free_pages++;
    }
}

uint64_t pmm_get_free_page_count(void) {
    return free_pages;
}

uint64_t pmm_get_total_page_count(void) {
    return total_pages;
}
```

**🔁 LOOP CHECKPOINT 3**: *Have you integrated the PMM, verified that it correctly parses the Limine memory map, and confirmed via serial output that it reports the correct number of free pages? Reply 'YES' to proceed to Phase 4 (VMM).*

---

## 🔄 PHASE 4: VIRTUAL MEMORY MANAGEMENT (VMM)
**Objective**: Implement x86_64 4-level paging (PML4, PDPT, PD, PT) to enable virtual memory, identity mapping, and higher-half kernel mapping.

### Step 4.1: Architectural Context
x86_64 uses a 4-level page table hierarchy (4KiB pages):
1. **PML4**: 512 entries, selects the PDPT.
2. **PDPT**: 512 entries, selects the PD.
3. **PD**: 512 entries, selects the PT.
4. **PT**: 512 entries, maps to the physical 4KiB page.
Each entry is 8 bytes. A full table is 4096 bytes (1 page).

### Step 4.2: File Creation - `include/vmm.h`
```c
#ifndef ARCHFORGE_VMM_H
#define ARCHFORGE_VMM_H

#include <stdint.h>
#include <stdbool.h>

#define PTE_PRESENT   (1ULL << 0)
#define PTE_WRITABLE  (1ULL << 1)
#define PTE_USER      (1ULL << 2)

void vmm_init(void);
void vmm_map_page(uint64_t virt_addr, uint64_t phys_addr, uint64_t flags);
void vmm_unmap_page(uint64_t virt_addr);
uint64_t vmm_get_phys_addr(uint64_t virt_addr);

#endif // ARCHFORGE_VMM_H
```

### Step 4.3: File Creation - `kernel/vmm.c`
```c
#include "vmm.h"
#include "pmm.h"
#include "serial.h"

static uint64_t *pml4 = NULL;

static uint64_t *get_next_level(uint64_t *table, uint64_t index, bool allocate) {
    if (table[index] & PTE_PRESENT) {
        return (uint64_t *)(uintptr_t)(table[index] & ~0xFFF);
    }
    
    if (!allocate) {
        return NULL;
    }
    
    void *new_table = pmm_alloc_page();
    if (!new_table) {
        serial_write("[VMM] FATAL: Out of memory allocating page table!\n");
        for(;;) { __asm__ volatile("hlt"); }
    }
    
    for (int i = 0; i < 512; i++) {
        ((uint64_t *)new_table)[i] = 0;
    }
    
    table[index] = (uint64_t)(uintptr_t)new_table | PTE_PRESENT | PTE_WRITABLE | PTE_USER;
    return (uint64_t *)new_table;
}

void vmm_init(void) {
    serial_write("[VMM] Initializing Virtual Memory Manager...\n");
    
    pml4 = (uint64_t *)pmm_alloc_page();
    if (!pml4) {
        serial_write("[VMM] FATAL: Cannot allocate PML4!\n");
        for(;;) { __asm__ volatile("hlt"); }
    }
    
    for (int i = 0; i < 512; i++) {
        pml4[i] = 0;
    }
    
    for (uint64_t i = 0; i < 512; i++) {
        vmm_map_page(i * 4096, i * 4096, PTE_PRESENT | PTE_WRITABLE);
    }
    
    uint64_t pml4_phys = (uint64_t)(uintptr_t)pml4;
    __asm__ volatile("mov %0, %%cr3" : : "r"(pml4_phys) : "memory");
    
    uint64_t cr0;
    __asm__ volatile("mov %%cr0, %0" : "=r"(cr0));
    cr0 |= (1ULL << 31);
    __asm__ volatile("mov %0, %%cr0" : : "r"(cr0) : "memory");
    
    uint64_t cr4;
    __asm__ volatile("mov %%cr4, %0" : "=r"(cr4));
    cr4 |= (1ULL << 5);
    __asm__ volatile("mov %0, %%cr4" : : "r"(cr4) : "memory");
    
    serial_write("[VMM] Paging enabled successfully.\n");
}

void vmm_map_page(uint64_t virt_addr, uint64_t phys_addr, uint64_t flags) {
    uint64_t pml4_index = (virt_addr >> 39) & 0x1FF;
    uint64_t pdpt_index = (virt_addr >> 30) & 0x1FF;
    uint64_t pd_index   = (virt_addr >> 21) & 0x1FF;
    uint64_t pt_index   = (virt_addr >> 12) & 0x1FF;
    
    uint64_t *pdpt = get_next_level(pml4, pml4_index, true);
    uint64_t *pd   = get_next_level(pdpt, pdpt_index, true);
    uint64_t *pt   = get_next_level(pd, pd_index, true);
    
    pt[pt_index] = (phys_addr & ~0xFFF) | flags;
    
    __asm__ volatile("invlpg (%0)" : : "r"(virt_addr) : "memory");
}

void vmm_unmap_page(uint64_t virt_addr) {
    uint64_t pml4_index = (virt_addr >> 39) & 0x1FF;
    uint64_t pdpt_index = (virt_addr >> 30) & 0x1FF;
    uint64_t pd_index   = (virt_addr >> 21) & 0x1FF;
    uint64_t pt_index   = (virt_addr >> 12) & 0x1FF;
    
    uint64_t *pdpt = get_next_level(pml4, pml4_index, false);
    if (!pdpt) return;
    
    uint64_t *pd = get_next_level(pdpt, pdpt_index, false);
    if (!pd) return;
    
    uint64_t *pt = get_next_level(pd, pd_index, false);
    if (!pt) return;
    
    pt[pt_index] = 0;
    __asm__ volatile("invlpg (%0)" : : "r"(virt_addr) : "memory");
}

uint64_t vmm_get_phys_addr(uint64_t virt_addr) {
    uint64_t pml4_index = (virt_addr >> 39) & 0x1FF;
    uint64_t pdpt_index = (virt_addr >> 30) & 0x1FF;
    uint64_t pd_index   = (virt_addr >> 21) & 0x1FF;
    uint64_t pt_index   = (virt_addr >> 12) & 0x1FF;
    
    uint64_t *pdpt = get_next_level(pml4, pml4_index, false);
    if (!pdpt || !(pdpt[pml4_index] & PTE_PRESENT)) return 0;
    
    uint64_t *pd = get_next_level(pdpt, pdpt_index, false);
    if (!pd || !(pd[pt_index] & PTE_PRESENT)) return 0;
    
    uint64_t *pt = get_next_level(pd, pd_index, false);
    if (!pt || !(pt[pt_index] & PTE_PRESENT)) return 0;
    
    return (pt[pt_index] & ~0xFFF) + (virt_addr & 0xFFF);
}
```

**🔁 LOOP CHECKPOINT 4**: *Have you implemented the VMM, verified that `vmm_map_page` and `vmm_get_phys_addr` work correctly without causing a Page Fault? Reply 'YES' to proceed to Phase 5 (Heap).*

---

## 🔄 PHASE 5: KERNEL HEAP & DYNAMIC ALLOCATION
**Objective**: Implement a basic `kmalloc` and `kfree` using a page-granular allocator on top of the VMM and PMM.

### Step 5.1: File Creation - `include/heap.h`
```c
#ifndef ARCHFORGE_HEAP_H
#define ARCHFORGE_HEAP_H

#include <stdint.h>
#include <stddef.h>

void heap_init(void);
void *kmalloc(size_t size);
void kfree(void *ptr);

#endif // ARCHFORGE_HEAP_H
```

### Step 5.2: File Creation - `kernel/heap.c`
```c
#include "heap.h"
#include "pmm.h"
#include "vmm.h"
#include "serial.h"

#define HEAP_BASE 0xffffffffa0000000
static uint64_t heap_current = HEAP_BASE;

void heap_init(void) {
    serial_write("[HEAP] Initializing Kernel Heap at 0x");
    serial_write_hex(HEAP_BASE);
    serial_write("\n");
}

void *kmalloc(size_t size) {
    if (size == 0) return NULL;
    
    uint64_t pages = (size + 4095) / 4096;
    
    for (uint64_t i = 0; i < pages; i++) {
        void *phys = pmm_alloc_page();
        if (!phys) {
            serial_write("[HEAP] FATAL: Out of memory in kmalloc!\n");
            return NULL;
        }
        vmm_map_page(heap_current + (i * 4096), (uint64_t)(uintptr_t)phys, PTE_PRESENT | PTE_WRITABLE | PTE_USER);
    }
    
    void *ret = (void *)(uintptr_t)heap_current;
    heap_current += pages * 4096;
    
    return ret;
}

void kfree(void *ptr) {
    if (!ptr) return;
    serial_write("[HEAP] kfree called on 0x");
    serial_write_hex((uint64_t)(uintptr_t)ptr);
    serial_write(" (stub)\n");
}
```

**🔁 LOOP CHECKPOINT 5**: *Have you integrated the heap, verified that `kmalloc` successfully returns higher-half virtual addresses? Reply 'YES' to proceed to Phase 6 (Advanced Debugging).*

---

## 🔄 PHASE 6: ADVANCED DEBUGGING & TOOLING
**Objective**: Establish a robust debugging workflow using QEMU's built-in monitor, GDB, and enhanced serial logging.

### Step 6.1: QEMU Debugging Setup
Update your `Makefile` to include a robust `debug` target:
```makefile
debug: $(ISO_IMAGE)
	@echo "Starting QEMU in debug mode (GDB on port 1234)..."
	qemu-system-x86_64 -M q35 -m 2G -cdrom $(ISO_IMAGE) -serial stdio -s -S -d int,cpu_reset,guest_errors
```

### Step 6.2: GDB Initialization Script (`.gdbinit`)
Create a `.gdbinit` file in your project root:
```gdb
set architecture i386:x86-64
target remote localhost:1234
set disassembly-flavor intel
layout asm
layout regs
break kernel_main
continue
```

### Step 6.3: Enhanced Serial Logging Macro
Add to `include/serial.h`:
```c
#include <stdarg.h>
void serial_printf(const char *format, ...);

#define LOG_INFO(fmt, ...) do { \
    serial_printf("[INFO] %s:%d - " fmt "\n", __FILE__, __LINE__, ##__VA_ARGS__); \
} while(0)
```

**🔁 LOOP CHECKPOINT 6**: *Have you set up the GDB workflow and verified that you can step through `kernel_main` instruction by instruction? Reply 'YES' to conclude the current loop.*

---

## 📜 APPENDICES (REFERENCE MATERIAL)

### Appendix A: x86_64 Register Quick Reference
- **General Purpose**: RAX (return), RBX (callee-saved), RCX (4th arg), RDX (3rd arg), RSI (2nd arg), RDI (1st arg), RBP (callee-saved), RSP (stack pointer), R8-R15 (args/callee-saved).
- **Control Registers**: CR0 (PG, PE, WP), CR2 (Page Fault Linear Address), CR3 (PML4 Base), CR4 (PAE, PGE, SMAP, SMEP).

### Appendix B: Common QEMU Commands
- BIOS Boot: `qemu-system-x86_64 -M q35 -m 2G -hda build/archforge.img -serial stdio -display none`
- UEFI Boot: `qemu-system-x86_64 -M q35 -m 2G -bios /usr/share/ovmf/x64/OVMF_CODE_4M.fd -cdrom build/archforge.iso -serial stdio -display none`

### Appendix C: Iterative Loop Template (Copy & Paste This)
```text
STATUS UPDATE:
- Current Phase Completed: [e.g., Phase 3: PMM]
- Compilation Status: [SUCCESS / FAILED - paste error if failed]
- QEMU Boot Status: [SUCCESS / TRIPLE FAULT / HANG]
- Serial Output Observed: [Paste the exact last 10 lines of serial output]
- GDB/Debugging Findings: [Any register states or crash addresses found]

NEXT ACTION REQUEST:
Please provide the complete, step-by-step instructions and full code for the next phase: [e.g., Phase 4: VMM], including any necessary Makefile updates, new file creations, and specific QEMU test cases to verify correctness.
```

---
END OF PROMPT