riscv_basic_asm.s:
.align 4
.section .text
.globl _start
    # Refer to the RISC-V ISA Spec for the functionality of
    # the instructions in this test program.
_start:
    
lw x6, four 
la x2, garbage
la x4, endgarbage
sub x4, x4, x6
sub x4, x4, x6
loop1:
    lw x3, 4(x2)
    addi x3, x3, 0x1
    sw x3, 4(x2)
    addi x2, x2, 0x4
    bne x2, x4, loop1

    lw x6, two
    la x2, garbage
loop2:
    lh x3, 2(x2)
    addi x3, x3, 0x1
    sh x3, 2(x2)
    addi x2, x2, 0x2
    bne x2, x4, loop2

    lw x6, one
    la x2, garbage
loop3:
    lb x3, 1(x2)
    addi x3, x3, 0x1
    sb x3, 1(x2)
    addi x2, x2, 0x1
    bne x2, x4, loop3

    lw x6, two
    la x2, garbage
loop4:
    lhu x3, 2(x2)
    addi x3, x3, 0x1
    sh x3, 2(x2)
    addi x2, x2, 0x2
    bne x2, x4, loop4

    lw x6, one
    la x2, garbage
loop5:
    lbu x3, 1(x2)
    addi x3, x3, 0x1
    sb x3, 1(x2)
    addi x2, x2, 0x1
    bne x2, x4, loop5

    la x2, garbage
    # Additional random loads/stores
    lw x6, 36(x2)
    lw x8, 72(x2)
    lh x9, 90(x2)
    lhu x13, 142(x2)
    lb x10, 189(x2)
    lbu x14, 145(x2)
    sw x6, 40(x2)
    sw x8, 76(x2)
    sh x9, 92(x2)
    sb x10, 191(x2)

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
.word 0xa7fcd9a8
.word 0x242d0230
.word 0xfa7b6dc0
.word 0xd76fe35e
.word 0xf8059098
.word 0xb08f9fd5
.word 0xd762bf7e
.word 0xcad7288c
.word 0x2a79caba
.word 0xe15d3263
.word 0x86489bc1
.word 0x02a68873
.word 0x8635b6ed
.word 0xdb3e07ac
.word 0xb8fbdab8
.word 0x8df23283
.word 0xa2812278
.word 0x461503f8
.word 0xe559acce
.word 0x2bf6b1ec
.word 0x0bfea459
.word 0xb534d827
.word 0xc1ee0366
.word 0x9ea5e7c3
.word 0x63ddf35f
.word 0x74d76d3b
.word 0xd27a7b15
.word 0x9d0a2041
.word 0x778024eb
.word 0x25e2f9d5
.word 0x9e83555a
.word 0xb883e79c
.word 0x32478221
.word 0xdd6bb546
.word 0xd0cab208
.word 0x8e3c4720
.word 0xec38e48c
.word 0x96f38131
.word 0x6c22543e
.word 0x0cafb18d
.word 0xddbc7e73
.word 0x366d731f
.word 0x7fff4cba
.word 0xca0d8e14
.word 0x1abd5d81
.word 0x7272b42f
.word 0x5f2e985d
.word 0x848c1a71
.word 0x8b570ae9
.word 0x42973bbc
.word 0x86d17694
.word 0xefd593c5
.word 0xf3348d3c
.word 0x4cfdd63a
.word 0xd37c194e
.word 0x39c4aa1c
.word 0x76f6d0f4
.word 0xb0ca28f8
.word 0xfc5680cc
.word 0xdf40588a
.word 0xfbfdc92c
.word 0x3b36aed2
.word 0x5cb0504f
.word 0x5ce12d8c
.word 0xbe44e386
.word 0x79b26625
.word 0xfbe8fef6
.word 0x41beda25
.word 0xb683a773
.word 0x34b65ec8
.word 0xcc69f98b
.word 0x7d049d27
.word 0x503421b8
.word 0x9bbbf441
.word 0x178c15ca
.word 0x8af2f121
.word 0x21366ea4
.word 0x19b5afab
.word 0xbc2b266f
.word 0x420b8281
.word 0xdc8d4b0c
.word 0x4f26b210
.word 0x116deb7d
.word 0xc03fb291
.word 0xb15ea254
.word 0x02e67130
.word 0x1864e6df
.word 0xd8115572
.word 0xc52551a6
.word 0xb8a881b9
.word 0x6bf028cc
.word 0x541c540c
.word 0xf5834963
.word 0xc949ac43
.word 0x351f0e60
.word 0x5cec08c0
.word 0x72b62f40
.word 0xd77504fb
.word 0x70628528
.word 0x76774915
.word 0x0493ed9f
endgarbage:

.section ".tohost"
.globl tohost
tohost: .dword 0
.section ".fromhost"
.globl fromhost
fromhost: .dword 0
