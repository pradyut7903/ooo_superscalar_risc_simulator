riscv_basic_asm.s:
.align 4
.section .text
.globl _start
    # Refer to the RISC-V ISA Spec for the functionality of
    # the instructions in this test program.
_start:

    # setup
    la x1, data
    la x2, dataend
loop:
    sw x1, 0(x1)    
    addi x1, x1, 4
    bne x1, x2, loop
    la x1, data

    # Dependent stores
    lw x3, 0(x1)
    sw x3, 0(x1)
    lw x3, 4(x3)    
    sw x3, 4(x3)
    lw x3, 4(x3)
    sh x3, 4(x3)
    lw x3, 4(x3)
    sh x3, 4(x3)
    lw x3, 4(x3)
    sb x3, 4(x3)
    lw x3, 4(x3)
    sb x3, 4(x3)

    # when you are writing your own testcase, include these 4 lines and the halt.
    li  t0, 1
    la  t1, tohost
    sw  t0, 0(t1)
    sw  x0, 4(t1)
halt:                 # Infinite loop to keep the processor
    beq x0, x0, halt  # from trying to execute the data below.
                      # Your own programs should also make use
                      # of an infinite loop at the end.

.section .rodata

data:
.word 0xb8594e5a
.word 0xc1c42cac
.word 0x71044d48
.word 0x65bf361b
.word 0xb7516279
.word 0xbe5e0496
.word 0x3f67a1ba
.word 0xc198ca43
.word 0xae1caa77
.word 0x82ae5cb5
dataend:

.section ".tohost"
.globl tohost
tohost: .dword 0
.section ".fromhost"
.globl fromhost
fromhost: .dword 0
