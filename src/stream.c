#include "stream.h"

#include <stdint.h>

typedef enum __attribute__((packed)) {
	STREAM_FILE,
	STREAM_TEXT
} StreamType;
struct Stream {
	char* buf;
	size_t pos;
	size_t datasz;
	size_t bufsz;
	unsigned col;
	unsigned line;
	StreamType type;
	uint8_t atEnd : 1;
	uint8_t hasUnget : 1;
	int unget;
	char* name;
	union {
		FILE* file;
	} internal;
};
