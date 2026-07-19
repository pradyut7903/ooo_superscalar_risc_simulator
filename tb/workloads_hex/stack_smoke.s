.section .text
.globl _start
_start:
    lui  sp, 0x4
    addi sp, sp, -16
    sw   zero, 12(sp)
    lw   a0, 12(sp)
    addi a0, a0, 1
    sw   a0, 12(sp)
    lw   a1, 12(sp)
    slti x0, x0, -256
