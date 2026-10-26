
.text
.p2align 4
.globl vy_asm_cycles
.type vy_asm_cycles, @function
vy_asm_cycles:
    lfence
    rdtsc
    shlq $32, %rdx
    orq %rdx, %rax
    lfence
    ret
.size vy_asm_cycles, .-vy_asm_cycles

.p2align 4
.globl vy_asm_pause
.type vy_asm_pause, @function
vy_asm_pause:
    pause
    ret
.size vy_asm_pause, .-vy_asm_pause

.section .note.GNU-stack,"",@progbits
