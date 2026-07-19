/* Bare-metal CRT for ooo_rtl Harvard RV32IM core.
 *
 * Halt sentinel must NOT sit between CRT and the rest of .text — WIDTH-wide
 * fetch would see INSTR_INVALID and raise EOP before main runs.  Put it in
 * .text.end, linked after all other code.
 */
    .section .text.startup, "ax", @progbits
    .globl   _start
    .type    _start, @function
_start:
    .option push
    .option norelax
    la   sp, _stack_top
    .option pop

    /* Clear BSS */
    la   t0, _bss_start
    la   t1, _bss_end
1:
    bgeu t0, t1, 2f
    sw   zero, 0(t0)
    addi t0, t0, 4
    j    1b
2:
    call main
    j    __halt
    .size _start, .-_start

    .section .text.end, "ax", @progbits
    .globl   __halt
    .type    __halt, @function
__halt:
    /* Core halt: INSTR_INVALID sentinel for fetch EOP. */
    .word 0xffffffff
    .size __halt, .-__halt
