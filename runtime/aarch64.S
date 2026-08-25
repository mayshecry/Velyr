/* AArch64 generic timer and spin-loop hint. */
.text
.p2align 4
.global vy_asm_cycles
.type vy_asm_cycles, %function
vy_asm_cycles:
    isb
    mrs x0, cntvct_el0
    ret
.size vy_asm_cycles, .-vy_asm_cycles

.p2align 4
.global vy_asm_pause
.type vy_asm_pause, %function
vy_asm_pause:
    yield
    ret
.size vy_asm_pause, .-vy_asm_pause

.section .note.GNU-stack,"",@progbits
