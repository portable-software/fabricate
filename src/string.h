#ifndef FAB_STRING_H
#define FAB_STRING_H

#include "vlb.h"

#include <stdbool.h>

typedef struct VLB(char) CharBuf;

char* DupStr(const char* str);

bool MatchWildcard(const char* pat, const char* text);

#endif
