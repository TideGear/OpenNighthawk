/* tls.h - file-scope scratch that more than one thread may use: the replay on
 * the main thread and the in-between frame drawn on a worker each need their own. */
#ifndef F117R_TLS_H
#define F117R_TLS_H
#ifdef _MSC_VER
#define F117_TLS __declspec(thread)
#else
#define F117_TLS _Thread_local
#endif
#endif
