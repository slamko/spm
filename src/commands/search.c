#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <dirent.h>
#include <string.h>
#include <stdbool.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <fcntl.h>
#include <math.h>
#include <errno.h>
#include <stdarg.h> 
#include <ctype.h>
#include <pwd.h>
#include "commands/search.h"
#include "deftypes.h"
#include "utils/entry-utils.h"
#include "utils/pathutils.h"
#include "utils/logutils.h"

int 
is_line_separator(const char *line) {
    return (line[0] & line[1] & line[2]) == '-';
}

result 
check_matched_all(const bool *is_matched, const size_t wordcount) {
    for (size_t i = 0; i < wordcount; i++) {
        if (!is_matched[i])
            FAIL()
    }
    RET_OK()
}

int 
iter_search_words(const char *searchbuf, bool *matched, const searchsyms *sargs) {
    for (size_t i = 0; i < sargs->wordcount; i++) {
        if (!matched[i]) {
            if (strstr(searchbuf, sargs->words[i])) {
                matched[i] = true;

                if (IS_OK(check_matched_all(matched, sargs->wordcount)))
                    return 1;
            }
        }
    }
    return 0;
}

int
toolname_contains_searchword(const char *toolname, bool *matched, const searchsyms *sargs) {
    return iter_search_words(toolname, matched, sargs);
}

result
searchdescr(FILE *descfile, const char *toolname, const searchsyms *sargs) {
    char searchbuf[LINEBUF] = {0};
    bool *matched = NULL;

    ZIC_RESULT_INIT()

    matched = calloc(sargs->wordcount, sizeof(*matched));
    UNWRAP_PTR (matched)

    if (toolname_contains_searchword(toolname, matched, sargs))
        RET_OK_CLEANUP()

    while (fgets(searchbuf, sizeof(searchbuf), descfile)) {
        if (iter_search_words(searchbuf, matched, sargs))
            RET_OK_CLEANUP()

        memset(searchbuf, ASCNULL, sizeof(searchbuf));
    }

    ZIC_RESULT = check_matched_all(matched, sargs->wordcount);
    
    CLEANUP(free(matched))
}

char *
tryread_desc(FILE *index, char *buf, const bool descrexists) {
    if (descrexists) {
        return fgets(buf, LINEBUF, index);
    }
    return fgets(buf, DESCRIPTION_SECTION_LENGTH, index);
}

int
read_description(FILE *descfile, const char *indexmd) {
    FILE *index = NULL;
    char tempbuf[LINEBUF] = {0};
    char linebuf[LINEBUF] = {0};
    bool description_exists = false;

    ZIC_RESULT_INIT()

    UNWRAP_PTR (index = fopen(indexmd, "r"))

    for(int descrlen = 0; 
        tryread_desc(index, linebuf, description_exists);
        ) {
        if (description_exists) {
            if (is_line_separator(linebuf)) {
                if (descrlen > 1)
                    break;
            } else {
                if (descrlen > 0)
                    UNWRAP_NEG_CLEANUP (fputs(tempbuf, descfile))
                
                memcpy(tempbuf, linebuf, sizeof(linebuf));
            }
            descrlen++;
        } else {
            description_exists = !strcmp(linebuf, DESCRIPTION_SECTION);
        }
        memset(linebuf, ASCNULL, sizeof(linebuf));
    }

    if (description_exists) {
        UNWRAP_NEG_CLEANUP (fflush(descfile))
    }

    RET_OK_CLEANUP()
    CLEANUP (fclose(index))
}

result 
lock_if_multithreaded(pthread_mutex_t *mutex) {
    if (mutex)
        return pthread_mutex_lock(mutex);

    RET_OK()
}

result 
unlock_if_multithreaded(pthread_mutex_t *mutex) {
    if (mutex)
        return pthread_mutex_unlock(mutex);
    
    RET_OK()
}

result 
print_matched_entry(FILE *descfile, FILE *targetf, const char *entryname) {
    char *print_buf = NULL;
    size_t descf_size;
    static int matchedc;
    ZIC_RESULT_INIT()

    matchedc++;

    UNWRAP_NEG (fprintf(targetf, "\n------------------------------------------------------------"))
    UNWRAP_NEG (fprintf(targetf, "\n%d) %s:\n", matchedc, entryname))

    UNWRAP_NEG (fseek(descfile, 0, SEEK_END))
    UNWRAP_NEG (descf_size = ftell(descfile))
    UNWRAP_NEG (fseek(descfile, 0, SEEK_SET))

    print_buf = calloc(descf_size + 1, sizeof(*print_buf));
    UNWRAP_PTR (print_buf)

    if (fread(print_buf, descf_size, sizeof(*print_buf), descfile) == 0) {
        UNWRAP_CLEANUP (ferror(descfile))
    }

    if (fwrite(print_buf, descf_size, sizeof(*print_buf), descfile) == 0) {
        UNWRAP_CLEANUP (ferror(descfile))
    }

    CLEANUP(free(print_buf))
}

int
lookup_entries_args(const char *descfname, const int startpoint, 
                    const int endpoint, const int outfd, const char *patchdir, 
                    const searchsyms *sargs, pthread_mutex_t *fmutex) {
    DIR *pd;
    FILE *descfile, *rescache;
    struct dirent *pdir;
    int res = FAIL;

    printf("\nStart point: %d", startpoint);
    printf("\nEnd point: %d", endpoint);

    rescache = fdopen(outfd, "w");
    P_UNWRAP(rescache)

    pd = opendir(patchdir);
    if (!pd) {
        res = ERR_SYS;
        goto cleanuprescache;
    }
    
    while ((pdir = readdir(pd)) != NULL) {
        if (OK(check_isdir(pdir))) {
            char *indexmd = NULL; 

            descfile = fopen(descfname, "w+");
            if (!descfile) {
                res = ERR_SYS;
                goto cleanuppdir;
            }

            res = append_patchmd(&indexmd, patchdir, pdir->d_name);
            if (!OK(res)) {
                goto cleanall;
            }

            if (OK(read_description(descfile, indexmd))) {
                fseek(descfile, 0, SEEK_SET);
                int searchres = searchdescr(descfile, pdir->d_name, sargs);

                lock_if_multithreaded(fmutex);
                fseek(descfile, 0, SEEK_SET);
                
                if (OK(searchres)) {
                    print_matched_entry(descfile, rescache, pdir->d_name);
                }

                unlock_if_multithreaded(fmutex);
            }
            free(indexmd);
            fclose(descfile);
        }
    }

    res = 0;

cleanall:
    if (remove(descfname))
        res = ERR_SYS;
cleanuppdir:
    if (closedir(pd))
        res = ERR_SYS;
cleanuprescache:
    if (fclose(rescache))
        res = ERR_SYS;
    return res;
}

int 
lookup_entries(const lookupthread_args *args) {
    return lookup_entries_args(args->descfname, args->startpoint, args->endpoint, 
        args->outfd, args->patchdir, args->searchargs, args->mutex);
}

void *
search_entry(void *thread_args) {
    lookupthread_args *args = (lookupthread_args *)thread_args;
    args->result = lookup_entries(args);
    return NULL;
}