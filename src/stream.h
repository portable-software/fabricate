#ifndef FAB_STREAM_H
#define FAB_STREAM_H

#include <stdio.h>
#include <stddef.h>
#include <stdbool.h>

typedef struct Stream Stream;
typedef struct {
	Stream* head;
	Stream* data;
	unsigned len;
	size_t size;
} StreamStack;
typedef struct {
	unsigned index;
	unsigned line;
	unsigned col;
} StreamStackLoc;

typedef struct {
	char TODO;
} IOStream;

#define SEOF (-1)

bool SPushFile(StreamStack*, const char* name, FILE*);
bool SPushText(StreamStack*, const char* name, const char* text, size_t len);
int SGetC(StreamStack*);
int SGetCI(StreamStack*); // Interpret comments and escaped newlines
void SUnGetC(StreamStack*);
void SGetLoc(StreamStack*, StreamStackLoc*);
char* SGetName(StreamStack*, unsigned index);

#endif
