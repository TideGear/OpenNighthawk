# MT-32 ROMs

You supply two ROM files from a Roland MT-32 (or CM-32L): the **control ROM**
(the unit's firmware) and the **PCM ROM** (its sampled sounds). Put them in
one folder and point `mt32-roms` at it. The files are recognised by their
SHA-1 (Munt's own table), so their names do not matter; with several in the
folder, the first match in this order is used. F-117A (1991) wants the
first-generation ("old") MT-32 with control ROM 1.0x. Rendering
this game's own MIDI (a run from boot into flight) agrees: control ROMs 1.04
and 1.07 give bit-identical output, while 2.04 plays the music much the same
but the in-flight effects about 5 dB quieter and 5-7 dB duller above 3 kHz.
Any 1.0x ROM is right; 2.0x and the CM-32L are accepted as fallbacks.

| Control ROM | Size | SHA-1 |
|---|---|---|
| MT-32 1.07 | 64 KB | `b083518fffb7f66b03c23b7eb4f868e62dc5a987` |
| MT-32 1.06 | 64 KB | `a553481f4e2794c10cfe597fef154eef0d8257de` |
| MT-32 1.05 | 64 KB | `e17a3a6d265bf1fa150312061134293d2b58288c` |
| MT-32 1.04 | 64 KB | `5a5cb5a77d7d55ee69657c2f870416daed52dea7` |
| MT-32 BlueRidge | 64 KB | `7b8c2a5ddb42fd0732e2f22b3340dcf5360edf92` |
| MT-32 2.07 | 128 KB | `47b52adefedaec475c925e54340e37673c11707c` |
| MT-32 2.06 | 128 KB | `2869cf4c235d671668cfcb62415e2ce8323ad4ed` |
| MT-32 2.04 | 128 KB | `2c16432b6c73dd2a3947cba950a0f4c19d6180eb` |
| MT-32 2.03 | 128 KB | `5837064c9df4741a55f7c4d8787ac158dff2d3ce` |
| CM-32L 1.02 | 64 KB | `a439fbb390da38cada95a7cbb1d6ca199cd66ef8` |
| CM-32L 1.00 | 64 KB | `73683d585cd6948cc19547942ca0e14a0319456d` |
| CM-32LN 1.00 | 64 KB | `dc1c5b1b90a4646d00f7daf3679733c7badc7077` |

| PCM ROM | Size | SHA-1 | Goes with |
|---|---|---|---|
| MT-32 | 512 KB | `f6b1eebc4b2d200ec6d3d21d51325d5b48c60252` | every MT-32 control ROM |
| CM-32L | 1 MB | `289cc298ad532b702461bfc738009d9ebe8025ea` | the CM-32L and CM-32LN control ROMs |

Each ROM must be a single whole file; the split halves some dumps come as
(`..._a` / `..._b`, `..._l` / `..._h`) are not combined. On Windows,
`certutil -hashfile FILE SHA1` shows a file's SHA-1.
