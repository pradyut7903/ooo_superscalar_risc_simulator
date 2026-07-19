riscv_basic_asm.s:
.align 4
.section .text
.globl _start
    # Refer to the RISC-V ISA Spec for the functionality of
    # the instructions in this test program.
_start:
    
lw x6, four 
la x2, garbage
la x3, endgarbage
la x4, store_section
la x5, end_store_section
sub x3, x3, x6
sub x5, x5, x6
loop1:
    lw x10, 4(x2)
    addi x10, x10, 0xce
    sw x10, 4(x4)
    lw x11, 0(x4)
    addi x2, x2, 0x4
    addi x4, x4, 0x4
    bne x2, x3, loop1

    la x2, garbage
    la x4, store_section
loop2:
    lh x10, 2(x2)
    addi x10, x10, 0xce
    sh x10, 2(x4)
    lh x11, 0(x4)
    addi x2, x2, 0x2
    addi x4, x4, 0x2
    bne x2, x3, loop2

    la x2, garbage
    la x4, store_section
loop3:
    lb x10, 1(x2)
    addi x10, x10, 0xce
    sb x10, 1(x4)
    lb x11, 0(x4)
    addi x2, x2, 0x1
    addi x4, x4, 0x1
    bne x2, x3, loop3

    la x2, garbage
    la x4, store_section
loop4:
    lhu x10, 2(x2)
    addi x10, x10, 0xce
    sh x10, 2(x4)
    lhu x11, 0(x4)
    addi x2, x2, 0x2
    addi x4, x4, 0x2
    bne x2, x3, loop4

    la x2, garbage
    la x4, store_section
loop5:
    lbu x10, 1(x2)
    addi x10, x10, 0xce
    sb x10, 1(x4)
    lbu x11, 1(x4)
    addi x2, x2, 0x1
    addi x4, x4, 0x1
    bne x2, x3, loop5

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

one: .word 0x00000001
two: .word 0x00000002
four: .word 0x00000004

garbage:
.word 0x0be65160
.word 0x74a8ef9c
.word 0xc79bdd3e
.word 0x501eed5e
.word 0x0bca86a5
.word 0xe575dc7f
.word 0x86f217c9
.word 0xddd6f891
.word 0xfb44e397
.word 0xd492d4c5
.word 0x08f7bc42
.word 0xdf502b82
.word 0xcf1b7478
.word 0x06b92fcd
.word 0x9b2f8bf4
.word 0xee3e837f
.word 0x414e3478
.word 0xcea364df
.word 0x02b3280d
.word 0x817698fa
.word 0xdf5814bb
.word 0xc892bbc5
.word 0x7eaae4f2
.word 0x73fda6d0
.word 0x5e461065
.word 0x9e64f8f1
.word 0xc5122788
.word 0x97b15009
.word 0xb054fee2
.word 0x4e8e293b
.word 0x1ca772f0
.word 0xbd837ffa
.word 0xd1f74af1
.word 0xdff3fc0b
.word 0x7a178ee3
.word 0x994addc6
.word 0x5c7ec64e
.word 0x16e5aa8f
.word 0x91bb574e
.word 0xc4c1534a
.word 0x902cfa49
.word 0x47da8dd4
.word 0x6c70b755
.word 0x7a9b470f
.word 0x6cd2dbe8
.word 0x0ad63dcc
.word 0x0538fbfd
.word 0x52bdbbc4
.word 0xc7a3eb1f
.word 0xd7a070f9
.word 0xbec3d09f
.word 0x397772c8
.word 0xb6498b7a
.word 0x57bc2029
.word 0x1598b1a1
.word 0xb71c9ea7
.word 0x2aacd2d0
.word 0x3ee59609
.word 0x5ac3a8e6
.word 0x6e835028
.word 0x51237cd7
.word 0x702a055d
.word 0x6b7de48f
.word 0xc3dbd4c6
.word 0xd3394510
.word 0x1059b166
.word 0x94312840
.word 0x595ccbe6
.word 0xe5d5f2fb
.word 0x95d72cc3
.word 0x12103f49
.word 0xbd8d13dd
.word 0x92668166
.word 0x9109b2b1
.word 0x950c0d65
.word 0x0dfa2f4c
.word 0x34a6c9f5
.word 0xe28b1e17
.word 0xf688db98
.word 0xbd1c156a
.word 0xc76cb8fe
.word 0x785a3d21
.word 0x605239b8
.word 0x8436ea6d
.word 0x5c2ecdc5
.word 0xb5495f66
.word 0x83b2dcd6
.word 0x8972688d
.word 0x90b847a6
.word 0x82e31141
.word 0x5cb3fd5e
.word 0xbf6f5720
.word 0x5443d60e
.word 0x63415a55
.word 0xfd98cd03
.word 0x459c2c1d
.word 0xf9a197a0
.word 0x975dc382
.word 0x88c001a6
endgarbage:
.word 0xa7fcd9a8

store_section:
.word 0x00000000
.word 0x00000000
.word 0x00000000
.word 0x00000000
.word 0x00000000
.word 0x00000000
.word 0x00000000
.word 0x00000000
.word 0x00000000
.word 0x00000000
.word 0x00000000
.word 0x00000000
.word 0x00000000
.word 0x00000000
.word 0x00000000
.word 0x00000000
.word 0x00000000
.word 0x00000000
.word 0x00000000
.word 0x00000000
.word 0x00000000
.word 0x00000000
.word 0x00000000
.word 0x00000000
.word 0x00000000
.word 0x00000000
.word 0x00000000
.word 0x00000000
.word 0x00000000
.word 0x00000000
.word 0x00000000
.word 0x00000000
.word 0x00000000
.word 0x00000000
.word 0x00000000
.word 0x00000000
.word 0x00000000
.word 0x00000000
.word 0x00000000
.word 0x00000000
.word 0x00000000
.word 0x00000000
.word 0x00000000
.word 0x00000000
.word 0x00000000
.word 0x00000000
.word 0x00000000
.word 0x00000000
.word 0x00000000
.word 0x00000000
.word 0x00000000
.word 0x00000000
.word 0x00000000
.word 0x00000000
.word 0x00000000
.word 0x00000000
.word 0x00000000
.word 0x00000000
.word 0x00000000
.word 0x00000000
.word 0x00000000
.word 0x00000000
.word 0x00000000
.word 0x00000000
.word 0x00000000
.word 0x00000000
.word 0x00000000
.word 0x00000000
.word 0x00000000
.word 0x00000000
.word 0x00000000
.word 0x00000000
.word 0x00000000
.word 0x00000000
.word 0x00000000
.word 0x00000000
.word 0x00000000
.word 0x00000000
.word 0x00000000
.word 0x00000000
.word 0x00000000
.word 0x00000000
.word 0x00000000
.word 0x00000000
.word 0x00000000
.word 0x00000000
.word 0x00000000
.word 0x00000000
.word 0x00000000
.word 0x00000000
.word 0x00000000
.word 0x00000000
.word 0x00000000
.word 0x00000000
.word 0x00000000
.word 0x00000000
.word 0x00000000
.word 0x00000000
.word 0x00000000
end_store_section:
.word 0x00000000

.section ".tohost"
.globl tohost
tohost: .dword 0
.section ".fromhost"
.globl fromhost
fromhost: .dword 0
