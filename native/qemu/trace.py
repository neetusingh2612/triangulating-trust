# GDB Python: single-step from MARK=1..2 (HMAC) and MARK=3..4 (TT), counting
# executed instructions and assigning ARMv7-M (Cortex-M3) cycle costs.
import gdb, re

# ARMv7-M cycle costs (Cortex-M3 TRM DDI0337, integer core, zero wait states)
def cyc(mn):
    b=mn.split('.')[0]
    b=b[:-1] if b.endswith('s') and b not in ('bics','muls') else b
    if b in ('ldr','ldrb','ldrh','ldrsb','ldrsh'): return 2
    if b in ('str','strb','strh'): return 2
    if b in ('ldm','ldmia','pop'): return 2   # 1+N approx; refined below
    if b in ('stm','stmia','stmdb','push'): return 2
    if b in ('b','bx','bl','blx','beq','bne','blt','bgt','bge','ble','bhi','bls','bcc','bcs','cbz','cbnz'): return 3
    if b in ('mul','muls'): return 1
    if b in ('mla','mls'): return 2
    if b in ('umull','smull','umlal','smlal'): return 5
    return 1

def run_region(tag):
    n=0; cycles=0; hist={}
    while True:
        frame=gdb.selected_frame()
        pc=frame.pc()
        # disassemble current instruction
        d=gdb.execute(f"x/i {pc}",to_string=True)
        m=re.search(r':\t(\S+)',d)
        mn=m.group(1) if m else '?'
        # multi-reg cost refinement for push/pop/ldm/stm
        c=cyc(mn)
        if mn.split('.')[0] in ('push','pop','ldm','ldmia','stm','stmia','stmdb'):
            regs=len(re.findall(r'r\d+|lr|pc|sp',d))
            c=1+max(regs,1)
        cycles+=c; n+=1; hist[mn.split('.')[0]]=hist.get(mn.split('.')[0],0)+1
        # check MARK
        mark=int(gdb.parse_and_eval("(unsigned)MARK"))
        gdb.execute("stepi",to_string=True)
        newmark=int(gdb.parse_and_eval("(unsigned)MARK"))
        if newmark!=mark and newmark in (tag+1,):
            break
        if n>200000: break
    return n,cycles,hist

gdb.execute("target remote :1234")
gdb.execute("set pagination off")
# run to MARK==1 (start HMAC)
gdb.execute("break bench.c:16")  # MARK=1 line-ish; use watchpoint instead
gdb.execute("delete")
# simpler: break at main, then watch MARK transitions
gdb.execute("tbreak main"); gdb.execute("continue")
# step until MARK becomes 1
def step_to_mark(val):
    while int(gdb.parse_and_eval("(unsigned)MARK"))!=val:
        gdb.execute("stepi",to_string=True)

step_to_mark(1)
nH,cH,hH=run_region(1)   # runs until MARK becomes 2
step_to_mark(3)
nT,cT,hT=run_region(3)   # runs until MARK becomes 4

print(f"RESULT HMAC  instructions={nH}  cycles={cH}")
print(f"RESULT TT    instructions={nT}  cycles={cT}")
print("HMAC mix:",dict(sorted(hH.items(),key=lambda x:-x[1])[:8]))
print("TT   mix:",dict(sorted(hT.items(),key=lambda x:-x[1])[:8]))
gdb.execute("kill")
gdb.execute("quit")
