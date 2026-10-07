"""hello_floppy.py IMG: a 1.2 MB floppy image whose boot sector sets text
mode 3 (clearing the screen, cursor on row 0, inside what a window capture
shows) and prints one line - proof that POST passed and the BIOS booted."""
import sys

img = bytearray(1228800)
# mov ax,3 / int 10h / xor ax,ax / mov ds,ax / mov si,7C30h /
# l: lodsb / or al,al / jz d / mov ah,0Eh / int 10h / jmp l / d: jmp $
code = bytes.fromhex("B80300CD1031C08ED8BE307CAC08C07406B40ECD10EBF5EBFE")
img[:len(code)] = code
msg = b"HELLO FROM THE TEST FLOPPY\r\n\0"
img[0x30:0x30 + len(msg)] = msg
img[510:512] = b"\x55\xAA"
open(sys.argv[1], "wb").write(img)
