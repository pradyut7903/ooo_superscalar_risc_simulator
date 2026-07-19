.section .text
.globl _start
    # This program demonstrates the `jalr` instruction in RISC-V assembly.

_start:
    # Setup
    auipc x1, 0      
    addi x1, x1, 24        # Adjust x1 to point to the correct label address, x1 = 0x60000004
    addi x5, x0, 1        # Setup x5 with a value to check that jump happens, x5 = 1

    # Jump to label and come back
    jalr x2, x1, 0        # Jump to the address in x1, return address saved in x2
    addi x5, x5, 1        # Increment x5 to mark that we returned here, x5 = 2

    # Termination (using the magic instruction)
    slti x0, x0, -256     # Magic instruction to end the simulation

    # Target label for jump, must align with the address placed in x1
.section .text
.globl some_label
some_label:
    addi x5, x5, 2        # Increment x5 at the jump target, x5 = 2
    jalr x0, x2, 0        # Return to the address in x2
