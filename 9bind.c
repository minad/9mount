/* © 2008 sqweek <sqweek@gmail.com>
 * See COPYING for details.
 */
#include <err.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>

#include <sys/stat.h>
#include <sys/mount.h>

int main(int argc, char* argv[]) {
	if (argc != 3) {
                fprintf(stderr, "usage: 9bind old new\n");
                return 1;
        }

        char* old = argv[1];
        char* new = argv[2];

	/* Make sure mount exists, is writable, and not sticky */
	struct stat st;
	if (stat(new, &st) || access(new, W_OK))
		err(1, "%s", new);

	if (st.st_mode & S_ISVTX)
		errx(1, "%s: refusing to bind over sticky directory", new);

	if (mount(old, new, 0, MS_BIND, 0))
		err(1, "mount");

	return 0;
}
