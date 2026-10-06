import sys
name = sys.argv[1]
eq = int(sys.argv[2], 16); fd = int(sys.argv[3], 16); hd = int(sys.argv[4], 16)
base_kb = int(sys.argv[5]); ext_kb = int(sys.argv[6])
cm = bytearray(128)
cm[0x0A] = 0x26; cm[0x0B] = 0x02; cm[0x0D] = 0x80
cm[0x10] = fd; cm[0x12] = hd; cm[0x14] = eq
cm[0x15] = base_kb & 0xFF; cm[0x16] = base_kb >> 8
cm[0x17] = ext_kb & 0xFF; cm[0x18] = ext_kb >> 8
cm[0x30] = ext_kb & 0xFF; cm[0x31] = ext_kb >> 8
s = sum(cm[0x10:0x2E]); cm[0x2E] = (s >> 8) & 0xFF; cm[0x2F] = s & 0xFF
open('D:/86box/vm/nvr/' + name, 'wb').write(cm)
print(cm[:0x34].hex(' '))
