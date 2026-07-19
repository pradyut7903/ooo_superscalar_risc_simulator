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


    lw x3, 0(x1)
    lw x5, 4(x1)
    lw x4, 8(x1)

    addi x3, x3, 0x1
    slti x4, x3, 0x5
    sltiu x3, x4, 0x6
    xori x3, x3, 0xff
    lw x3, 12(x1)
    ori  x3, x3, 0xff
    andi x3, x3, 0xff
    slli x4, x4, 4
    srli x4, x4, 4
    srai x4, x4, 8
    lw x5, 20(x1)
    add  x5, x3, x4
    sub  x3, x4, x5
    sll  x4, x3, x5
    slt  x6, x3, x4
    sltu x5, x4, x6
    lw x3, 24(x1)
    lw x5, 16(x1)
    xor x3, x5, x6
    srl x5, x5, x3
    srl x6, x3, x5
    or x5, x5, x4
    and x6, x6, x5


    # when you are writing your own testcase, include these 4 lines and the halt.
    li  t0, 1
    la  t1, tohost
    sw  t0, 0(t1)
    sw  x0, 4(t1)
halt:                 # Infinite loop to keep the processor
    beq x0, x0, halt  # from trying to execute the data below.
                      # Your own programs should also make use
                      # of an infinite loop at the end.
    
    slti x0, x0, -256 # this is the magic instruction to end the simulation

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
.word 0x82ae5cb5
dataend:

.section ".tohost"
.globl tohost
tohost: .dword 0
.section ".fromhost"
.globl fromhost
fromhost: .dword 0
