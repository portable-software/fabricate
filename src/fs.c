#include "fs.h"

FileInfo* Ls(const char* path, const char* wildcard, bool excldDirs, size_t* count) {
	// TODO
	*count = -1;
	return NULL;
}
void FreeLs(FileInfo* info) {
	free(info[-1].name);
	free(info);
}
