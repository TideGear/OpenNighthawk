/* Opt-in research capture before model visibility filtering. This is the
 * original model request and its complete input memory, not a scene renderer
 * or a claim of persistent object identity. Never ship original bytes. */
#ifndef F117R_SCENE_CAPTURE_H
#define F117R_SCENE_CAPTURE_H

#define SCENE_CAPTURE_VERSION 1
#define SCENE_REQUEST_KIND 'i'
#define SCENE_MEMORY_KIND 'j'
/* version, DS/SS/SP/CS/IP, far return IP/CS, model bank/count/index,
 * position xyz, orientation word/table pointer, orthographic shift,
 * view origin xy, viewport xmin/ymin/xmax/ymax, ES, eight registers, flags.
 * The record clock identifies its memory chunks. */
#define SCENE_REQUEST_N 33
/* version, role (0 DS, 1 model bank, 2 stack), segment, offset, byte count,
 * then little-endian words. A stack chunk can wrap at FFFF. */
#define SCENE_MEMORY_HEAD 5
#define SCENE_CHUNK_BYTES 512
#define SCENE_STACK_BYTES 256
#define SCENE_MEMORY_MAX (SCENE_MEMORY_HEAD + SCENE_CHUNK_BYTES / 2)

/* Record 'I' (f117_observer.ident): a compact model request, one per 00CF, logged just before the vertices it
 * projects. Fields: version, model index, caller return IP, upstream return IP (caller's frame), caller BP,
 * parent BP, ring (DS:E328), entry pointer (DS:E476), DS:4994/4996/4998, the parent frame's words at -16h, -2,
 * -18h, -4, -0Ch, -1Eh, and the five bytes at the entry pointer. */
#define SCENE_IDENT_KIND 'I'
#define SCENE_IDENT_N 22

#endif
