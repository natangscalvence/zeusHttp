/**
 * privileges.c
 * Implements functions for dropping root privileges via setuid/setgid.
 */

#include "../../include/zeushttp.h"
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <unistd.h>
#include <errno.h>
#include <pwd.h>
#include <grp.h>

/**
 * Default non-privileged user to switch to.
 */

#define DEFAULT_USER "zeushttp"

/**
 * Drops root privileges to a less privileged user.
 */
int drop_privileges(const char *user_name) {
    struct passwd *pw;

    /**
     * Verify if we are running as root (UID 0)
     */
    if (getuid() != 0) {
        printf("Security: Privileges already dropped (not running as root).\n");
        return 0;
    }

    /**
     * Obtain UID and GID from user.
     */
    pw = getpwnam(user_name);
    if (pw == NULL) {
        fprintf(stderr, "Security Error: User '%s' not found. Cannot drop privileges.\n", user_name);
        return -1;
    }

    /**
     * Remove supplementary groups (setgid first).
     */
    if (setgid(pw->pw_gid) == -1 || setgroups(1, &pw->pw_gid) == -1) {
        perror("Security Error: Failed to drop group privileges.");
        return -1;
    }

    /**
     * Remove privileges from user. (setuid).
     */
    if (setuid(pw->pw_uid) == -1) {
        perror("Security Error: Failed to drop user privileges.");
        return -1;
    }

    printf("Security: Privileges successfully dropped to user '%s'. Effective UID: %d\n", user_name, geteuid());
    return 0;
}

/**
 * Utility function.
 */

int zeus_drop_privileges() {
    return drop_privileges(DEFAULT_USER);
}

