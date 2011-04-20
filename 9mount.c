// © 2008 sqweek <sqweek@gmail.com>
// © 2011 minad  <mail@daniel-mendler.de>
// See COPYING for details.
#include <err.h>
#include <mntent.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <regex.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <sys/mount.h>
#include <arpa/inet.h>
#include <pwd.h>
#include <netdb.h>

struct {
        char* mnemonic;
        int   mask;
} const debug_flags[] = {
	{ "err",   0x001 },
	{ "devel", 0x002 },
	{ "9p",    0x004 },
	{ "vfs",   0x008 },
	{ "conv",  0x010 },
	{ "mux",   0x020 },
	{ "trans", 0x040 },
	{ "alloc", 0x080 },
	{ "fcall", 0x100 },
	{ "fid",   0x200 },
        { "pkt",   0x400 },
	{ "cache", 0x800 },
        { 0,       0     },
};

// Append option string to dest
char* append(char **dest, size_t *destlen, char *fmt, ...) {
        char src[64];
        va_list ap;

        va_start(ap, fmt);
        vsnprintf(src, sizeof (src), fmt, ap);
        va_end(ap);

	while (strlen(*dest) + 1 + strlen(src) > *destlen)
		*destlen *= 2;
        *dest = realloc(*dest, *destlen);
	if (!*dest)
		errx(1, "out of memory");
	if (**dest)
		strcat(*dest, ",");
	strcat(*dest, src);
	return *dest;
}

// Check if string matches regex
int match(const char* pattern, const char* str) {
        regex_t re;
        if (regcomp(&re, pattern, REG_EXTENDED|REG_NOSUB) != 0)
                err(1, "regcomp");
        int status = regexec(&re, str, 0, 0, 0);
        regfree(&re);
        return !status;
}

// Check argument for invalid characters
void checkarg(const char* arg) {
        if (!match("^[A-Za-z0-9_-]+$", arg))
                errx(1, "%s: argument contains invalid characters", arg);
}

// Resolve hostname and return ip string
char* resolve_host(const char* addr) {
        static char host[256];
        struct addrinfo* ai;
        int r;
        if ((r = getaddrinfo(addr, 0, 0, &ai)))
                errx(1, "getaddrinfo: %s", gai_strerror(r));
        if ((r = getnameinfo(ai->ai_addr, ai->ai_addrlen, host,
                             sizeof (host), 0, 0, NI_NUMERICHOST)))
                errx(1, "getnameinfo: %s", gai_strerror(r));
        return host;
}

// Resolve port number from /etc/services
int resolve_port(const char* port) {
        if (match("^[0-9]+$", port))
                return atoi(port);
        struct servent *sv;
        if ((sv = getservbyname(port, "tcp"))) {
                endservent();
                return ntohs((uint16_t)sv->s_port);
        }
        errx(1, "unknown service %s", port);
}

int main(int argc, char *argv[]) {
        char c;
        int accessopt = 0, dotu = 0, uidgid = 0, dev = 0, dryrun = 0, maxdata = -1;
        char *debugstr = 0, *cache = 0, *aname = 0;
        while ((c = getopt(argc, argv, "insuvxa:c:d:m:")) != -1) {
                switch (c) {
                case 'i': uidgid = 1; break;
                case 'n': dryrun = 1; break;
                case 's': accessopt = -1; break;
                case 'u': dotu = 1; break;
                case 'v': dev = 1; break;
                case 'x': accessopt = getuid(); break;
                case 'a': aname = optarg; break;
                case 'c': cache = optarg; break;
                case 'd': debugstr = optarg; break;
                case 'm':
                        maxdata = atoi(optarg);
                        if (maxdata <= 0)
                                errx(1, "invalid maxdata %s", optarg);
                        break;
                case '?':
                        return 1;
                default:
                        abort ();
                        break;
                }
        }

        if (optind + 2 > argc) {
                fprintf(stderr, "usage: 9mount [ -insuvx ] [ -a spec ] [ -c cache ] [ -d debug ] [ -m maxdata ] dial mountpt\n");
                return 1;
        }

        char* dial = argv[optind];
        char* mountpt = argv[optind + 1];

        struct passwd *pw = getpwuid(getuid());
	if(!pw)
		err(1, "who are you? getpwuid failed");

	// Make sure mount exists and is writable
        struct stat st;
	if (stat(mountpt, &st) || access(mountpt, W_OK))
		err(1, "%s", mountpt);

        // Make sure mount is not sticky
	if (st.st_mode & S_ISVTX)
		errx(1, "%s: refusing to mount over sticky directory", mountpt);

        size_t optlen = 64;
        char* opts = calloc(optlen, 1);
	if (!opts)
		errx(1, "out of memory");

        char* addr;
        // Stdin/Stdout
	if (!strcmp(dial, "-")) {
		addr = "nodev";
		append(&opts, &optlen, "trans=fd,rfdno=0,wrfdno=1");
	}
        // Virtio transport "virtio:channel"
        else if (strstr(dial, "virtio:") == dial) {
                addr = dial + 7;
                append(&opts, &optlen, "trans=virtio");
        }
        // Unix socket "path/to/socket"
        else if (strchr(dial, '/') || (!stat(dial, &st) && S_ISSOCK(st.st_mode))) {
                addr = dial;
                append(&opts, &optlen, "trans=unix");
        }
        // TCP transport "hostname:port"
        else {
                char* port = strchr(dial, ':');
                if (!port)
                        errx(1, "invalid dial %s", dial);
                *port++ = '\0';
                addr = resolve_host(dial);
                append(&opts, &optlen, "trans=tcp,port=%d", resolve_port(port));
        }

        // Name of the exported filetree
	if (aname) {
                checkarg(aname);
		append(&opts, &optlen, "aname=%s", aname);
	}

        // Caching
	if (cache) {
                if (strcmp(cache, "loose") && strcmp(cache, "fscache"))
                        errx(1, "cache must be loose or fscache");
		append(&opts, &optlen, "cache=%s", cache);
	}

        // Debugging options (Comma separated options)
	if (debugstr) {
                char* flag;
                int debug = 0;
                while ((flag = strsep(&debugstr, ","))) {
                        int i;
			for (i = 0; debug_flags[i].mnemonic; ++i) {
				if (!strcmp(flag, debug_flags[i].mnemonic)) {
					debug |= debug_flags[i].mask;
					break;
				}
			}
                        if (!debug_flags[i].mnemonic)
				errx(1, "%s: unrecognised debug flag", flag);
                }
		append(&opts, &optlen, "debug=0x%04x", debug);
	}

        // The number of bytes to use for 9p packet payload
	if (maxdata > 0)
		append(&opts, &optlen, "maxdata=%d", maxdata);

        // User name to attempt mount as on the remote server
        char* user = getenv("USER");
	if (!user)
		user = pw->pw_name;
        checkarg(user);
	append(&opts, &optlen, "uname=%s", user);

        // Access mode
	if (accessopt < 0)
		append(&opts, &optlen, "access=any");
	else if (accessopt)
		append(&opts, &optlen, "access=%d", accessopt);

        // Force legacy mode
	if (!dotu)
		append(&opts, &optlen, "noextend");

        // Do not map special files - represent them as normal files.
	if (!dev)
		append(&opts, &optlen, "nodevmap");

        // Attempt to mount as a particular uid
	if (uidgid)
                append(&opts, &optlen, "dfltuid=%d,dfltgid=%d", getuid(), getgid());

	if(dryrun)
		fprintf(stderr, "mount -t 9p -o %s %s %s\n", opts, addr, mountpt);
	else if (mount(addr, mountpt, "9p", 0, opts))
		err(1, "mount");

	return 0;
}
