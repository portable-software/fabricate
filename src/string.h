#ifndef FAB_STRING_H
#define FAB_STRING_H

#include "vlb.h"

#include <stdbool.h>
#include <string.h>

typedef struct VLB(char) CharBuf;

static inline bool CBAddPartStr(CharBuf* b, const char* s, size_t l) {
    size_t ol = b->len;
	VLB_EXPANDBY(*b, l, 2, 1, return false;);
    memcpy(b->data + ol, s, l);
    return true;
}
static inline bool CBAddStr(CharBuf* b, const char* s) {
    return CBAddPartStr(b, s, strlen(s));
}
static inline bool CBNullTerm(CharBuf* b) {
	VLB__EXP(*b, 2, 1, b->data[b->len] = '\0', return false;);
	return true;
}

char* DupStr(const char* str);

bool MatchWildcard(const char* pat, const char* text);

#endif
