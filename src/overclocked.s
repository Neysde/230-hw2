# overclocked - CMPE 230 Project 2 (starter file)
#
# Input  (stdin) : <RULE_PREP> <RULE_COOK> <RULE_PLATE> <ORDER> <ORDER> ...
#                  RULE  = FIRST | QUICK | TURNS-q
#                  ORDER = ID-arrival-prep-cook-plate
# Output (stdout): Prep timeline, Cook timeline, Plate timeline, serve times
#
# Declare whatever memory your design needs. Use Linux system calls only;
# do not call C standard library functions.
#
#   args in rdi rsi rdx and result in rax
#   rbx r12 r13 are callee-saved
#   rax rcx rdx r8 r9 r10 caller-saved (may be changed)

.equ MAX_ORD, 10            # max number of orders
.equ TL_SIZE, 1024          # bytes per station timeline (selected in base 2 according to T<=700)
.equ BUF_SIZE, 4096         # input and output buffer size (4096 covers both input and output size)
.equ R_FIRST, 0             # rule codes
.equ R_QUICK, 1
.equ R_TURNS, 2
.equ DONE, 3                # code of served order

.section .bss # uninitialized variable section
.balign 8 # align the address to a multiple of 8 (because of cache to increase performance (not necessary))
# per order data indexed by input position
arrival:  .skip MAX_ORD*8.      # leaves 80 bytes empty after the starting of arrival address
work:     .skip MAX_ORD*3*8     # work[i*3 + s] (i=order, s=section) (2d as 1d array)
stn:      .skip MAX_ORD*8       # stores current station or done index of orders
etick:    .skip MAX_ORD*8       # entry tick at current station of orders
rem:      .skip MAX_ORD*8       # remaining work at current station of orders
serve:    .skip MAX_ORD*8       # serve time of orders

# per station data indexed by station 0 1 2
rule:     .skip 3*8             # stores rule index (0,1,2) (first, quick, turns)
quant:    .skip 3*8             # q value for turns
cur:      .skip 3*8             # dish in progress or -1 (used by first and turns)
used:     .skip 3*8             # ticks used in current turn
pend:     .skip 3*8             # dish whose turn ended or -1 (turns)
sel:      .skip 3*8             # dish chosen for this tick or -1
qlen:     .skip 3*8             # length of turns queue 
queue:    .skip 3*MAX_ORD*8     # turns queue of station s starts at s*MAX_ORD

# global addresses
n_ord:    .skip 8               # number of orders
tick:     .skip 8               # current tick
served:   .skip 8               # number of served orders

ids:      .skip 16              # stores order letters
inbuf:    .skip BUF_SIZE        # buffers
outbuf:   .skip BUF_SIZE
timeline: .skip 3*TL_SIZE       # timeline of station s starts at s*TL_SIZE

.section .text # code section
.global _start # export _start so linker can see where program starts

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
    push    %rbx                    # saves value in rbx
    xor     %rbx, %rbx              # clears rbx
.ri_loop:
    mov     $BUF_SIZE-1, %rdx       # last byte is reserved for 0 terminator
    sub     %rbx, %rdx              # space left in buffer
    jle     .ri_done                # less or equal
    mov     $0, %rax                # sys_read
    mov     $0, %rdi                # stdin
    lea     inbuf(%rbx), %rsi       # write the inbuf
    syscall
    cmp     $0, %rax                # kernel returns number of copied bytes to rax
    jle     .ri_done                # eof or error
    add     %rax, %rbx              # saves the read byte count to rbx
    cmpb    $'\n', inbuf-1(%rbx)    # stop at end of line
    jne     .ri_loop                # go to loop again if it is not the end of the line
.ri_done:
    movb    $0, inbuf(%rbx)         # terminate the string
    pop     %rbx                    # restore the saved value before returning
    ret                             


##################################
# parsing
##################################

# read a decimal number at rsi
# returns value in rax and leaves rsi at the first non digit
parse_num:
    xor     %rax, %rax              # clears rax
.pn_loop:                           
    movzbq  (%rsi), %rdx
    sub     $'0', %rdx              # converts char to num
    cmp     $9, %rdx                
    ja      .pn_done                # unsigned check rejects non digits (jump if above)
    imul    $10, %rax               # digit shifting and adding
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
    call    parse_num               # we need to read the turn number using parse_num
    mov     %rax, quant(,%rdi,8)
    ret
.pr_first:
    movq    $R_FIRST, rule(,%rdi,8)
    add     $5, %rsi                # skip "FIRST"
    ret                             # returns to parse_input
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
    inc     %rsi                    # skips space
    inc     %r12                    # goes to next station index
    cmp     $3, %r12                
    jl      .pi_rule                # loops until 0,1,2 covered (3 stations) (jump if less)

    xor     %rbx, %rbx              # order index (clears rbx)
.pi_order:
    movzbq  (%rsi), %rax            # orders start with an uppercase id
    cmp     $'A', %al               
    jb      .pi_done                # jump if below a
    cmp     $'Z', %al
    ja      .pi_done                # jump if above z
    movb    %al, ids(%rbx)
    add     $2, %rsi                # skip id and -
    call    parse_num               # returns number in rax
    mov     %rax, arrival(,%rbx,8)

    lea     (%rbx,%rbx,2), %rcx     # rcx = i*3 index into work (starting of given order's row in work table)
    mov     $3, %r12                # r12 becomes loop counter there
.pi_work:
    inc     %rsi                    # skip -
    call    parse_num
    mov     %rax, work(,%rcx,8)     # saves number to the first index of row
    inc     %rcx
    dec     %r12
    jnz     .pi_work                # (jump if not zero)

    inc     %rsi                    # skip space
    inc     %rbx                    # go to the next order
    cmp     $MAX_ORD, %rbx 
    jl      .pi_order               # if we havent reached max order count keep checking for new orders
.pi_done:
    mov     %rbx, n_ord             # store the order count
    pop     %r12                    # r12 and rbx are callee saved
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
    cmp     $DONE, %rax.            # whether order will be served or not
    jge     .ns_done                # (jump if greater or equal)
    lea     (%rcx,%rax), %rdx       
    cmpq    $0, work(,%rdx,8)       # whether work is finished in that station or not
    jne     .ns_done                # station needs work
    inc     %rax                    # zero work so skip it (to next station)
    jmp     .ns_loop
.ns_done:
    ret

# move order rdi to its next station starting from station rsi
# rdx is the tick it enters there
# if no station is left the order is served at tick rdx
place_order:                        # place order is called when initialization and in simulate when an order has finished its current station
    push    %rdx
    call    next_station
    pop     %rdx                    # restore order entering tick
    mov     %rax, stn(,%rdi,8)      # store station value of our current order
    cmp     $DONE, %rax             
    je      .pl_served              # if its finished serve it
    mov     %rdx, etick(,%rdi,8)    # entry tick stores the tick at which order entered its current station
    lea     (%rdi,%rdi,2), %rcx     
    add     %rax, %rcx              
    mov     work(,%rcx,8), %rcx     # goes to the correspoding row of order and station that it is working and writes remaining work to rcx
    mov     %rcx, rem(,%rdi,8)      # full work of the new station
    ret
.pl_served:
    mov     %rdx, serve(,%rdi,8)    # stores the serve time of that order
    incq    served                  # increments served order counter
    ret                             # returns to caller

# reset station state and send every order to its first station
init_state:
    push    %rbx                    # saves caller's rbx
    xor     %rbx, %rbx              # clears rbx
.is_station:                        # arrays filled with 0 by default and except cur and pend, these are the default starting value
    movq    $-1, cur(,%rbx,8)       # current station has not started to any dish
    movq    $-1, pend(,%rbx,8)      # no dish waiting to rejoin
    inc     %rbx                    # go to the next station
    cmp     $3, %rbx                
    jl      .is_station             # repeat for all stations (0,1,2)

    xor     %rbx, %rbx              # clear rbx after finishing
.is_order:
    cmp     n_ord, %rbx             
    jge     .is_done                # (jump if greater or equal)
    mov     %rbx, %rdi              # arg 1: order index
    xor     %rsi, %rsi              # arg 2: clears rsi and starts checking from station 0 (prep)
    mov     arrival(,%rbx,8), %rdx  # arg 3: order enters at its arrival tick
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
    imul    $MAX_ORD, %rdi, %rdx    # rdx = s * 10 (where station row starts in queue)
    add     qlen(,%rdi,8), %rdx     # rdx = s*10(rdx) + first free slot in station row
    mov     %rsi, queue(,%rdx,8)    # queue[rdx(we calculated as first free slot of that station's row)]=order
    incq    qlen(,%rdi,8)           # increases the length value of that station queue
    ret

# step 1 of a tick
# new entries join turns lines in input order
# then dishes whose turn ended go to the back
update_queues:
    push    %rbx
    xor     %rbx, %rbx
.uq_new:
    cmp     n_ord, %rbx
    jge     .uq_pend                # (greater or equal)
    mov     stn(,%rbx,8), %rdi         
    cmp     $DONE, %rdi
    je      .uq_next                # if that particular order is finished, check the next order
    mov     etick(,%rbx,8), %rax    # entry tick of the order
    cmp     tick, %rax              
    jne     .uq_next                # not entering at this tick (if not equal)
    cmpq    $R_TURNS, rule(,%rdi,8) # (rdi stores the station order) so we check the rule of that station
    jne     .uq_next                # only turns uses a line
    mov     %rbx, %rsi              # enqueue gets order as argument
    call    enqueue
.uq_next:
    inc     %rbx
    jmp     .uq_new

.uq_pend:
    xor     %rdi, %rdi              # clears rdi
.uq_ploop:
    mov     pend(,%rdi,8), %rsi     # save that particular order's id to rsi
    cmp     $-1, %rsi               
    je      .uq_pnext
    call    enqueue
    movq    $-1, pend(,%rdi,8)      # we put that order to queue, clear the pend index
.uq_pnext:
    inc     %rdi
    cmp     $3, %rdi
    jl      .uq_ploop
    pop     %rbx                    # restore the rbx and return when we finished checking for all stations
    ret                             


##################################
# chef rules
# each takes station in rdi and returns order in rax or -1
##################################

select_order:
    mov     rule(,%rdi,8), %rax
    cmp     $R_FIRST, %rax
    je      select_first            # if rule is first
    cmp     $R_QUICK, %rax
    je      select_quick            # if rule is quick
    jmp     select_turns            # rule is turns

# keep current dish or take earliest entry then lowest input index
select_first:
    # does chef committed to an order?
    mov     cur(,%rdi,8), %rax
    cmp     $-1, %rax               
    jne     .sf_ret                 # still working on a dish, so return (jump if not equal)

    # chef is free so scan all orders to find the best one
    mov     $0x7fffffff, %r8        # r8 will hold smallest entry tick found so we initially start with the largest signed 32bit integer
    xor     %rcx, %rcx              # clears rcx
.sf_loop:
    cmp     n_ord, %rcx             
    jge     .sf_set                 # when orders are finished
    cmp     stn(,%rcx,8), %rdi      # check whether the order is in the same station that we are checking
    jne     .sf_next                # if not at this station
    mov     etick(,%rcx,8), %rdx    
    cmp     tick, %rdx
    jg      .sf_next                # that order has not entered the station yet
    cmp     %r8, %rdx               
    jge     .sf_next                # strict check keeps lower index on ties
    mov     %rdx, %r8               # r8=rdx if rdx<r8
    mov     %rcx, %rax              # make the order which will be processed this (for now)
.sf_next:                           # go to the next order
    inc     %rcx
    jmp     .sf_loop
.sf_set:
    mov     %rax, cur(,%rdi,8)      # put the evaluated value to corresponding station's cur order
.sf_ret:
    ret

# least remaining work then earliest entry then lowest input index
select_quick:
    mov     $-1, %rax
    mov     $0x7fffffff, %r8        # least remaining work so we initially start with the largest signed 32bit integer
    mov     $0x7fffffff, %r9        # least entry tick so we initially start with the largest signed 32bit integer
    xor     %rcx, %rcx              # clears rcx
.sq_loop:
    cmp     n_ord, %rcx
    jge     .sq_ret                 # if we finished checking all orders return
    cmp     stn(,%rcx,8), %rdi      
    jne     .sq_next                # if that order's station is not same as our station, go checking next order
    mov     etick(,%rcx,8), %rdx
    cmp     tick, %rdx
    jg      .sq_next                # that order has not entered the station yet
    mov     rem(,%rcx,8), %r10      # remaining ticks of the order
    cmp     %r8, %r10
    jl      .sq_take                # if the order we are checking has less work, put its index to rax (for now) (less work wins)
    jg      .sq_next                # if the order has more work, check next order
    cmp     %r9, %rdx               # if they have same work check for the least entry tick
    jge     .sq_next                # if the order has entered later, check for next order
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
    # is a turn already in progress?
    mov     cur(,%rdi,8), %rax
    cmp     $-1, %rax
    jne     .st_ret                 # not -1, turn continues return that dish.
    # is any dish waiting?
    mov     qlen(,%rdi,8), %rcx
    test    %rcx, %rcx              # bitwise and but it sets only the zf(zero flag) depending on the result (zf=1 when result=0)
    jz      .st_ret                 # (jump if zero) empty line so chef is idle
    imul    $MAX_ORD, %rdi, %rdx    # start of this station's line
    mov     queue(,%rdx,8), %rax    # store the order in front
    dec     %rcx                    # decrement the length by 1
    mov     %rcx, qlen(,%rdi,8)     # and save the new length to the station's index
.st_shift:                          # move the rest one slot forward
    test    %rcx, %rcx
    jz      .st_take                # when we finished moving all orders
    mov     queue+8(,%rdx,8), %r8   # save the next order in queue
    mov     %r8, queue(,%rdx,8)     # and put it to the previous index in queue
    inc     %rdx                    # we increment rdx by 1 to move in the station's line
    dec     %rcx                    # we decrement rcx to count the orders that we moved
    jmp     .st_shift               
.st_take:
    mov     %rax, cur(,%rdi,8)      # put the selected order to the station's cur index
    movq    $0, used(,%rdi,8)       # new turn starts
.st_ret:
    ret


##################################
# simulation
##################################

simulate:
    push    %rbx
    push    %r12
    movq    $0, tick                # simulation starts at tick=0
.sim_tick:
    mov     served, %rax            
    cmp     n_ord, %rax
    jge     .sim_end                # if every order is served end the simulation

    # step 1 update queues
    call    update_queues

    # step 2 select orders at all stations
    xor     %r12, %r12
.sim_sel:
    mov     %r12, %rdi              # argument: station
    call    select_order            # rax = chosen order or -1
    mov     %rax, sel(,%r12,8)      # writes the selected order index to corresponding station's index in sel
    inc     %r12                    # go to next station index
    cmp     $3, %r12    
    jl      .sim_sel                # if we do not finish all stations, repeat

    # step 3 record and process
    xor     %r12, %r12              # clears station id
.sim_proc:
    imul    $TL_SIZE, %r12, %rax    # save the starting index of row of the station to rax
    add     tick, %rax              # timeline slot of this tick
    mov     sel(,%r12,8), %rbx      
    cmp     $-1, %rbx               
    je      .sim_idle               # if no order is selected for that station we write '.' for that tick
    movb    ids(%rbx), %dl          # write the id of the order to dl
    movb    %dl, timeline(%rax)     # and put the id of the order into the calculated timeline address
    decq    rem(,%rbx,8)            # we decrease the remaining time for that order
    incq    used(,%r12,8)           # increases station's used value (only read by turns to check whether we reached turns-q)
    jmp     .sim_pnext              # increment station id and loop until we evaluated all stations
.sim_idle:
    movb    $'.', timeline(%rax)
.sim_pnext:
    inc     %r12
    cmp     $3, %r12
    jl      .sim_proc

    # step 4 handle completions and ended turns
    xor     %r12, %r12              # clears station id
.sim_comp:
    mov     sel(,%r12,8), %rbx
    cmp     $-1, %rbx
    je      .sim_cnext              # if there is no order in that station check next station
    cmpq    $0, rem(,%rbx,8)        
    jne     .sim_unfinished         # if order is not finished
    movq    $-1, cur(,%r12,8)       # station is free
    mov     %rbx, %rdi
    lea     1(%r12), %rsi           # search from next station
    mov     tick, %rdx
    inc     %rdx                    # enters or is served at t+1
    call    place_order
    jmp     .sim_cnext
.sim_unfinished:
    cmpq    $R_TURNS, rule(,%r12,8)
    jne     .sim_cnext              # for first and quick, an unfinished order needs no action (first: cur still holds the order, quick: will choose every time at next tick)
    mov     used(,%r12,8), %rax
    cmp     quant(,%r12,8), %rax    
    jl      .sim_cnext              # if used value is not exceed q value we will evaluate same item in next tick too(quant holds turns-q values of stations)
    mov     %rbx, pend(,%r12,8)     # put the current order to pending (will enter the queue after the coming orders in next tick)
    movq    $-1, cur(,%r12,8)       # there is no currently evaluated order in that station
.sim_cnext:         
    inc     %r12
    cmp     $3, %r12
    jl      .sim_comp

    # step 5 advance time
    incq    tick
    jmp     .sim_tick               # loops in the simulate part
.sim_end:                           # ends the simulation by recovering the callee saved registers
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
    xor     %rdx, %rdx              # clears rdx
    div     %r8                     # rax = rax / 10 and rdx = digit (as rdx stores remainder)
    add     $'0', %rdx              # convert digit to char
    push    %rdx                    # digits come out in reverse so we push them onto stack
    inc     %rcx
    test    %rax, %rax              
    jnz     .it_div                 # loop until decimal part is 0
.it_pop:
    pop     %rax
    movb    %al, (%rdi)             
    inc     %rdi
    dec     %rcx
    jnz     .it_pop                 # pop all digits and save until digit count is 0
    ret

# write rdx bytes from rsi to stdout handling partial writes
write_all:
.wa_loop:
    test    %rdx, %rdx
    jle     .wa_done                # if there are no bytes left to write
    mov     $1, %rax                # sys_write
    mov     $1, %rdi                # stdout
    syscall
    cmp     $0, %rax                
    jle     .wa_done                # write error if rax<=0
    add     %rax, %rsi              # move data pointer past the bytes written  
    sub     %rax, %rdx              # calculate the current count of bytes left and write it to rdx as argument
    jmp     .wa_loop                # try to write the remaining part again
.wa_done:
    ret

# build the four output lines in outbuf and print them
print_output:
    push    %rbx
    push    %r12
    mov     $outbuf, %rdi           # put starting address of outbuf to rdi
    xor     %r12, %r12              # clears r12 (station index)
.out_line:                          # copy T chars of each timeline
    imul    $TL_SIZE, %r12, %rsi
    add     $timeline, %rsi         # now rsi points to the current tick index in current station's row
    mov     tick, %rcx              # tick equals T at the end
    rep movsb                       # !!!!! movsb copies 1 byte from rsi to rdi and add 1 to both rsi and rdi and does this operation rcx times
    movb    $'\n', (%rdi)           # we add newline at the end of each row
    inc     %rdi                    # go to the next line (we already went to the end of the line by movsb)
    inc     %r12                    # go to the next station index
    cmp     $3, %r12
    jl      .out_line               # loop until we evaluate all 3 stations

    xor     %rbx, %rbx              # reset rbx to store order index
.out_serve:                         # ID=time entries in input order
    cmp     n_ord, %rbx             
    jge     .out_end                # when all orders finished go to printing part
    test    %rbx, %rbx
    jz      .out_entry              # no space before the first entry
    movb    $' ', (%rdi)            # put space between entries
    inc     %rdi
.out_entry:
    movb    ids(%rbx), %al
    movb    %al, (%rdi)             # write the id of order to 4th output line
    movb    $'=', 1(%rdi)           # add '=' next to the id
    add     $2, %rdi                # rdi+=2 (now points to the place of the serve time of the order)
    mov     serve(,%rbx,8), %rax    # get the serve time of the order and convert it to char
    call    itoa
    inc     %rbx                    # go to the next order index
    jmp     .out_serve              # loop until all orders processed
.out_end:
    movb    $'\n', (%rdi)           # put newline to end
    inc     %rdi
    mov     %rdi, %rdx              # increment rdi and put it into rdx to calculate total length and give it as argument
    sub     $outbuf, %rdx           # total length
    mov     $outbuf, %rsi
    call    write_all               
    pop     %r12                    # recover the callee saved values and return
    pop     %rbx
    ret
