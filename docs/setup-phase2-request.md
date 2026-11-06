# Phase 2 request: hex-literal spacing for Zig

Please make whitespace-only spaces around + and - immediately after hex literals
ending in E/e in the lines below. For example `0x061E+2` becomes `0x061E + 2`.
Zig's C compiler tokenizes the former as one invalid preprocessing number.
Keep the expressions and test behavior identical, and verify equivalence after
stripping whitespace. This setup branch does not edit Phase 2 test files.

`tests/test_rerun.c` lines 132–133 (current master):

```c
for(int ring=1;ring<=4;ring++)memory[0x20000+0x061E+2*(ring*32+1)]=1;
if(populated_off)for(int ring=1;ring<=4;ring++)memory[0x20000+0x061E+2*(ring*32+1)]=0;
```

Recorded full-unit-build log excerpt:

```text
D:/f117-wt/setup-wizard/tests/test_rerun.c:132:64: error: invalid suffix '+2' on integer constant
D:/f117-wt/setup-wizard/tests/test_rerun.c:133:81: error: invalid suffix '+2' on integer constant
```

Log: `D:/f117-dl/wizard/release-review/unit-build.log`.
The corresponding 19 `tests/func_lockstep.c` sites follow with exact line numbers.
The earlier Zig gate used only a whitespace-verified scratch copy of that file.

```c
8459: const uint16_t at=(uint16_t)(0xCBDE+16*k);
13357: else if(q_<72)n_=(uint16_t)(0x7E+RND(0x84));
14687: const uint16_t record=XPICK(4)?(uint16_t)(0xCBDE+16*XPICK(64)):(uint16_t)(0xA000+XPICK(0x800));
18086: case 10: y0_=(uint16_t)(0xDE-XRAND()%4);break;
19398: const uint16_t list_=XPICK(6)==0?(uint16_t)(0xFFFE+XPICK(2)):(uint16_t)(0x2000+2*XPICK(0x40));
20600: const uint16_t rb=(uint16_t)(0xCBDE+16*(int)rec[k]);
20725: const uint16_t rb=(uint16_t)(0xCBDE+16*(int)rec[k]);
31386: const uint16_t rec_=(uint16_t)(0xCBDE+(uint16_t)(idx_<<4));
34118: else if(q_<72)n_=(uint16_t)(0x7E+RND(0x84));
34786: const uint16_t list_=XPICK(6)==0?(uint16_t)(0xFFFE+XPICK(2)):(uint16_t)(0x2000+2*XPICK(0x40));
42245: if(psel_==2)rec=(uint16_t)(0x066E-0x0A+(XRAND()%6));           /* ... or the base word */
42306: if(psel_==0)rec=(uint16_t)(0xFFFE+XRAND()%2);                  /* byte 1 wraps past the segment end */
42894: if(ovl_&&XRAND()%3==0)r[R_SP]=(uint16_t)(0x03CE+2*(XRAND()%3));   /* the clear of [03C8] lands on a saved DI, BP or DS */
47123: const uint16_t ra_=(uint16_t)(0x2E+44*num_+j);   /* not a_: XW8's own local is named a_ */
49764: const uint16_t list_=XPICK(6)==0?(uint16_t)(0xFFFE+XPICK(2)):(uint16_t)(0x2000+2*XPICK(0x40));
50578: const unsigned lim_=(0x08BE-0x0494-dsoff_-0x10)/4;
51263: const uint16_t record=XPICK(4)?(uint16_t)(0xCBDE+16*XPICK(64)):(uint16_t)(0xA000+XPICK(0x800));
51368: else if(k_==1) r[R_SP]=(uint16_t)(0xFFFE-2*(XRAND()%4));
51697: if(ssel_<2){seg[S_SS]=NS;{const unsigned k_=XPICK(16); r[R_SP]=(uint16_t)((k_<8)?(0x151E+2*k_):(0x153A+2*(k_-8)));}   /* the far frame of the random step must not meet the cells the stub rewrites */}
```
