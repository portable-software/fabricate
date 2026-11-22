#ifndef FAB_ERRORS_H
#define FAB_ERRORS_H

#include <stdio.h>
#include <stddef.h>

extern const char* argv0;

typedef const char* FabErr;
#define ERR_NONE NULL
#define ERR_INT "Internal error"
#define ERR_MEM "Memory error"
#define ERR_NOFAB "No .fab file found"

#define PUTERR__EZ_LOCMSG(S, M) do { \
	StreamStackLoc LOC; \
	SGetLoc((S), &LOC); \
	fprintf(stderr, "%s: %s:%u:%u: %s\n", argv0, SGetName((S), LOC.index), LOC.line, LOC.col, (M)); \
} while (0)
#define PUTERR__EZ_LOCMSGF(S, F, ...) do { \
	StreamStackLoc LOC; \
	SGetLoc((S), &LOC); \
	fprintf(stderr, "%s: %s:%u:%u: " F "\n", argv0, SGetName((S), LOC.index), LOC.line, LOC.col, __VA_ARGS__); \
} while (0)

#define PUTERR(X) fprintf(stderr, "%s: %s\n", argv0, (X))
#define PUTERR_LIT(X) fprintf(stderr, "%s: " ERR_##X "\n", argv0)

#define PUTERR_EZ_EXPECTED(S, M) ERR__EZ_LOCMSGF((S), "Expected %s", M)
#define PUTERR_EZ_UNEXPCHAR(S) ERR__EZ_LOCMSG((S), "Unexpected character\n")

#endif
