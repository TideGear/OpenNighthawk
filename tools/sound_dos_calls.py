#!/usr/bin/env python3
"""sound_dos_calls.py - every call to a DOS program's sound thunk (and to VGAME's priority gate), with the id pushed and the named routine it sits in.

    py tools/sound_dos_calls.py     (run from a scratch directory: writes doscalls.json and gatecalls.json there)

Reads the unpacked images under D:/GOG/F-117A (matched_draft) and the census D:/f117-gate/census.tsv for routine names; the thunk addresses are
START 0A95:6BB1, END 05FA:25F3, VGAME 1E42:0313, PLAYER 02A3:13B9 (docs/sound-board.md). Needs capstone. Scratch tool of 1 November 2026:
paths are this machine's. Output feeds docs/owner-issues.md S6 and tools/sound_call_returns.py."""
import sys, csv, json, re
sys.path.insert(0,'C:/Dev/F-117A Recomp/tools'); sys.argv=['x']
import matched_draft as md
from capstone import *
cs=Cs(CS_ARCH_X86,CS_MODE_16); cs.detail=False
mods={m.name:m for m in md.load_all('D:/GOG/F-117A')}
census={}
for r in csv.DictReader(open('D:/f117-gate/census.tsv'),delimiter='\t'):
    census.setdefault(r['binary'],[]).append((int(r['entry'],16),int(r['size']),r['implemented_by'] or (r['cited_by'].split(';')[0] if r['cited_by'] else '')))
for k in census: census[k].sort()
THUNK={'START.EXE':(0x0A95,0x6BB1),'END.EXE':(0x05FA,0x25F3),'VGAME.EXE':(0x1E42,0x0313),'PLAYER.EXE':(0x02A3,0x13B9)}
def enclosing(binary,p):
    rows=census.get(binary.split('.')[0],[])
    best=None
    for e,sz,n in rows:
        if e<=p: best=(e,sz,n)
        else: break
    if best and best[0]<=p<best[0]+max(best[1],1): return best
    return None if not best else ('after',)+best
def pushed(img,p,fn):
    """disassemble forward from the routine's entry to the call; read the last push and what defined it"""
    if not fn or fn[0]=='after': return ('?',None,'no routine')
    entry=fn[0]
    ins=[]
    pos=entry
    while pos<p:
        got=list(cs.disasm(img[pos:min(pos+16,p)],pos,1))
        if not got: pos+=1; ins=[]; continue
        i=got[0]; ins.append(i); pos=i.address+i.size
    if pos!=p or not ins: return ('?',None,'out of step')
    last=ins[-1]
    if last.mnemonic!='push': return ('other',None,last.mnemonic+' '+last.op_str)
    op=last.op_str
    if re.match(r'^(0x[0-9a-f]+|\d+)$',op): return ('imm',int(op,0),'push '+op)
    if op in ('ax','bx','cx','dx','si','di'):
        for i in reversed(ins[:-1]):
            if i.mnemonic in ('call','jmp') or i.mnemonic.startswith('j'): break
            if i.mnemonic=='mov' and i.op_str.startswith(op+', '):
                v=i.op_str.split(', ',1)[1]
                try: return ('imm',int(v,0),'mov %s; push %s'%(i.op_str,op))
                except: return ('computed',None,'mov %s; push %s'%(i.op_str,op))
            if i.mnemonic=='xor' and i.op_str==op+', '+op: return ('imm',0,'xor; push')
            if i.mnemonic in ('add','sub','shl','sar','shr','and','or','lea','cwd','cbw','imul','inc','dec','neg') and (op in i.op_str.split(',')[0]): 
                return ('computed',None,'%s %s ... push %s'%(i.mnemonic,i.op_str,op))
        return ('computed',None,'push '+op+' (not set in this routine)')
    return ('computed',None,'push '+op)
out={}
for name,(seg,ip) in THUNK.items():
    img=mods[name].image; lin=(seg<<4)+ip
    calls=[]
    for i in range(len(img)-5):
        if img[i]==0x9a:
            off=img[i+1]|img[i+2]<<8; sg=img[i+3]|img[i+4]<<8
            if (sg<<4)+off==lin:
                enc=enclosing(name,i)
                k,v,t=pushed(img,i,enc)
                calls.append(dict(at=i,kind=k,id=v,text=t,fn=(enc if enc else None)))
    out[name]=calls
json.dump(out,open('doscalls.json','w'),indent=1,default=str)
for name,calls in out.items():
    print('==',name,len(calls))
    for c in calls:
        fn=c['fn']; fnt='?' if not fn else (('after '+str(fn[1:])) if fn[0]=='after' else '%s@%X+%d(%d)'%(fn[2] or '-',fn[0],c['at']-fn[0],fn[1]))
        print('  %06X id=%-5s %-28s %s'%(c['at'],c['id'],c['text'],fnt))

# ---- calls to VGAME's priority gate (D3F9): callers push the priority, then the id
img=mods['VGAME.EXE'].image
gate=[]
for i in range(0,0x10000-3):
    if img[i]==0xE8:
        rel=img[i+1]|img[i+2]<<8
        if ((i+3+rel)&0xFFFF)==0xD3F9:
            enc=enclosing('VGAME.EXE',i)
            if not enc or enc[0]=='after': gate.append(dict(at=i,fn=None,pushes=[])); continue
            ins=[]; pos=enc[0]
            while pos<i:
                got=list(cs.disasm(img[pos:min(pos+16,i)],pos,1))
                if not got: pos+=1; ins=[]; continue
                x=got[0]; ins.append(x); pos=x.address+x.size
            ok=(pos==i)
            # the two pushes before the call and what set them
            pushes=[]
            for k in range(len(ins)-1,-1,-1):
                x=ins[k]
                if x.mnemonic=='push':
                    v=None; txt=x.op_str
                    if re.match(r'^(0x[0-9a-f]+|\d+)$',x.op_str): v=int(x.op_str,0)
                    elif x.op_str in ('ax','bx','cx','dx'):
                        for y in reversed(ins[:k]):
                            if y.mnemonic=='mov' and y.op_str.startswith(x.op_str+', '):
                                try: v=int(y.op_str.split(', ',1)[1],0); txt=y.op_str
                                except: txt=y.op_str
                                break
                            if y.mnemonic=='xor' and y.op_str==x.op_str+', '+x.op_str: v=0; txt='xor'; break
                    pushes.append((v,txt))
                    if len(pushes)==2: break
                elif x.mnemonic in ('call','ret','retf') : break
            gate.append(dict(at=i,fn=enc,pushes=pushes,ok=ok))
json.dump(gate,open('gatecalls.json','w'),indent=1,default=str)
print('== gate calls',len(gate))
for g in gate:
    fn=g['fn']; fnt='?' if not fn else '%s@%X+%d'%(fn[2] or '-',fn[0],g['at']-fn[0])
    ps=g['pushes']
    idv=ps[0][0] if ps else None; pr=ps[1][0] if len(ps)>1 else None
    print('  %06X id=%-5s prio=%-5s %-40s %s'%(g['at'],idv,pr,fnt[:60],('' if g.get('ok',True) else '(out of step)')))
