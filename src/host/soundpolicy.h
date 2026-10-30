/* soundpolicy.h - which source plays a sound (docs/roadmap.md, Phase 3, hybrid sound).
 *
 * Sources: the Macintosh recordings, the three DOS drivers' own output, and nothing. The default order by kind:
 *   speech: Mac, then PC speaker
 *   music:  Mac, then Roland, then AdLib
 *   sound:  Mac, then AdLib
 * "Available" is the caller's to say (a Mac recording exists for the id; the driver can be run). The player can
 * override the default for a whole kind. */
#ifndef F117R_SOUNDPOLICY_H
#define F117R_SOUNDPOLICY_H

typedef enum { SP_NONE = 0, SP_MAC, SP_ROLAND, SP_ADLIB, SP_SPEAKER } sp_source;
typedef enum { SP_SPEECH = 0, SP_MUSIC, SP_SOUND, SP_KINDS } sp_kind;

/* source availability as a bit mask: 1 << source */
#define SP_BIT(s) (1u << (s))

/* The source to use for a kind: the override if it is set and available, else the first available in the
 * default order, else SP_NONE. override is SP_NONE for "use the default". */
sp_source sp_choose(sp_kind kind, unsigned available, sp_source override);

/* "mac", "roland", "adlib", "speaker", "default" (SP_NONE); returns -1 for an unknown word. */
int sp_parse_source(const char *word);
const char *sp_source_name(sp_source s);

#endif
