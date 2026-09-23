#define _POSIX_C_SOURCE 200809L
#define _DEFAULT_SOURCE
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/inotify.h>
#include <sys/stat.h>
#include <sys/wait.h>

/* snapguard-watch: SnapGuard in real time. It starts at login and watches the
 * Downloads folder and the USB drives that get plugged in. Every file that
 * lands there is scanned as soon as it is complete; a threat is contained
 * (or only reported, per /etc/snapos/onInfected) and a desktop notification
 * says what happened. A drive that is plugged in is scanned whole. */

#define MAX_WATCH 512
#define RECENT 64

static struct { int wd; char path[PATH_MAX]; } watches[MAX_WATCH];
static int nwatch = 0;
static int fd = -1;
static char pidfile[PATH_MAX];
static char media_root[PATH_MAX];
static int settle_ms = 1500;
static int verbose = 0;

static struct { char path[PATH_MAX]; time_t when; } recent[RECENT];
static int nrecent = 0;

static const char *notify_cmd(void) {
    const char *c = getenv("SNAPGUARD_WATCH_NOTIFY");
    return (c && *c) ? c : "notify-send";
}

static void log_line(const char *fmt, const char *a) {
    if (!verbose) return;
    fprintf(stderr, "snapguard-watch: ");
    fprintf(stderr, fmt, a);
    fputc('\n', stderr);
}

static void notify(const char *title, const char *body, int critical) {
    fflush(NULL);
    pid_t pid = fork();
    if (pid != 0) return;
    int devnull = open("/dev/null", O_RDWR);
    if (devnull >= 0) { dup2(devnull, 1); dup2(devnull, 2); }
    execlp(notify_cmd(), notify_cmd(), "-a", "SnapGuard", "-i", "snapguard",
           "-u", critical ? "critical" : "normal", title, body, (char *)NULL);
    _exit(127);
}

/* The system policy: contain the threat, or only warn. */
static int contain_threats(void) {
    const char *e = getenv("SNAPOS_ONINFECTED");
    char buf[32] = "contain";
    if (e && *e) snprintf(buf, sizeof buf, "%s", e);
    else {
        FILE *f = fopen("/etc/snapos/onInfected", "r");
        if (f) { if (!fgets(buf, sizeof buf, f)) buf[0] = 0; fclose(f); }
    }
    return strncmp(buf, "warn", 4) != 0;
}

static int run_capture(char *const argv[], char *out, size_t n) {
    int fds[2];
    out[0] = 0;
    if (pipe(fds) != 0) return -1;
    fflush(NULL);
    pid_t pid = fork();
    if (pid < 0) { close(fds[0]); close(fds[1]); return -1; }
    if (pid == 0) {
        close(fds[0]);
        dup2(fds[1], 1);
        dup2(fds[1], 2);
        close(fds[1]);
        execvp(argv[0], argv);
        _exit(127);
    }
    close(fds[1]);
    size_t used = 0;
    ssize_t r;
    char tmp[512];
    while ((r = read(fds[0], tmp, sizeof tmp)) > 0) {
        size_t take = (size_t)r;
        if (used + take >= n) take = n - 1 - used;
        memcpy(out + used, tmp, take);
        used += take;
    }
    out[used] = 0;
    close(fds[0]);
    int st;
    while (waitpid(pid, &st, 0) < 0 && errno == EINTR) {}
    return WIFEXITED(st) ? WEXITSTATUS(st) : -1;
}

/* The file name, as plain text: notification bodies are markup, so a name
 * with < or & must not be able to change how the message looks. */
static const char *base_of(const char *p) {
    static char safe[512];
    const char *b = strrchr(p, '/');
    b = b ? b + 1 : p;
    size_t j = 0;
    for (const char *c = b; *c && j + 6 < sizeof safe; c++) {
        if (*c == '<') { memcpy(safe + j, "&lt;", 4); j += 4; }
        else if (*c == '>') { memcpy(safe + j, "&gt;", 4); j += 4; }
        else if (*c == '&') { memcpy(safe + j, "&amp;", 5); j += 5; }
        else safe[j++] = *c;
    }
    safe[j] = 0;
    return safe;
}

/* Runs in a child, so a long scan never holds up the watching. */
static void scan_path(const char *path, int is_drive) {
    fflush(NULL);
    pid_t pid = fork();
    if (pid != 0) return;
    /* the parent ignores SIGCHLD so scans reap themselves; this child must
     * wait for the scanner it runs, so it takes the default back */
    signal(SIGCHLD, SIG_DFL);
    struct timespec ts = { settle_ms / 1000, (settle_ms % 1000) * 1000000L };
    nanosleep(&ts, NULL);
    struct stat st;
    if (stat(path, &st) != 0) _exit(0);
    char out[4096], body[600];
    char *scan[] = { "snapguard", "scan", (char *)path, NULL };
    int rc = run_capture(scan, out, sizeof out);
    if (rc == 0) {
        if (is_drive) {
            snprintf(body, sizeof body, "%s was scanned and is clean.", base_of(path));
            notify("USB drive checked", body, 0);
        }
        _exit(0);
    }
    if (rc == 1) {
        if (is_drive) {
            snprintf(body, sizeof body, "A threat was found on %s. Open SnapGuard to review it.", base_of(path));
            notify("Threat on a USB drive", body, 1);
            _exit(0);
        }
        if (contain_threats()) {
            char *q[] = { "snapguard", "quarantine", (char *)path, NULL };
            char qout[512];
            run_capture(q, qout, sizeof qout);
            snprintf(body, sizeof body, "%s was moved to quarantine. Open SnapGuard to keep it or delete it.", base_of(path));
            notify("Threat contained", body, 1);
        } else {
            snprintf(body, sizeof body, "%s looks dangerous. Do not open it; check it in SnapGuard.", base_of(path));
            notify("Threat found", body, 1);
        }
        _exit(0);
    }
    /* Said once per session: before the virus database is in place every
     * file would be reported, which teaches people to ignore the message. */
    char marker[PATH_MAX + 16];
    snprintf(marker, sizeof marker, "%s.limited", pidfile);
    if (access(marker, F_OK) != 0) {
        int mf = open(marker, O_WRONLY | O_CREAT, 0644);
        if (mf >= 0) close(mf);
        snprintf(body, sizeof body, "%s could not be scanned: the virus database is not ready yet. See snapguard status.", base_of(path));
        notify("SnapGuard could not scan a file", body, 0);
    }
    _exit(0);
}

static int seen_recently(const char *path) {
    time_t now = time(NULL);
    for (int i = 0; i < nrecent; i++)
        if (!strcmp(recent[i].path, path) && now - recent[i].when < 3) return 1;
    int slot = nrecent < RECENT ? nrecent++ : (int)(now % RECENT);
    snprintf(recent[slot].path, sizeof recent[slot].path, "%s", path);
    recent[slot].when = now;
    return 0;
}

/* Browsers and copies write a temporary name first; the real file arrives
 * with IN_MOVED_TO or IN_CLOSE_WRITE under its final name. */
static int temporary_name(const char *name) {
    size_t n = strlen(name);
    if (name[0] == '.') return 1;
    static const char *ends[] = { ".part", ".crdownload", ".tmp", ".partial", "~", NULL };
    for (int i = 0; ends[i]; i++) {
        size_t m = strlen(ends[i]);
        if (n > m && !strcmp(name + n - m, ends[i])) return 1;
    }
    return 0;
}

static int add_watch(const char *path, uint32_t mask) {
    if (nwatch >= MAX_WATCH) return -1;
    int wd = inotify_add_watch(fd, path, mask);
    if (wd < 0) return -1;
    for (int i = 0; i < nwatch; i++) if (watches[i].wd == wd) return wd;
    watches[nwatch].wd = wd;
    snprintf(watches[nwatch].path, sizeof watches[nwatch].path, "%s", path);
    nwatch++;
    log_line("watching %s", path);
    return wd;
}

static const char *watch_path(int wd) {
    for (int i = 0; i < nwatch; i++) if (watches[i].wd == wd) return watches[i].path;
    return NULL;
}

static void drop_watch(int wd) {
    for (int i = 0; i < nwatch; i++)
        if (watches[i].wd == wd) { watches[i] = watches[--nwatch]; return; }
}

#define FILE_EVENTS (IN_CLOSE_WRITE | IN_MOVED_TO | IN_CREATE | IN_DELETE_SELF)

/* Folders inside a watched folder are watched too (a few levels is enough). */
static void watch_tree(const char *dir, int depth) {
    if (add_watch(dir, FILE_EVENTS) < 0 || depth <= 0) return;
    DIR *dh = opendir(dir);
    if (!dh) return;
    struct dirent *e;
    while ((e = readdir(dh))) {
        if (e->d_name[0] == '.') continue;
        char sub[PATH_MAX];
        if (snprintf(sub, sizeof sub, "%s/%s", dir, e->d_name) >= (int)sizeof sub) continue;
        struct stat st;
        if (lstat(sub, &st) == 0 && S_ISDIR(st.st_mode)) watch_tree(sub, depth - 1);
    }
    closedir(dh);
}

static void handle(const struct inotify_event *ev) {
    if (ev->mask & IN_Q_OVERFLOW) return;
    const char *dir = watch_path(ev->wd);
    if (!dir) return;
    if (ev->mask & IN_IGNORED) { drop_watch(ev->wd); return; }
    if (ev->mask & IN_DELETE_SELF) { inotify_rm_watch(fd, ev->wd); drop_watch(ev->wd); return; }
    if (!ev->len) return;
    char path[PATH_MAX];
    if (snprintf(path, sizeof path, "%s/%s", dir, ev->name) >= (int)sizeof path) return;

    if (ev->mask & IN_ISDIR) {
        if (!(ev->mask & (IN_CREATE | IN_MOVED_TO))) return;
        int is_drive = media_root[0] && !strcmp(dir, media_root);
        if (is_drive) {
            /* a drive was mounted: watch it, and check everything on it */
            watch_tree(path, 2);
            scan_path(path, 1);
        } else {
            watch_tree(path, 2);
        }
        return;
    }
    if (!(ev->mask & (IN_CLOSE_WRITE | IN_MOVED_TO))) return;
    if (temporary_name(ev->name)) return;
    if (seen_recently(path)) return;
    log_line("new file %s", path);
    scan_path(path, 0);
}

static void on_signal(int sig) {
    (void)sig;
    if (pidfile[0]) {
        char marker[PATH_MAX + 16];
        snprintf(marker, sizeof marker, "%s.limited", pidfile);
        unlink(marker);
        unlink(pidfile);
    }
    _exit(0);
}

static void pidfile_path(char *dst, size_t n) {
    const char *rt = getenv("XDG_RUNTIME_DIR");
    if (rt && *rt) snprintf(dst, n, "%s/snapguard-watch.pid", rt);
    else snprintf(dst, n, "/tmp/snapguard-watch-%d.pid", (int)getuid());
}

static int already_running(void) {
    FILE *f = fopen(pidfile, "r");
    if (!f) return 0;
    int pid = 0;
    if (fscanf(f, "%d", &pid) != 1) pid = 0;
    fclose(f);
    return pid > 0 && kill(pid, 0) == 0;
}

static void usage(FILE *f) {
    fprintf(f,
        "snapguard-watch [-v] [FOLDER...]\n\n"
        "Watches folders and scans every new file with SnapGuard. Without\n"
        "arguments it watches ~/Downloads and the USB drives under /run/media.\n"
        "Starts at login; a second copy exits at once.\n");
}

int main(int argc, char **argv) {
    const char *dirs[MAX_WATCH];
    int ndirs = 0;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "-v")) verbose = 1;
        else if (!strcmp(argv[i], "-h") || !strcmp(argv[i], "--help")) { usage(stdout); return 0; }
        else if (ndirs < MAX_WATCH) dirs[ndirs++] = argv[i];
    }
    const char *s = getenv("SNAPGUARD_WATCH_SETTLE");
    if (s && atoi(s) >= 0) settle_ms = atoi(s);

    pidfile_path(pidfile, sizeof pidfile);
    if (already_running()) { fprintf(stderr, "snapguard-watch: already running\n"); return 0; }
    FILE *pf = fopen(pidfile, "w");
    if (pf) { fprintf(pf, "%d\n", (int)getpid()); fclose(pf); }
    signal(SIGTERM, on_signal);
    signal(SIGINT, on_signal);
    signal(SIGCHLD, SIG_IGN);

    fd = inotify_init1(IN_CLOEXEC);
    if (fd < 0) { perror("snapguard-watch: inotify"); return 1; }

    media_root[0] = 0;
    if (ndirs == 0) {
        const char *home = getenv("HOME");
        const char *user = getenv("USER");
        char d[PATH_MAX];
        if (home) {
            snprintf(d, sizeof d, "%s/Downloads", home);
            watch_tree(d, 2);
        }
        if (user) {
            snprintf(media_root, sizeof media_root, "/run/media/%s", user);
            if (add_watch(media_root, IN_CREATE | IN_MOVED_TO | IN_DELETE_SELF) < 0) media_root[0] = 0;
        }
    } else {
        for (int i = 0; i < ndirs; i++) watch_tree(dirs[i], 2);
    }
    if (nwatch == 0) { fprintf(stderr, "snapguard-watch: nothing to watch\n"); unlink(pidfile); return 1; }

    char buf[64 * 1024] __attribute__((aligned(8)));
    for (;;) {
        ssize_t n = read(fd, buf, sizeof buf);
        if (n < 0) { if (errno == EINTR) continue; break; }
        for (char *p = buf; p < buf + n;) {
            const struct inotify_event *ev = (const struct inotify_event *)p;
            handle(ev);
            p += sizeof *ev + ev->len;
        }
    }
    unlink(pidfile);
    return 0;
}
