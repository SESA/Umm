/* Simplified version of the barrelfish syscall path. */

/* regular syscall path */
  .text
  .global syscall_path

syscall_path:
    pushq   %rcx            /* Save user-space RIP */
    pushq   %r11            /* Save user-space RFLAGS */

    mov     $0x0, %r11      /* God awful HACK because I don't understand EbbRT's SS. */
    movw    %r11w, %ss

    /* pushq   %r15       */
    /* movq    %rbp, %r15 */     /* NOTE: This is only for debugging convenience. */

    /* Umm path, syscall num in %rdi, void ptr in %rsi */
    callq   sys_call_handler     /* Process system call in C */

    /* popq    %r15 */

    popq    %r11            /* Restore RFLAGS */
    popq    %rcx            /* Restore RIP */
    sysretq             /* Return to user-space */
