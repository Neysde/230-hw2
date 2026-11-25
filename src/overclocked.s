# overclocked - CMPE 230 Project 2 (starter file)
#
# Input  (stdin) : <RULE_PREP> <RULE_COOK> <RULE_PLATE> <ORDER> <ORDER> ...
#                  RULE  = FIRST | QUICK | TURNS-q
#                  ORDER = ID-arrival-prep-cook-plate
# Output (stdout): Prep timeline, Cook timeline, Plate timeline, serve times
#
# Declare whatever memory your design needs. Use Linux system calls only;
# do not call C standard library functions.

.section .text
.global _start

_start:


##################################
# EXIT SYSCALL
##################################
.exit:
    mov     $60, %rax
    xor     %rdi, %rdi
    syscall
