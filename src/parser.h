#ifndef FAB_PARSER_H
#define FAB_PARSER_H

#include "stream.h"
#include "vm.h"

#include <stddef.h>
#include <stdint.h>

#if 0
typedef enum {
	TOK_END,
	TOK_SPACE,   // ' ' '\t'
	TOK_AND,     // &&
	TOK_OR,      // ||
	TOK_STR,     // str 'str' "str"
	TOK_COMMA,   // ,
	TOK_SET,     // =
	TOK_SETNULL, // ?=
	TOK_CAT,     // +=
	TOK_NULL,    // $
	TOK_EXP,     // @
	// the following require an END token
	TOK_ID,      // cmd var
	TOK_FUNC,    // $(...)
	TOK_VAR,     // $ ${...}
	TOK_ARG,     // $n
	TOK_ARGC,    // $?
	TOK_GROUP,   // ()
	TOK_ARR,     // {}
	TOK_SEL      // []
} TokenType;
typedef struct {
	TokenType type;
	union {
		struct {
			char* text;
			size_t len;
			char quotes; // '\0', '\'', or '"'
		} str;
		struct {
			char* name;
			size_t len;
			uint32_t crc;
		} id;
		struct {
			size_t index;
		} arg;
	} data;
} Token;
#endif

size_t ParseNext(StreamStack*, VMOpBuf* addTo);

#endif
