#!/usr/bin/env python3
"""sound_call_returns.py - which DOS sound call sites read the driver's answer in AX (run tools/sound_dos_calls.py first, in the same directory).

Result of 1 November 2026: only the three id 2 calls (START 30BF, END 161A, PLAYER 1D4: `or ax, ax; jne` until the driver answers 0) read it;
every other call site ignores AX, so a request the host takes can answer 0. Needs capstone."""
import sys, json, re
sys.path.insert(0,'C:/Dev/F-117A Recomp/tools'); sys.argv=['x']
import matched_draft as md
from capstone import *
cs=Cs(CS_ARCH_X86,CS_MODE_16)
mods={m.name:m for m in md.load_all('D:/GOG/F-117A')}
calls=json.load(open('doscalls.json'))
rows=[]
for name,lst in calls.items():
    img=mods[name].image
    for c in lst:
        p=c['at']
        ins=list(cs.disasm(img[p+5:p+5+24],p+5))[:7]
        txt=' ; '.join('%s %s'%(i.mnemonic,i.op_str) for i in ins)
        # does AX get read before being written?
        used=None
        for i in ins:
            ops=i.op_str
            if i.mnemonic in ('ret','retf','call','lcall','jmp'): break
            if re.search(r'\bax\b|\bal\b|\bah\b',ops) :
                first=ops.split(',')[0]
                if i.mnemonic in ('or','test','cmp','and') : used=i.mnemonic+' '+ops; break
                if i.mnemonic in ('mov','xor','sub','lea','sbb') and re.match(r'^(ax|al|ah)$',first.strip()): used=None; break
                if i.mnemonic=='push' and 'ax' in ops: used='push ax'; break
        rows.append((name,c['id'],'%X'%p,used,txt[:90]))
for r in sorted(rows,key=lambda r:(r[1] is None,r[1] if r[1] is not None else -1,r[0])):
    print('%-10s id=%-5s at %-6s uses-AX=%-22s after: %s'%r)
