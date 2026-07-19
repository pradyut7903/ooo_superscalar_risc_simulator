# Bigger ILP-friendly loop for IPC measurement (fits in small I$).
# Independent accumulators so WIDTH=4 can issue multiple ALUs/cycle.
.align 4
.section .text
.globl _start
_start:
    li   t0, 0
    li   t1, 0
    li   t2, 0
    li   t3, 0
    li   a0, 4096          # iteration count
loop:
    addi t0, t0, 1
    addi t1, t1, 3
    addi t2, t2, 5
    addi t3, t3, 7
    xor  t0, t0, t1
    add  t1, t1, t2
    xor  t2, t2, t3
    add  t3, t3, t0
    addi a0, a0, -1
    bne  a0, x0, loop
    # sink results so they are not DCE'd by a human reader
    add  a1, t0, t1
    add  a2, t2, t3
    xor  a3, a1, a2
halt:
    beq  x0, x0, halt
