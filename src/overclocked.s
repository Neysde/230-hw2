# overclocked - cmpe 230 assignment 2
#
# input  (stdin) : <RULE_PREP> <RULE_COOK> <RULE_PLATE> <ORDER> <ORDER> ...
#                  RULE  = FIRST | QUICK | TURNS-q
#                  ORDER = ID-arrival-prep-cook-plate
# output (stdout): prep timeline, cook timeline, plate timeline, serve times
#
# calling convention used by all routines
#   args in rdi rsi rdx and result in rax
#   rbx r12 r13 are saved by the callee
#   rax rcx rdx r8 r9 r10 may be clobbered

.equ MAX_ORD, 10            # max number of orders
.equ TL_SIZE, 1024          # bytes per station timeline
.equ BUF_SIZE, 4096         # input and output buffer size
.equ R_FIRST, 0             # rule codes
.equ R_QUICK, 1
.equ R_TURNS, 2
.equ DONE, 3                # station value of a served order

.section .bss
.balign 8
# per order data indexed by input position
arrival:  .skip MAX_ORD*8
work:     .skip MAX_ORD*3*8     # work[i*3 + s]
stn:      .skip MAX_ORD*8       # current station or DONE
etick:    .skip MAX_ORD*8       # entry tick at current station
rem:      .skip MAX_ORD*8       # remaining work at current station
serve:    .skip MAX_ORD*8       # serve time

# per station data indexed by station 0 1 2
rule:     .skip 3*8
quant:    .skip 3*8             # q for turns
cur:      .skip 3*8             # dish in progress or -1
used:     .skip 3*8             # ticks used in current turn
pend:     .skip 3*8             # dish whose turn ended or -1
sel:      .skip 3*8             # dish chosen for this tick or -1
qlen:     .skip 3*8             # length of turns line
queue:    .skip 3*MAX_ORD*8     # turns line of station s starts at s*MAX_ORD

# globals
n_ord:    .skip 8               # number of orders
tick:     .skip 8               # current tick
served:   .skip 8               # number of served orders

ids:      .skip 16              # order letters
inbuf:    .skip BUF_SIZE
outbuf:   .skip BUF_SIZE
timeline: .skip 3*TL_SIZE       # timeline of station s starts at s*TL_SIZE

.section .text
.global _start

_start:
    call    read_input
    call    parse_input
    call    init_state
    call    simulate
    call    print_output

    # exit with status 0
    mov     $60, %rax
    xor     %rdi, %rdi
    syscall


##################################
# input reading
##################################

# read stdin into inbuf until newline or eof
read_input:
    push    %rbx
    xor     %rbx, %rbx              # bytes read so far
.ri_loop:
    mov     $BUF_SIZE-1, %rdx
    sub     %rbx, %rdx              # space left in buffer
    jle     .ri_done
    mov     $0, %rax                # sys_read
    mov     $0, %rdi                # stdin
    lea     inbuf(%rbx), %rsi
    syscall
    cmp     $0, %rax
    jle     .ri_done                # eof or error
    add     %rax, %rbx
    cmpb    $'\n', inbuf-1(%rbx)    # stop at end of line
    jne     .ri_loop
.ri_done:
    movb    $0, inbuf(%rbx)         # terminate the string
    pop     %rbx
    ret


##################################
# parsing
##################################

# read a decimal number at rsi
# returns value in rax and leaves rsi at the first non digit
parse_num:
    xor     %rax, %rax
.pn_loop:
    movzbq  (%rsi), %rdx
    sub     $'0', %rdx
    cmp     $9, %rdx
    ja      .pn_done                # unsigned check rejects non digits
    imul    $10, %rax
    add     %rdx, %rax
    inc     %rsi
    jmp     .pn_loop
.pn_done:
    ret

# read one rule token at rsi for station rdi
# leaves rsi right after the token
parse_rule:
    movzbq  (%rsi), %rax
    cmp     $'F', %al
    je      .pr_first
    cmp     $'Q', %al
    je      .pr_quick
    movq    $R_TURNS, rule(,%rdi,8) # token is TURNS-q
    add     $6, %rsi                # skip "TURNS-"
    call    parse_num
    mov     %rax, quant(,%rdi,8)
    ret
.pr_first:
    movq    $R_FIRST, rule(,%rdi,8)
    add     $5, %rsi                # skip "FIRST"
    ret
.pr_quick:
    movq    $R_QUICK, rule(,%rdi,8)
    add     $5, %rsi                # skip "QUICK"
    ret

# parse three rules and then all orders from inbuf
parse_input:
    push    %rbx
    push    %r12
    mov     $inbuf, %rsi
    xor     %r12, %r12              # station index
.pi_rule:
    mov     %r12, %rdi
    call    parse_rule
    inc     %rsi                    # skip space
    inc     %r12
    cmp     $3, %r12
    jl      .pi_rule

    xor     %rbx, %rbx              # order index
.pi_order:
    movzbq  (%rsi), %rax            # orders start with an uppercase id
    cmp     $'A', %al
    jb      .pi_done
    cmp     $'Z', %al
    ja      .pi_done
    movb    %al, ids(%rbx)
    add     $2, %rsi                # skip id and hyphen
    call    parse_num
    mov     %rax, arrival(,%rbx,8)

    lea     (%rbx,%rbx,2), %rcx     # rcx = i*3 index into work
    mov     $3, %r12                # three work times follow
.pi_work:
    inc     %rsi                    # skip hyphen
    call    parse_num
    mov     %rax, work(,%rcx,8)
    inc     %rcx
    dec     %r12
    jnz     .pi_work

    inc     %rsi                    # skip space or newline
    inc     %rbx
    cmp     $MAX_ORD, %rbx
    jl      .pi_order
.pi_done:
    mov     %rbx, n_ord
    pop     %r12
    pop     %rbx
    ret


##################################
# order movement
##################################

# find first station from rsi on where order rdi has work
# returns station in rax or DONE if none left
next_station:
    lea     (%rdi,%rdi,2), %rcx     # rcx = i*3
    mov     %rsi, %rax
.ns_loop:
    cmp     $DONE, %rax
    jge     .ns_done
    lea     (%rcx,%rax), %rdx
    cmpq    $0, work(,%rdx,8)
    jne     .ns_done                # station needs work
    inc     %rax                    # zero work so skip it
    jmp     .ns_loop
.ns_done:
    ret

# move order rdi to its next station starting from station rsi
# rdx is the tick it enters there
# if no station is left the order is served at tick rdx
place_order:
    push    %rdx
    call    next_station
    pop     %rdx
    mov     %rax, stn(,%rdi,8)
    cmp     $DONE, %rax
    je      .pl_served
    mov     %rdx, etick(,%rdi,8)
    lea     (%rdi,%rdi,2), %rcx
    add     %rax, %rcx
    mov     work(,%rcx,8), %rcx
    mov     %rcx, rem(,%rdi,8)      # full work of the new station
    ret
.pl_served:
    mov     %rdx, serve(,%rdi,8)
    incq    served
    ret

# reset station state and send every order to its first station
init_state:
    push    %rbx
    xor     %rbx, %rbx
.is_station:
    movq    $-1, cur(,%rbx,8)
    movq    $-1, pend(,%rbx,8)
    inc     %rbx
    cmp     $3, %rbx
    jl      .is_station

    xor     %rbx, %rbx
.is_order:
    cmp     n_ord, %rbx
    jge     .is_done
    mov     %rbx, %rdi
    xor     %rsi, %rsi              # check from prep
    mov     arrival(,%rbx,8), %rdx
    call    place_order
    inc     %rbx
    jmp     .is_order
.is_done:
    pop     %rbx
    ret


##################################
# turns line helpers
##################################

# append order rsi to the back of the line of station rdi
enqueue:
    imul    $MAX_ORD, %rdi, %rdx
    add     qlen(,%rdi,8), %rdx
    mov     %rsi, queue(,%rdx,8)
    incq    qlen(,%rdi,8)
    ret

# step 1 of a tick
# new entries join turns lines in input order
# then dishes whose turn ended go to the back
update_queues:
    push    %rbx
    xor     %rbx, %rbx
.uq_new:
    cmp     n_ord, %rbx
    jge     .uq_pend
    mov     stn(,%rbx,8), %rdi
    cmp     $DONE, %rdi
    je      .uq_next
    mov     etick(,%rbx,8), %rax
    cmp     tick, %rax
    jne     .uq_next                # not entering at this tick
    cmpq    $R_TURNS, rule(,%rdi,8)
    jne     .uq_next                # only turns uses a line
    mov     %rbx, %rsi
    call    enqueue
.uq_next:
    inc     %rbx
    jmp     .uq_new

.uq_pend:
    xor     %rdi, %rdi
.uq_ploop:
    mov     pend(,%rdi,8), %rsi
    cmp     $-1, %rsi
    je      .uq_pnext
    call    enqueue
    movq    $-1, pend(,%rdi,8)
.uq_pnext:
    inc     %rdi
    cmp     $3, %rdi
    jl      .uq_ploop
    pop     %rbx
    ret


##################################
# chef rules
# each takes station in rdi and returns order in rax or -1
##################################

select_order:
    mov     rule(,%rdi,8), %rax
    cmp     $R_FIRST, %rax
    je      select_first
    cmp     $R_QUICK, %rax
    je      select_quick
    jmp     select_turns

# keep current dish or take earliest entry then lowest input index
select_first:
    mov     cur(,%rdi,8), %rax
    cmp     $-1, %rax
    jne     .sf_ret                 # still working on a dish
    mov     $0x7fffffff, %r8        # best entry tick
    xor     %rcx, %rcx
.sf_loop:
    cmp     n_ord, %rcx
    jge     .sf_set
    cmp     stn(,%rcx,8), %rdi
    jne     .sf_next                # not at this station
    mov     etick(,%rcx,8), %rdx
    cmp     tick, %rdx
    jg      .sf_next                # has not entered yet
    cmp     %r8, %rdx
    jge     .sf_next                # strict check keeps lower index on ties
    mov     %rdx, %r8
    mov     %rcx, %rax
.sf_next:
    inc     %rcx
    jmp     .sf_loop
.sf_set:
    mov     %rax, cur(,%rdi,8)
.sf_ret:
    ret

# least remaining work then earliest entry then lowest input index
select_quick:
    mov     $-1, %rax
    mov     $0x7fffffff, %r8        # best remaining work
    mov     $0x7fffffff, %r9        # best entry tick
    xor     %rcx, %rcx
.sq_loop:
    cmp     n_ord, %rcx
    jge     .sq_ret
    cmp     stn(,%rcx,8), %rdi
    jne     .sq_next
    mov     etick(,%rcx,8), %rdx
    cmp     tick, %rdx
    jg      .sq_next
    mov     rem(,%rcx,8), %r10
    cmp     %r8, %r10
    jl      .sq_take                # less work wins
    jg      .sq_next
    cmp     %r9, %rdx
    jge     .sq_next                # same work so earlier entry wins
.sq_take:
    mov     %r10, %r8
    mov     %rdx, %r9
    mov     %rcx, %rax
.sq_next:
    inc     %rcx
    jmp     .sq_loop
.sq_ret:
    ret

# keep current dish during its turn or pop the front of the line
select_turns:
    mov     cur(,%rdi,8), %rax
    cmp     $-1, %rax
    jne     .st_ret
    mov     qlen(,%rdi,8), %rcx
    test    %rcx, %rcx
    jz      .st_ret                 # empty line so chef is idle
    imul    $MAX_ORD, %rdi, %rdx    # start of this line
    mov     queue(,%rdx,8), %rax    # front dish
    dec     %rcx
    mov     %rcx, qlen(,%rdi,8)
.st_shift:                          # move the rest one slot forward
    test    %rcx, %rcx
    jz      .st_take
    mov     queue+8(,%rdx,8), %r8
    mov     %r8, queue(,%rdx,8)
    inc     %rdx
    dec     %rcx
    jmp     .st_shift
.st_take:
    mov     %rax, cur(,%rdi,8)
    movq    $0, used(,%rdi,8)       # new turn starts
.st_ret:
    ret


##################################
# simulation
##################################

simulate:
    push    %rbx
    push    %r12
    movq    $0, tick
.sim_tick:
    mov     served, %rax
    cmp     n_ord, %rax
    jge     .sim_end                # every order is served

    # step 1 update queues
    call    update_queues

    # step 2 select orders at all stations
    xor     %r12, %r12
.sim_sel:
    mov     %r12, %rdi
    call    select_order
    mov     %rax, sel(,%r12,8)
    inc     %r12
    cmp     $3, %r12
    jl      .sim_sel

    # step 3 record and process
    xor     %r12, %r12
.sim_proc:
    imul    $TL_SIZE, %r12, %rax
    add     tick, %rax              # timeline slot of this tick
    mov     sel(,%r12,8), %rbx
    cmp     $-1, %rbx
    je      .sim_idle
    movb    ids(%rbx), %dl
    movb    %dl, timeline(%rax)
    decq    rem(,%rbx,8)
    incq    used(,%r12,8)           # only read by turns
    jmp     .sim_pnext
.sim_idle:
    movb    $'.', timeline(%rax)
.sim_pnext:
    inc     %r12
    cmp     $3, %r12
    jl      .sim_proc

    # step 4 handle completions and ended turns
    xor     %r12, %r12
.sim_comp:
    mov     sel(,%r12,8), %rbx
    cmp     $-1, %rbx
    je      .sim_cnext
    cmpq    $0, rem(,%rbx,8)
    jne     .sim_unfinished
    movq    $-1, cur(,%r12,8)       # station is free
    mov     %rbx, %rdi
    lea     1(%r12), %rsi           # search from next station
    mov     tick, %rdx
    inc     %rdx                    # enters or is served at t+1
    call    place_order
    jmp     .sim_cnext
.sim_unfinished:
    cmpq    $R_TURNS, rule(,%r12,8)
    jne     .sim_cnext
    mov     used(,%r12,8), %rax
    cmp     quant(,%r12,8), %rax
    jl      .sim_cnext              # turn not over yet
    mov     %rbx, pend(,%r12,8)     # back of the line next tick
    movq    $-1, cur(,%r12,8)
.sim_cnext:
    inc     %r12
    cmp     $3, %r12
    jl      .sim_comp

    # step 5 advance time
    incq    tick
    jmp     .sim_tick
.sim_end:
    pop     %r12
    pop     %rbx
    ret


##################################
# output
##################################

# write decimal rax at rdi and advance rdi
itoa:
    mov     $10, %r8
    xor     %rcx, %rcx              # digit count
.it_div:
    xor     %rdx, %rdx
    div     %r8                     # rax = rax / 10 and rdx = digit
    add     $'0', %rdx
    push    %rdx                    # digits come out in reverse
    inc     %rcx
    test    %rax, %rax
    jnz     .it_div
.it_pop:
    pop     %rax
    movb    %al, (%rdi)
    inc     %rdi
    dec     %rcx
    jnz     .it_pop
    ret

# write rdx bytes from rsi to stdout handling partial writes
write_all:
.wa_loop:
    test    %rdx, %rdx
    jle     .wa_done
    mov     $1, %rax                # sys_write
    mov     $1, %rdi                # stdout
    push    %rsi
    push    %rdx
    syscall
    pop     %rdx
    pop     %rsi
    cmp     $0, %rax
    jle     .wa_done                # write error
    add     %rax, %rsi
    sub     %rax, %rdx
    jmp     .wa_loop
.wa_done:
    ret

# build the four output lines in outbuf and print them
print_output:
    push    %rbx
    push    %r12
    mov     $outbuf, %rdi
    xor     %r12, %r12
.out_line:                          # copy T chars of each timeline
    imul    $TL_SIZE, %r12, %rsi
    add     $timeline, %rsi
    mov     tick, %rcx              # tick equals T at the end
    rep movsb
    movb    $'\n', (%rdi)
    inc     %rdi
    inc     %r12
    cmp     $3, %r12
    jl      .out_line

    xor     %rbx, %rbx
.out_serve:                         # ID=time entries in input order
    cmp     n_ord, %rbx
    jge     .out_end
    test    %rbx, %rbx
    jz      .out_entry              # no space before the first entry
    movb    $' ', (%rdi)
    inc     %rdi
.out_entry:
    movb    ids(%rbx), %al
    movb    %al, (%rdi)
    movb    $'=', 1(%rdi)
    add     $2, %rdi
    mov     serve(,%rbx,8), %rax
    call    itoa
    inc     %rbx
    jmp     .out_serve
.out_end:
    movb    $'\n', (%rdi)
    inc     %rdi
    mov     %rdi, %rdx
    sub     $outbuf, %rdx           # total length
    mov     $outbuf, %rsi
    call    write_all
    pop     %r12
    pop     %rbx
    ret
