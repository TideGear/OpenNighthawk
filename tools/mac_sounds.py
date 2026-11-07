#!/usr/bin/env python3
"""mac_sounds.py - the Macintosh release's recorded sounds as WAV files for --mac-sounds.

    py tools/mac_sounds.py SOURCE --out DIR

SOURCE is the 'F117a Sound' file's resource fork in any of the forms an unpacked Mac
archive leaves on Windows: an AppleDouble file ('F117a Sound.rsrc' from unar, or
'._F117a Sound'), an AppleSingle file, a raw resource fork, or a folder holding one of
those anywhere inside it. Every 'snd ' resource becomes DIR/<resource id>.wav (8-bit
mono PCM at its own rate), which is what src/host/macsound.c loads. Only the user's
own copy is read; nothing is written outside DIR.

Formats: the resource fork as Inside Macintosh: More Macintosh Toolbox describes it;
'snd ' format 1 and 2 with a standard (encode 0) or extended (encode 0xFF) sound header.
Compressed headers (encode 0xFE, MACE) are reported, not decoded.
"""
from __future__ import annotations

import argparse
import os
import struct
import sys
import wave

SOUND_FILE = 'F117a Sound'

# The 'snd ' resources as a set (id, length and bytes, in id order), SHA-256. The whole fork differs between releases (its map and
# attributes changed) while the sounds did not, so the sounds themselves are what is recognised. Seen in: the 2.1 archive
# (F-117a_Stealth_Fighter_2.1.sit) and the archive that also carries the 2.3.2 update (f117a_stealth_fighter.sit); the 2.3.1 and
# 2.3.2 updates do not contain the sound file.
KNOWN_SOUND_SETS = {
    'ebb855b3a91db4f3c066db6fc006b240cefdda17c36f7135270345fba974d8f8': 'the Mac release 2.1 to 2.3.2 sound file (33 sounds)',
}


def sound_set_hash(fork: bytes) -> str:
    import hashlib
    h = hashlib.sha256()
    for rid, data in sorted(resources(fork, b'snd ')):
        h.update(rid.to_bytes(2, 'big', signed=True) + len(data).to_bytes(4, 'big') + data)
    return h.hexdigest()


def fork_from_container(b: bytes) -> bytes | None:
    """The resource fork from AppleDouble/AppleSingle, or b itself when it already is one."""
    if len(b) >= 26 and b[:4] in (b'\x00\x05\x16\x07', b'\x00\x05\x16\x00'):
        n = struct.unpack('>H', b[24:26])[0]
        for k in range(n):
            eid, off, ln = struct.unpack('>III', b[26 + 12 * k:38 + 12 * k])
            if eid == 2:                                    # entry 2: the resource fork
                return b[off:off + ln]
        return None
    return b if looks_like_fork(b) else None


def looks_like_fork(b: bytes) -> bool:
    if len(b) < 16:
        return False
    d_off, m_off, d_len, m_len = struct.unpack('>IIII', b[:16])
    return d_off >= 16 and d_off + d_len <= len(b) and m_off + m_len <= len(b) and m_len >= 30


def resources(fork: bytes, want: bytes):
    """(id, data) for every resource of type `want`."""
    d_off, m_off = struct.unpack('>II', fork[:8])
    tl_off = m_off + struct.unpack('>H', fork[m_off + 24:m_off + 26])[0]
    ntypes = struct.unpack('>H', fork[tl_off:tl_off + 2])[0] + 1
    for t in range(ntypes):
        e = tl_off + 2 + 8 * t
        rtype = fork[e:e + 4]
        count, ref_off = struct.unpack('>HH', fork[e + 4:e + 8])
        if rtype != want:
            continue
        for r in range(count + 1):
            ref = tl_off + ref_off + 12 * r
            rid = struct.unpack('>h', fork[ref:ref + 2])[0]
            data_rel = int.from_bytes(fork[ref + 5:ref + 8], 'big')
            at = d_off + data_rel
            ln = struct.unpack('>I', fork[at:at + 4])[0]
            yield rid, fork[at + 4:at + 4 + ln]


def decode_snd(s: bytes) -> tuple[int, bytes]:
    """(sample rate, 8-bit unsigned mono samples) of one 'snd ' resource."""
    fmt = struct.unpack('>H', s[:2])[0]
    if fmt == 1:
        nmods = struct.unpack('>H', s[2:4])[0]
        p = 4 + 6 * nmods
    elif fmt == 2:
        p = 4
    else:
        raise ValueError(f'snd format {fmt}')
    ncmds = struct.unpack('>H', s[p:p + 2])[0]
    p += 2
    hdr = None
    for k in range(ncmds):
        cmd, _p1, p2 = struct.unpack('>HHI', s[p + 8 * k:p + 8 * k + 8])
        if cmd & 0x7FFF in (0x50, 0x51) and cmd & 0x8000:    # soundCmd / bufferCmd with an offset to the header
            hdr = p2
            break
    if hdr is None:
        raise ValueError('no sound header command')
    num, rate_fixed = struct.unpack('>II', s[hdr + 4:hdr + 12])
    encode = s[hdr + 20]
    rate = round(rate_fixed / 65536)
    if encode == 0x00:                                     # standard header: num is the byte count
        return rate, s[hdr + 22:hdr + 22 + num]
    if encode == 0xFF:                                     # extended header: num is the channel count
        frames = struct.unpack('>I', s[hdr + 22:hdr + 26])[0]
        bits = struct.unpack('>H', s[hdr + 48:hdr + 50])[0]
        if num != 1 or bits != 8:
            raise ValueError(f'extended header with {num} channels, {bits} bits')
        return rate, s[hdr + 64:hdr + 64 + frames]
    raise ValueError(f'compressed sound (encode {encode:#x})')


def find_sound_fork(source: str) -> bytes:
    if os.path.isfile(source):
        fork = fork_from_container(open(source, 'rb').read())
        if fork is None:
            raise SystemExit(f'{source}: not a Mac resource fork (expected the {SOUND_FILE} file as unpacked from the archive)')
        return fork
    names = {f'{SOUND_FILE}.rsrc', f'._{SOUND_FILE}', SOUND_FILE, f'{SOUND_FILE}.as'}
    for root, _dirs, files in os.walk(source):
        for name in sorted(files):
            if name in names:
                fork = fork_from_container(open(os.path.join(root, name), 'rb').read())
                if fork is not None and any(True for _ in resources(fork, b'snd ')):
                    return fork
    raise SystemExit(f'{source}: no "{SOUND_FILE}" file with its sounds was found (unpack the Mac archive keeping resource forks)')


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('source')
    ap.add_argument('--out', required=True)
    args = ap.parse_args()
    fork = find_sound_fork(args.source)
    known = KNOWN_SOUND_SETS.get(sound_set_hash(fork))
    print(f'recognised: {known}' if known else 'not a known Mac sound file: imported anyway; please report its source')
    os.makedirs(args.out, exist_ok=True)
    done, skipped = 0, []
    for rid, data in resources(fork, b'snd '):
        try:
            rate, pcm = decode_snd(data)
        except (ValueError, struct.error) as e:
            skipped.append(f'{rid}: {e}')
            continue
        with wave.open(os.path.join(args.out, f'{rid}.wav'), 'wb') as w:
            w.setnchannels(1)
            w.setsampwidth(1)                                  # WAV 8-bit is unsigned, as the Mac's samples are
            w.setframerate(rate)
            w.writeframes(pcm)
        done += 1
    print(f'{done} Mac recordings written to {args.out}')
    for s in skipped:
        print(f'skipped {s}')
    return 0 if done else 1


if __name__ == '__main__':
    sys.exit(main())
