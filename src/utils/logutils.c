#include <stdarg.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "def.h"
#include "utils/logutils.h"

static const char *usage_msg = "Usage: \n"
	"\tspm [search] <tool> <keywords>\n"
	"\tspm <sync>\n"
	"\tspm <open> <tool> <patch> [-b]\n"
	"\tspm <load> <tool> <patch> [-a]\n";

void 
error(const char* err_format, ...) {
    va_list args;

    va_start(args, err_format);
    FORMAT_ERR(err_format, args)
    va_end(args);
}

void bug(const char *filename, int linenum, const char *bug_msg, ...) {
    va_list args;

    fprintf(stderr, BUG_PREFIX, filename, linenum, "");
    vfprintf(stderr, bug_msg, args);
    fprintf(stderr, "\n");
    va_end(args);
}

void
print_usage(void) {
    puts(usage_msg);
}

void 
error_nolocalrepo(void) {
    error("Unable to find base suckless repo. Try running 'spm sync'");
}
