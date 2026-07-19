ooo_test.s: 
.align 4
.section .text
.globl _start
    # This program will provide a simple test for
    # demonstrating OOO-ness

    # This test is NOT exhaustive
_start:
    # Test for BEQ (Branch if Equal)
    li x1, 10
    li x2, 10
    bne x1, x2, beq_target  # should branch because x1 == x2
    li x10, 0  # not executed if branch is taken
    j next_test_1
beq_target:
    li x10, 1  # executed only if branch is taken

next_test_1:
    # Test for BNE (Branch if Not Equal)
    li x3, 10
    li x4, 5
    beq x3, x4, bne_target  # should branch because x3 != x4
    li x11, 0
    j next_test_2
bne_target:
    li x11, 1

next_test_2:
    # Test for BLT (Branch if Less Than)
    li x5, 5
    li x6, 10
    bge x5, x6, blt_target  # should branch because 5 < 10
    li x12, 0
    j next_test_3
blt_target:
    li x12, 1

next_test_3:
    # Test for BGE (Branch if Greater Than or Equal)
    li x7, 10
    li x8, 5
    bltu x7, x8, bge_target  # should branch because 10 >= 5
    li x13, 0
    j next_test_4
bge_target:
    li x13, 1

next_test_4:
    # Test for BLTU (Branch if Less Than, Unsigned)
    li x9, 0xFFFFFFFF
    li x15, 0x1
    bgeu x9, x15, bltu_target  # should NOT branch because 0xFFFFFFFF < 0x1 unsigned
    li x14, 0
    j next_test_5
bltu_target:
    li x14, 1

next_test_5:
    # Test for BGEU (Branch if Greater Than or Equal, Unsigned)
    li x16, 0x1
    li x17, 0xFFFFFFFF
    bgeu x16, x17, bgeu_target  # should branch because 0x1 >= 0xFFFFFFFF unsigned
    li x18, 0
    j end
bgeu_target:
    li x18, 1

end:        
    slti x0, x0, -256
                       