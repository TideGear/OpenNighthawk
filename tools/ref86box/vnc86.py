#!/usr/bin/env python3
"""vnc86.py - drive a source-built 86Box through its VNC renderer.

The release 86Box has no way to take scripted input or run without a window.
A build with -DVNC=ON (tools/ref86box/build_86box.md) serves the guest's
display over VNC and takes key and mouse events from it, so the machine runs
with no window at all and a script can type and capture frames.

    py vnc86.py [--host 127.0.0.1] [--port 5900] SCRIPT

SCRIPT is words separated by ';':
    wait SECONDS            real-time pause
    shot FILE.png           the current frame
    type TEXT               ASCII, '\\r' is Enter, '\\e' Escape
    key NAME [NAME ...]     X11 key names pressed together, then released
                            (e.g. 'key Return', 'key Control_L Alt_L Delete')
    click X Y [buttons]     move (and press, with buttons 1/2/4) at guest x, y
    waitfor TEXT-FILE SECS  not used; reserved

This is a minimal RFB 3.8 client (no password, raw encoding only), enough for
LibVNCServer.
"""
import argparse
import socket
import struct
import sys
import time
import zlib

KEYS = {"Return": 0xFF0D, "Escape": 0xFF1B, "BackSpace": 0xFF08, "Tab": 0xFF09, "space": 0x20,
        "Control_L": 0xFFE3, "Alt_L": 0xFFE9, "Shift_L": 0xFFE1, "Delete": 0xFFFF,
        "Up": 0xFF52, "Down": 0xFF54, "Left": 0xFF51, "Right": 0xFF53, "Home": 0xFF50, "End": 0xFF57,
        "Page_Up": 0xFF55, "Page_Down": 0xFF56, "Insert": 0xFF63,
        **{"F%d" % i: 0xFFBD + i for i in range(1, 13)}}


class Rfb:
    def __init__(self, host, port, timeout=30):
        self.s = socket.create_connection((host, port), timeout=timeout)
        self.s.settimeout(timeout)
        version = self.read(12)
        self.s.sendall(b"RFB 003.008\n")
        n = self.read(1)[0]
        if n == 0:
            raise RuntimeError("server refused: " + self.read(struct.unpack(">I", self.read(4))[0]).decode())
        types = self.read(n)
        if 1 not in types:
            raise RuntimeError("server needs authentication: %r" % list(types))
        self.s.sendall(b"\x01")
        if struct.unpack(">I", self.read(4))[0] != 0:
            raise RuntimeError("security handshake failed")
        self.s.sendall(b"\x01")  # shared
        self.w, self.h = struct.unpack(">HH", self.read(4))
        self.bpp, self.depth, self.big, self.true, rm, gm, bm, rs, gs, bs = struct.unpack(">BBBBHHHBBB", self.read(13))
        self.read(3)
        self.read(struct.unpack(">I", self.read(4))[0])
        # 32 bpp, little-endian, 0x00RRGGBB, raw encoding only.
        self.s.sendall(struct.pack(">BxxxBBBBHHHBBBxxx", 0, 32, 24, 0, 1, 255, 255, 255, 16, 8, 0))
        self.s.sendall(struct.pack(">BxH", 2, 1) + struct.pack(">i", 0))
        self.frame = bytearray(self.w * self.h * 4)

    def read(self, n):
        out = b""
        while len(out) < n:
            chunk = self.s.recv(n - len(out))
            if not chunk:
                raise EOFError("connection closed")
            out += chunk
        return out

    def key(self, keysym, down):
        self.s.sendall(struct.pack(">BBxxI", 4, 1 if down else 0, keysym))

    def pointer(self, x, y, buttons=0):
        self.s.sendall(struct.pack(">BBHH", 5, buttons, x, y))

    def update(self, wait=3.0):
        """Request a full frame and apply every rectangle that comes back."""
        self.s.sendall(struct.pack(">BBHHHH", 3, 0, 0, 0, self.w, self.h))
        end = time.time() + wait
        got = False
        while time.time() < end:
            self.s.settimeout(max(0.05, end - time.time()))
            try:
                t = self.read(1)[0]
            except socket.timeout:
                break
            if t == 0:
                (n,) = struct.unpack(">xH", self.read(3))
                for _ in range(n):
                    x, y, w, h, enc = struct.unpack(">HHHHi", self.read(12))
                    if enc == 0:
                        data = self.read(w * h * 4)
                        for row in range(h):
                            o = ((y + row) * self.w + x) * 4
                            self.frame[o:o + w * 4] = data[row * w * 4:(row + 1) * w * 4]
                    elif enc == -223:
                        self.w, self.h = w, h
                        self.frame = bytearray(w * h * 4)
                    else:
                        raise RuntimeError("unsupported encoding %d" % enc)
                got = True
                break
            elif t == 2:
                pass
            elif t == 3:
                self.read(3); self.read(struct.unpack(">I", self.read(4))[0])
        self.s.settimeout(30)
        return got

    def png(self, path):
        raw = bytearray()
        for row in range(self.h):
            raw.append(0)
            line = self.frame[row * self.w * 4:(row + 1) * self.w * 4]
            raw += b"".join(bytes((line[i + 2], line[i + 1], line[i])) for i in range(0, len(line), 4))

        def chunk(tag, data):
            body = tag + data
            return struct.pack(">I", len(data)) + body + struct.pack(">I", zlib.crc32(body) & 0xFFFFFFFF)
        with open(path, "wb") as f:
            f.write(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", self.w, self.h, 8, 2, 0, 0, 0))
                    + chunk(b"IDAT", zlib.compress(bytes(raw), 6)) + chunk(b"IEND", b""))


def keysym(name):
    if name in KEYS:
        return KEYS[name]
    if len(name) == 1:
        return ord(name)
    raise ValueError("unknown key " + name)


def press(rfb, names, hold=0.06):
    syms = [keysym(n) for n in names]
    for s in syms:
        rfb.key(s, True)
    time.sleep(hold)
    for s in reversed(syms):
        rfb.key(s, False)
    time.sleep(hold)


def typing(rfb, text, hold=0.06):
    i = 0
    while i < len(text):
        c = text[i]
        if c == "\\" and i + 1 < len(text) and text[i + 1] in "re":
            press(rfb, ["Return" if text[i + 1] == "r" else "Escape"], hold)
            i += 2
            continue
        if c.isupper() or c in '~!@#$%^&*()_+{}|:"<>?':
            rfb.key(KEYS["Shift_L"], True); press(rfb, [c], hold); rfb.key(KEYS["Shift_L"], False)
        else:
            press(rfb, ["space" if c == " " else c], hold)
        i += 1


def run(rfb, script):
    for step in script.split(";"):
        word, _, arg = step.strip().partition(" ")
        if word == "wait":
            time.sleep(float(arg))
        elif word == "shot":
            rfb.update(); rfb.png(arg); print("shot", arg, rfb.w, rfb.h)
        elif word == "type":
            typing(rfb, arg)
        elif word == "key":
            press(rfb, arg.split())
        elif word == "click":
            p = arg.split(); x, y = int(p[0]), int(p[1]); b = int(p[2]) if len(p) > 2 else 1
            rfb.pointer(x, y, 0); time.sleep(0.1); rfb.pointer(x, y, b); time.sleep(0.1); rfb.pointer(x, y, 0)
        elif word:
            raise ValueError("unknown word " + word)


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--host", default="127.0.0.1")
    ap.add_argument("--port", type=int, default=5900)
    ap.add_argument("script")
    a = ap.parse_args()
    run(Rfb(a.host, a.port), a.script)


if __name__ == "__main__":
    sys.exit(main())
