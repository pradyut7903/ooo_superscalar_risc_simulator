simple_mem.s:
.align 4
.section .text
.globl _start
    # Refer to the RISC-V ISA Spec for the functionality of
    # the instructions in this test program.
_start:

    # setup
    la x1, data
    la x2, dataend

    lb x5, 8(x1)
    lbu x6, 11(x1)

    sw x6, 0(x1)
    sh x5, 6(x1)
    sh x5, 8(x1)
    sh x5, 10(x1)
    sb x5, 7(x1)

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
