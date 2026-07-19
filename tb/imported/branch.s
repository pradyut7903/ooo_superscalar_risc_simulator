.align 4
.section .text
.globl _start

_start:
    # Setup initial values directly in registers
    lui x1, 0x1      # Using `lui` to set upper immediate for demonstration purposes
    lui x2, 0x2
    lui x3, 0x1
    lui x4, 0x3
    lui x5, 0x2
    lui x6, 0x3

    # Loop and Branch Logic without using memory operations
loop:
    addi x1, x1, 4
    bne x1, x2, loop

brA: addi x3, x3, 4
     blt  x3, x5, brA

     addi x5, x5, 4
     blt  x5, x4, brA    
    
     lui x5, 0x2
     lui x6, 0x3

top: addi x5, x5, 4
#     beq x0, x0, brB
#brB: beq x0, x0, brC
#brC: beq x0, x0, brD
brD: bltu x6, x5, top

halt:                 # Infinite loop to keep the processor
    beq x0, x0, halt  # from trying to execute code further

