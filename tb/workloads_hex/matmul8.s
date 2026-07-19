# 8x8 integer matrix multiply: C = A * B (row-major, word elements).
.align 4
.section .text
.globl _start
_start:
    la   s0, A
    la   s1, B
    la   s2, C
    li   s3, 8             # N
    li   t0, 0             # i
i_loop:
    li   t1, 0             # j
j_loop:
    li   t2, 0             # acc
    li   t3, 0             # k
k_loop:
    # A[i][k]
    slli t4, t0, 3
    add  t4, t4, t3
    slli t4, t4, 2
    add  t4, s0, t4
    lw   t5, 0(t4)
    # B[k][j]
    slli t4, t3, 3
    add  t4, t4, t1
    slli t4, t4, 2
    add  t4, s1, t4
    lw   t6, 0(t4)
    mul  t5, t5, t6
    add  t2, t2, t5
    addi t3, t3, 1
    blt  t3, s3, k_loop
    # C[i][j] = acc
    slli t4, t0, 3
    add  t4, t4, t1
    slli t4, t4, 2
    add  t4, s2, t4
    sw   t2, 0(t4)
    addi t1, t1, 1
    blt  t1, s3, j_loop
    addi t0, t0, 1
    blt  t0, s3, i_loop
    # read C[0][0] and C[7][7]
    la   s2, C
    lw   a0, 0(s2)
    lw   a1, 252(s2)
halt:
    beq  x0, x0, halt

.section .data
.align 4
A:
    .word 1,0,0,0,0,0,0,0
    .word 0,1,0,0,0,0,0,0
    .word 0,0,1,0,0,0,0,0
    .word 0,0,0,1,0,0,0,0
    .word 0,0,0,0,1,0,0,0
    .word 0,0,0,0,0,1,0,0
    .word 0,0,0,0,0,0,1,0
    .word 0,0,0,0,0,0,0,1
B:
    .word 1,2,3,4,5,6,7,8
    .word 2,3,4,5,6,7,8,9
    .word 3,4,5,6,7,8,9,10
    .word 4,5,6,7,8,9,10,11
    .word 5,6,7,8,9,10,11,12
    .word 6,7,8,9,10,11,12,13
    .word 7,8,9,10,11,12,13,14
    .word 8,9,10,11,12,13,14,15
C:
    .word 0,0,0,0,0,0,0,0
    .word 0,0,0,0,0,0,0,0
    .word 0,0,0,0,0,0,0,0
    .word 0,0,0,0,0,0,0,0
    .word 0,0,0,0,0,0,0,0
    .word 0,0,0,0,0,0,0,0
    .word 0,0,0,0,0,0,0,0
    .word 0,0,0,0,0,0,0,0
