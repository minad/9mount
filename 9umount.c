/* © 2008 sqweek <sqweek@gmail.com>
 * See COPYING for details.
 */
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <err.h>
#include <limits.h>

#include <sys/types.h>
#include <sys/mount.h>
#include <mntent.h>
#include <pwd.h>

int indir(struct mntent *mnt, const char *dir) {
	return strstr(mnt->mnt_dir, dir) == dir;
}

int mountedby(struct mntent *mnt, const char *user) {
        char* s = strdup(mnt->mnt_opts);
	if (!s)
		errx(1, "out of memory");
        char* tok;
        int ret = 0;
        while ((tok = strsep(&s, ","))) {
		if (strstr(tok, "name=") == tok) {
			ret = strcmp(tok + 5, user) == 0;
                        break;
		}
	}
	free(s);
	return ret;
}

int main(int argc, char* argv[]) {
        if (argc < 2) {
                fprintf(stderr, "usage: 9umount mountpt...\n");
                return 1;
        }

	struct passwd *pw = getpwuid(getuid());
	if (!pw)
		err(1, "who are you? getpwuid failed");

	int ret = 0;
	while (*++argv) {
                char path[PATH_MAX];
                realpath(*argv, path);

                FILE* fp = setmntent("/proc/mounts", "r");
                if (!fp)
                        err(1, "couldn't open /proc/mounts");

                struct mntent *mnt;
		while ((mnt = getmntent(fp))) {
			if (!strcmp(path, mnt->mnt_dir)) {
				int inhome = indir(mnt, pw->pw_dir);
				if (!inhome && strcmp(mnt->mnt_type, "9p")) {
					warn("%s: refusing to unmount non-9p fs", path);
					ret = 1;
				} else if (!inhome && !mountedby(mnt, pw->pw_name)) {
					warn("%s: not mounted by you", path);
					ret = 1;
				} else if (umount(mnt->mnt_dir)) {
					warn("umount %s", mnt->mnt_dir);
					ret = 1;
				}
                                goto done;
			}
		}
		warn("%s not found in /proc/mounts", path);
		ret = 1;
        done:
                endmntent(fp);
	}

	return ret;
}
