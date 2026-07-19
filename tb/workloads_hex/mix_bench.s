# Mixed control / mul / memory microbenchmark.
.align 4
.section .text
.globl _start
_start:
    la   s0, arr
    li   s1, 128           # elements
    li   s2, 32            # passes
    li   a0, 0             # checksum
pass:
    mv   t0, s0
    mv   t1, s1
    li   t2, 0             # index
elem:
    lw   t3, 0(t0)
    andi t4, t2, 1
    beq  t4, x0, even
    mul  t3, t3, t2
    addi t3, t3, 3
    j    store
even:
    add  t3, t3, t2
    xor  t3, t3, t1
store:
    sw   t3, 0(t0)
    add  a0, a0, t3
    addi t0, t0, 4
    addi t2, t2, 1
    addi t1, t1, -1
    bne  t1, x0, elem
    addi s2, s2, -1
    bne  s2, x0, pass
halt:
    beq  x0, x0, halt

.section .data
.align 4
arr:
    .word 1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16
    .word 17,18,19,20,21,22,23,24,25,26,27,28,29,30,31,32
    .word 33,34,35,36,37,38,39,40,41,42,43,44,45,46,47,48
    .word 49,50,51,52,53,54,55,56,57,58,59,60,61,62,63,64
    .word 65,66,67,68,69,70,71,72,73,74,75,76,77,78,79,80
    .word 81,82,83,84,85,86,87,88,89,90,91,92,93,94,95,96
    .word 97,98,99,100,101,102,103,104,105,106,107,108,109,110,111,112
    .word 113,114,115,116,117,118,119,120,121,122,123,124,125,126,127,128
