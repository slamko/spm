#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <dirent.h>
#include <string.h>
#include <bsd/string.h> 
#include <stdbool.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <fcntl.h>
#include <pthread.h>
#include <math.h>
#include <errno.h>
#include <stdarg.h> 
#include <ctype.h>
#include <pwd.h>
#include <time.h>
#include "def.h"
#include "commands/runsearch.h"
#include "commands/sync.h"
#include "commands/open.h"
#include "commands/download.h"
#include "utils/pathutils.h"
#include "utils/logutils.h"

char *basecacherepo = NULL;

typedef int(*commandp)(int, char **); 

enum command {
    SYNC = 0,
    SEARCH = 1,
    DOWNLOAD = 2,
    OPEN = 3
};

int
get_repocache(char **cachedirbuf) {
    char *homedir = NULL;
    struct passwd *pd = NULL;
    size_t homedirplen;

    homedir = getenv("HOME");
    if (!homedir) {
        pd = getpwuid(getuid());

        if (!pd)
            return 1;
        homedir = pd->pw_dir;
    }

    homedirplen = strnlen(homedir, PATHBUF);

    *cachedirbuf = calloc(PATHBUF + homedirplen + 1, sizeof(**cachedirbuf));
    if (!*cachedirbuf)
        return 1;

    strlcpy(*cachedirbuf, homedir, PATHBUF);
    strlcpy(*cachedirbuf + homedirplen, BASEREPO, PATHBUF);
    return !*cachedirbuf;
}

int
try_sync_caches(void) {
    struct stat cache_sb;
    time_t lastmtime, curtime;
    struct tm *lmttm = NULL, *cttm = NULL;

    if (stat(basecacherepo, &cache_sb) == -1) {
        return 1;
    }
    
    lastmtime = cache_sb.st_mtim.tv_sec;
    time(&curtime);
    cttm = gmtime(&curtime);
    lmttm = gmtime(&lastmtime);

    if (cttm->tm_mday - lmttm->tm_mday >= SYNC_INTERVAL_D || 
        (cttm->tm_mon > lmttm->tm_mon && cttm->tm_mday > SYNC_INTERVAL_D) || 
        (cttm->tm_year > lmttm->tm_year && cttm->tm_mday > SYNC_INTERVAL_D)) {
        return run_sync();
    }
    return 0;
}

int
parse_command(const int argc, char **argv, enum command *commandarg) {
    if (argc <= 1)
        return 1;

    if (OK(strncmp(argv[CMD_ARGPOS], SYNC_CMD, CMD_LEN))) {
        *commandarg = SYNC;   
    } else if (OK(strncmp(argv[CMD_ARGPOS], DOWNLOAD_CMD, CMD_LEN))) {
        *commandarg = DOWNLOAD;
    } else if (OK(strncmp(argv[CMD_ARGPOS], OPEN_CMD, CMD_LEN))) {
        *commandarg = OPEN;
    } else {
        *commandarg = SEARCH;
    }

    return 0;
}

int
main(int argc, char **argv) {
    enum command cmd;
    commandp commands[] = {
        &parse_sync_args,
        &parse_search_args,
        &parse_open_args,
        &parse_load_args
    };

    if (parse_command(argc, argv, &cmd)) {
        print_usage();
        return EXIT_FAILURE;
    }

    if (get_repocache(&basecacherepo)) {
        return EXIT_FAILURE;
    }
    
    if (cmd != SYNC) {
        if (!check_baserepo_exists()) {
            error_nolocalrepo();
            return EXIT_FAILURE;
        }

        if (try_sync_caches()) {
            error("Failed to autosync caches. Continuing without syncing...");
        }
    }

    return commands[(int)cmd](argc, argv);
}