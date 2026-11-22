#include "parser.h"
#include "error.h"
#include "string.h"

#include <stdbool.h>

#if 0
	ROUGH SYNTAX OUTLINE
	root    = { ( cmt | stmts ) } EOF
	cmt     = {ws} '#' { '\x00'..'\x0C' | '\x0E'..'\xFF' } ( '\n' | &EOF )
	stmts   = [ {ws} stmt {ws} term [ { {ws} ( "&&" | "||" ) {ws} stmt {ws} term } ] ]
	stmt    = cmd | asgmt
	id      = idchr {idchr}
	cmd     = id ws [ [expr] { {ws} "," {ws} [expr] } ]
	asgmt   = id {ws} ( "=" | "+=" | "?=" ) {ws} [expr]
	expr    = ( null | grp | str | var | arr | sel | func ) {ws} [expr]
	null    = "$"
	grp     = "(" {ws} [ expr {ws} ] ")"
	str     = strchr {strchr} | '"' {dstrchr} '"' | "'" {sstrchr} "'"
	var     = "$" ( id | "{" {ws} id {ws} [sel] {ws} "}" | '#' )
	arr     = "{" {ws} [ [expr] { {ws} "," {ws} [expr] } ] {ws} "}"
	sel     = "[" {ws} [expr] {ws} "]"
	func    = "$(" [stmts] ")"
	idchr   = 'A'..'Z' | 'a'..'z' | '0'..'9' | '-' | '_' | '.'
	strchr  = '!' | '%' | '*'..'+' | '-'..':' | '<'..'Z' | '^'..'z' | '~'
	dstrchr = '\x00'..'!' | '#'..'[' | ']'..'\xFF'
	sstrchr = '\x00'..'&' | '('..'\xFF'
	ws      = ' ' | '\t'
	term    = '\n' | ';' | &'&' | &'|' | &'#' | &EOF
#endif

#if 0

static inline bool IsIdChr(char c) {
	return
		(c >= 'A' && c <= 'Z') ||
		(c >= 'a' && c <= 'z') ||
		c == '-' || c == '_' || c == '.'
	;
}

static bool ReadId(StreamStack* s, CharBuf* str, int* cExt) {
	int c = *cExt;
	while (IsIdChr(c)) {
		VLB_ADD(*str, c, 2, 1, return false;);
		c = SGetCI(s);
	}
	*cExt = c;
	return true;
}
static bool AddIdTok(TokBuf* tb, CharBuf* str) {
	Token* t;
	VLB_SHRINK(*str, return false;);
	VLB_NEXTPTR(*tb, t, 2, 1, return false;);
	t->type = TOK_ID;
	t->data.id.name = str->data;
	t->data.id.len = str->len;
	VLB_INIT(*str, 16, return false;);
	return true;
}
#define EAT_WS() do {c = SGetCI(s);} while (c == ' ' || c == '\t')
size_t LexNext(StreamStack* s, TokBuf* tb) {
	// TODO: HANDLE BACKSLASH

	int c;
	size_t initTokCt = tb->len;
	Token* t;
	EAT_WS();
	// Check for && and ||
	if (c == '&' || c == '|') {
		// Ensure another & or |
		if (SGetCI(s) != c) {
			char str[2] = {c, 0};
			ERR_EXPECTED_EZ(s, str);
			return -1;
		}

		// Add AND or OR token
		VLB_NEXTPTR(*tb, t, 2, 1, ERR_INT(); return -1;);
		t->type = (c == '&') ? TOK_AND : TOK_OR;

		EAT_WS();
	} else if (c == '\n' || c == SEOF) {
		return 0;
	}

	CharBuf str;
	VLB_INIT(str, 16, goto intErr;);

	// Command or variable name
	SUnGetC(s);
	if (!ReadId(s, &str)) goto intErr;
	if (!str.len) {
		ERR_EXPECTED_EZ(s, "identifier");
		goto retErr;
	}
	if (!AddIdTok(tb, &str)) goto intErr;

	// Eat whitespace but remember if any whitespace was encountered
	bool ws = false;
	while (1) {
		c = SGetCI(s);
		if (c != ' ' && c != '\t') break;
		ws = true;
	}

	// Check for either var opr or cmd
	if (c == '=') {
		// Add SET token
		VLB_NEXTPTR(*tb, t, 2, 1, goto intErr;);
		t->type = TOK_SET;
	} else if (c == '+' || c == '?') {
		if (SGetCI(s) == '=') {
			// Add CAT or SETNULL token
			VLB_NEXTPTR(*tb, t, 2, 1, goto intErr;);
			t->type = (c == '+') ? TOK_CAT : TOK_SETNULL;
		} else {
			SUnGetC(s);
			if (ws) goto isCmd;
			else goto unExpCharErr;
		}
	} else {
		isCmd:
		if (ws) {
			
			// Read params
			goto ret;
		}
		isEnd:
		if (c == ';' || c == '\n') {
			addEnd:
			VLB_NEXTPTR(*tb, t, 2, 1, goto intErr;);
			t->type = TOK_END;
		} else if (c == '&' || c == '|' || c == SEOF) {
			SUnGetC(s);
			goto addEnd;
		} else {
			goto unExpCharErr;
		}
	}

	ret:
	return tb->len - initTokCt;

	unExpCharErr:
	ERR_UNEXPCHAR_EZ(s);
	goto retErr;
	intErr:
	ERR_INT();
	retErr:
	tb->len = initTokCt;
	VLB_FREE(str);
	return -1;

}

#endif
