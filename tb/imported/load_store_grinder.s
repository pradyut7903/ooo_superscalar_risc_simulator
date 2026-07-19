load_store_grinder.s:
.align 4
.section .text
.globl _start
    # Refer to the RISC-V ISA Spec for the functionality of
    # the instructions in this test program.
_start:

    # setup
    la x1, data
    la x2, dataend

    # Load Check
    lw x3, 0(x1)
    lh x4, 4(x1)
    la x4, data + 4
    lb x5, 8(x4)
    lhu x6, 10(x4)
    lbu x6, 11(x4)

    # Store Check
    sw x6, 0(x1)
    sh x5, 6(x1)
    sh x5, 8(x1)
    sh x5, 10(x1)
    sb x5, 7(x1)

    # Load and Stores Mix
    la x4, data + 24 
    la x5, data + 32
    lw x6, 0(x4)
    sw x6, 4(x4)
    lw x6, 4(x4)
    sw x6, 8(x4)
    addi x4, x4, 4
    lh x5, 0(x4)
    sh x6, 0(x4)
    lw x7, 0(x4)
    sw x7, 0(x4)
    
   
halt:                 
    slti x0, x0,-256

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
.word 0xb8594e5a
.word 0xc1c42cac
.word 0x71044d48
.word 0x65bf361b
.word 0xb7516279
.word 0xbe5e0496
.word 0x3f67a1ba
.word 0xc198ca43
.word 0xae1caa77
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
.word 0x82ae5cb5
.word 0x82ae5cb5
dataend:

.section ".tohost"
.globl tohost
tohost: .dword 0
.section ".fromhost"
.globl fromhost
fromhost: .dword 0
