#define _POSIX_C_SOURCE 200809L
#define _DEFAULT_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <unistd.h>
#include <dirent.h>
#include <limits.h>
#include <ctype.h>
#include <time.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/utsname.h>
#include <sys/wait.h>

/* snap-deb: Debian packages on SnapOS.
 *
 * A .deb is declared by copying it to <nixdir>/debs. `snap-deb sync` (run by
 * `snapos rebuild`) keeps a small Debian system, the layer, in
 * /var/lib/snapdeb/rootfs and installs every declared .deb there with Debian's
 * own apt, which fetches the libraries the package needs from the Debian
 * archive. `snap-deb run` starts a program inside the layer with bubblewrap: the
 * program sees a Debian root, and the user's home, display, sound and D-Bus
 * from SnapOS. Menu entries and commands are exported to
 * /var/lib/snapdeb/exports so they appear on the desktop like any other. */

#define RED "\033[91m"
#define GRN "\033[32m"
#define YEL "\033[33m"
#define DIM "\033[2m"
#define BLD "\033[1m"
#define RST "\033[0m"

#define LAYER_DEFAULT  "/var/lib/snapdeb"
#define SUITE_DEFAULT  "trixie"
#define MIRROR_DEFAULT "http://deb.debian.org/debian"
#define DEBS_MOUNT     "/snapos-debs"

/* Installed in the layer right after it is created, so that graphical programs
 * have a driver, fonts, settings, icons and certificates without the .deb
 * asking for them. apt installs them, so alternatives and recommends resolve. */
static char *const BASE_PACKAGES[] = {
    "ca-certificates", "libgl1-mesa-dri", "libglx-mesa0", "libegl-mesa0",
    "fontconfig", "fonts-dejavu-core", "dbus-x11", "dconf-gsettings-backend",
    "gsettings-desktop-schemas", "adwaita-icon-theme", "xdg-utils", "procps", NULL
};

#define MAX_PKGS 256

typedef struct {
    char pkg[128], ver[128], arch[32], desc[256], depends[1024];
} DebInfo;

typedef struct {
    char pkg[128];
    char file[256];
} Entry;

static const char *nixdir(void) {
    const char *d = getenv("SNAPOS_NIX_DIR");
    if (d) return d;
    if (access("/etc/snapos/configuration.nix", F_OK) == 0) return "/etc/snapos";
    if (access("/etc/nixos/configuration.nix", F_OK) == 0) return "/etc/nixos";
    return "nix";
}

static const char *layer(void) {
    const char *d = getenv("SNAPDEB_DIR");
    return (d && *d) ? d : LAYER_DEFAULT;
}

static const char *suite(void) {
    const char *s = getenv("SNAPDEB_SUITE");
    return (s && *s) ? s : SUITE_DEFAULT;
}

static const char *mirror(void) {
    const char *m = getenv("SNAPDEB_MIRROR");
    return (m && *m) ? m : MIRROR_DEFAULT;
}

/* Debian's archive keys, shipped with SnapOS, so the layer is verified. */
static const char *keyring(void) {
    const char *k = getenv("SNAPDEB_KEYRING");
    return (k && *k) ? k : "/etc/snapos/debian-archive-keyring.gpg";
}

static const char *bwrap(void) {
    const char *b = getenv("SNAPDEB_BWRAP");
    return (b && *b) ? b : "bwrap";
}

static void pathf(char *dst, size_t n, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(dst, n, fmt, ap);
    va_end(ap);
}

static int exists(const char *p) { return access(p, F_OK) == 0; }
static void mkdir_p(const char *path);

static int is_dir(const char *p) {
    struct stat st;
    return stat(p, &st) == 0 && S_ISDIR(st.st_mode);
}

static int run(char *const argv[]) {
    fflush(NULL);
    pid_t pid = fork();
    if (pid < 0) return -1;
    if (pid == 0) {
        execvp(argv[0], argv);
        _exit(127);
    }
    int st;
    waitpid(pid, &st, 0);
    return WIFEXITED(st) ? WEXITSTATUS(st) : -1;
}

/* Runs a command and captures its stdout. Returns its exit code. */
static int capture(char *const argv[], char *out, size_t n) {
    int fds[2];
    out[0] = 0;
    if (pipe(fds) != 0) return -1;
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
    waitpid(pid, &st, 0);
    return WIFEXITED(st) ? WEXITSTATUS(st) : -1;
}

static void trim(char *s) {
    size_t n = strlen(s);
    while (n && isspace((unsigned char)s[n - 1])) s[--n] = 0;
    char *p = s;
    while (*p && isspace((unsigned char)*p)) p++;
    if (p != s) memmove(s, p, strlen(p) + 1);
}

static int field(const char *deb, const char *name, char *dst, size_t n) {
    char out[2048];
    char *argv[] = { "dpkg-deb", "-f", (char *)deb, (char *)name, NULL };
    dst[0] = 0;
    if (capture(argv, out, sizeof out) != 0) return 0;
    char *nl = strchr(out, '\n');
    if (nl) *nl = 0;
    trim(out);
    snprintf(dst, n, "%.*s", (int)n - 1, out);
    return dst[0] != 0;
}

static int read_info(const char *deb, DebInfo *d) {
    memset(d, 0, sizeof *d);
    if (access(deb, R_OK) != 0) return 0;
    if (!field(deb, "Package", d->pkg, sizeof d->pkg)) return 0;
    field(deb, "Version", d->ver, sizeof d->ver);
    field(deb, "Architecture", d->arch, sizeof d->arch);
    field(deb, "Description", d->desc, sizeof d->desc);
    field(deb, "Depends", d->depends, sizeof d->depends);
    return 1;
}

/* Keeps only characters that are safe in a file name. */
static void clean(char *dst, size_t n, const char *src) {
    size_t j = 0;
    for (size_t i = 0; src[i] && j + 1 < n; i++) {
        unsigned char c = (unsigned char)src[i];
        dst[j++] = (isalnum(c) || c == '.' || c == '+' || c == '-') ? (char)c : '-';
    }
    dst[j] = 0;
    if (dst[0] == '.') dst[0] = '-';
}

static void debs_dir(char *dst, size_t n) { pathf(dst, n, "%s/debs", nixdir()); }

/* Debian's name for this computer's architecture: amd64 or arm64. */
static const char *host_arch(void) {
    static char a[32];
    if (a[0]) return a;
    const char *env = getenv("SNAPDEB_ARCH");
    struct utsname u;
    if (env && *env) snprintf(a, sizeof a, "%s", env);
    else if (uname(&u) == 0 && !strcmp(u.machine, "aarch64")) snprintf(a, sizeof a, "arm64");
    else snprintf(a, sizeof a, "amd64");
    return a;
}

static void target_name(const DebInfo *d, char *dst, size_t n) {
    char p[128], v[128], a[32];
    clean(p, sizeof p, d->pkg);
    clean(v, sizeof v, d->ver[0] ? d->ver : "0");
    clean(a, sizeof a, d->arch[0] ? d->arch : host_arch());
    pathf(dst, n, "%s_%s_%s.deb", p, v, a);
}

/* Runs a command as root when the directory is not writable. */
static int as_owner_of(const char *dir, char *const argv[]) {
    if (geteuid() == 0 || access(dir, W_OK) == 0) return run(argv);
    char *full[32];
    int k = 0;
    full[k++] = "sudo";
    for (int i = 0; argv[i] && k < 30; i++) full[k++] = argv[i];
    full[k] = NULL;
    return run(full);
}

static int as_owner(char *const argv[]) { return as_owner_of(nixdir(), argv); }

static int is_tty(void) { return isatty(STDIN_FILENO) && isatty(STDOUT_FILENO); }

static int ask(const char *question) {
    char line[64];
    printf("  %s [y/N] ", question);
    fflush(stdout);
    if (!fgets(line, sizeof line, stdin)) return 0;
    return line[0] == 'y' || line[0] == 'Y';
}

static void show_info(const char *deb, const DebInfo *d) {
    const char *base = strrchr(deb, '/');
    base = base ? base + 1 : deb;
    printf("\n  %sFile%s        %s\n", DIM, RST, base);
    printf("  %sPackage%s     %s%s%s %s (%s)\n", DIM, RST, BLD, d->pkg, RST, d->ver, d->arch[0] ? d->arch : "?");
    if (d->desc[0]) printf("  %sAbout%s       %s\n", DIM, RST, d->desc);
    if (d->depends[0]) printf("  %sNeeds%s       %.200s\n", DIM, RST, d->depends);
    printf("\n");
}

/* A package that ships a system service would install but never work: the
 * layer runs programs, not services. Returns the unit's file name, or "". */
static const char *service_in(const char *deb) {
    static char unit[256];
    unit[0] = 0;
    int fds[2];
    if (pipe(fds) != 0) return unit;
    fflush(NULL);
    pid_t pid = fork();
    if (pid < 0) { close(fds[0]); close(fds[1]); return unit; }
    if (pid == 0) {
        close(fds[0]);
        dup2(fds[1], 1);
        close(fds[1]);
        int devnull = open("/dev/null", O_WRONLY);
        if (devnull >= 0) dup2(devnull, 2);
        execlp("dpkg-deb", "dpkg-deb", "-c", deb, (char *)NULL);
        _exit(127);
    }
    close(fds[1]);
    FILE *f = fdopen(fds[0], "r");
    char line[PATH_MAX];
    while (f && fgets(line, sizeof line, f)) {
        trim(line);
        const char *p = strstr(line, "lib/systemd/system/");
        if (!p) p = strstr(line, "etc/init.d/");
        if (!p) continue;
        const char *name = strrchr(p, '/');
        if (!name || !name[1]) continue;
        if (!strstr(name, ".service") && !strstr(line, "init.d/")) continue;
        snprintf(unit, sizeof unit, "%.255s", name + 1);
        break;
    }
    if (f) fclose(f); else close(fds[0]);
    int st;
    waitpid(pid, &st, 0);
    return unit;
}

static int refuse_service(const DebInfo *d, const char *unit) {
    if (!unit[0]) return 0;
    fprintf(stderr,
        "  %s✗ %s installs a system service (%s).%s\n"
        "  Programs from .deb files run without services on SnapOS, so it would not work.\n"
        "  Look for it in the SnapOS configuration instead: search.nixos.org, then\n"
        "  add it with snapos config.\n", RED, d->pkg, unit, RST);
    return 1;
}

static int arch_ok(const DebInfo *d) {
    return !d->arch[0] || !strcmp(d->arch, host_arch()) || !strcmp(d->arch, "all");
}

/* 0 clean, 1 infected (and contained), 2 could not scan */
static int scan(const char *deb) {
    char out[2048];
    char *argv[] = { "snapguard", "scan", (char *)deb, NULL };
    int rc = capture(argv, out, sizeof out);
    if (rc == 0) {
        printf("  %s✓ SnapGuard: clean%s\n", GRN, RST);
        return 0;
    }
    if (rc == 1) {
        printf("  %s✗ SnapGuard found a threat in this file%s\n", RED, RST);
        char *q[] = { "snapguard", "quarantine", (char *)deb, NULL };
        run(q);
        return 1;
    }
    printf("  %s? SnapGuard could not scan this file%s\n", YEL, RST);
    char *nl = strrchr(out, '\n');
    if (nl && nl[1] == 0) *nl = 0;
    char *last = strrchr(out, '\n');
    printf("  %s%s%s\n", DIM, last ? last + 1 : out, RST);
    return 2;
}

static void remove_older(const char *dir, const DebInfo *d, const char *keep) {
    char p[128];
    clean(p, sizeof p, d->pkg);
    char prefix[160];
    pathf(prefix, sizeof prefix, "%s_", p);
    DIR *dh = opendir(dir);
    if (!dh) return;
    struct dirent *e;
    while ((e = readdir(dh))) {
        if (strncmp(e->d_name, prefix, strlen(prefix)) != 0 || strcmp(e->d_name, keep) == 0) continue;
        char path[PATH_MAX + 300];
        pathf(path, sizeof path, "%s/%s", dir, e->d_name);
        char *rm[] = { "rm", "-f", path, NULL };
        as_owner(rm);
    }
    closedir(dh);
}

/* Declares the package: the file goes to <nixdir>/debs, and the layer installs
 * every file there on the next sync. */
static int declare(const char *deb, const DebInfo *d) {
    char dir[PATH_MAX], name[400], target[PATH_MAX + 400];
    debs_dir(dir, sizeof dir);
    target_name(d, name, sizeof name);
    pathf(target, sizeof target, "%s/%s", dir, name);
    char *mk[] = { "install", "-d", dir, NULL };
    if (as_owner(mk) != 0) { fprintf(stderr, "snap-deb: could not create %s\n", dir); return 1; }
    char *cp[] = { "install", "-m", "644", (char *)deb, target, NULL };
    if (as_owner(cp) != 0) { fprintf(stderr, "snap-deb: could not copy the package to %s\n", dir); return 1; }
    remove_older(dir, d, name);
    printf("  %s✓ Declared:%s %s\n", GRN, RST, target);
    return 0;
}

static int check_file(const char *deb, DebInfo *d) {
    if (!read_info(deb, d)) {
        fprintf(stderr, "snap-deb: %s is not a readable .deb package\n", deb);
        return 0;
    }
    return 1;
}

/* ---- the layer --------------------------------------------------------- */

static void rootfs(char *dst, size_t n)  { pathf(dst, n, "%s/rootfs", layer()); }
static void exports(char *dst, size_t n) { pathf(dst, n, "%s/exports", layer()); }
static void statefile(char *dst, size_t n) { pathf(dst, n, "%s/installed", layer()); }

static int layer_ready(void) {
    char p[PATH_MAX];
    pathf(p, sizeof p, "%s/rootfs/etc/debian_version", layer());
    return exists(p);
}

static int read_state(Entry *e, int max) {
    char p[PATH_MAX];
    statefile(p, sizeof p);
    FILE *f = fopen(p, "r");
    if (!f) return 0;
    int n = 0;
    char line[512];
    while (n < max && fgets(line, sizeof line, f)) {
        trim(line);
        if (!line[0]) continue;
        char *sp = strchr(line, ' ');
        if (!sp) continue;
        *sp = 0;
        snprintf(e[n].pkg, sizeof e[n].pkg, "%.127s", line);
        snprintf(e[n].file, sizeof e[n].file, "%.255s", sp + 1);
        n++;
    }
    fclose(f);
    return n;
}

static int write_state(const Entry *e, int n) {
    char p[PATH_MAX];
    statefile(p, sizeof p);
    FILE *f = fopen(p, "w");
    if (!f) return 0;
    for (int i = 0; i < n; i++) fprintf(f, "%s %s\n", e[i].pkg, e[i].file);
    fclose(f);
    return 1;
}

/* dpkg keeps the file list of a package in /var/lib/dpkg/info; multi-arch
 * packages carry the architecture in the name. */
static int package_list(const char *pkg, char *dst, size_t n) {
    char r[PATH_MAX];
    rootfs(r, sizeof r);
    pathf(dst, n, "%s/var/lib/dpkg/info/%s.list", r, pkg);
    if (exists(dst)) return 1;
    pathf(dst, n, "%s/var/lib/dpkg/info/%s:%s.list", r, pkg, host_arch());
    return exists(dst);
}

static int installed(const char *pkg) {
    char p[PATH_MAX];
    return package_list(pkg, p, sizeof p);
}

static int write_file(const char *path, const char *text, int mode) {
    FILE *f = fopen(path, "w");
    if (!f) return 0;
    fputs(text, f);
    fclose(f);
    chmod(path, (mode_t)mode);
    return 1;
}

/* Settings for a Debian that lives inside another system: no services start
 * on install, manuals are skipped, and the archive has security updates. */
static int configure_layer(void) {
    char r[PATH_MAX], p[PATH_MAX + 64], text[1024];
    rootfs(r, sizeof r);

    pathf(p, sizeof p, "%s/usr/sbin/policy-rc.d", r);
    if (!write_file(p, "#!/bin/sh\nexit 101\n", 0755)) return 0;

    /* Packages that ship a service call systemctl from their install scripts.
     * There is no systemd in the layer, so those calls succeed and do nothing:
     * the program installs, but its service never runs here. */
    pathf(p, sizeof p, "%s/usr/local/bin", r);
    mkdir_p(p);
    static const char *shims[] = { "systemctl", "service", NULL };
    for (int i = 0; shims[i]; i++) {
        pathf(p, sizeof p, "%s/usr/local/bin/%s", r, shims[i]);
        if (!write_file(p, "#!/bin/sh\n# SnapOS: services do not run inside the Debian layer.\nexit 0\n", 0755)) return 0;
    }

    pathf(p, sizeof p, "%s/etc/dpkg/dpkg.cfg.d", r);
    mkdir(p, 0755);
    pathf(p, sizeof p, "%s/etc/dpkg/dpkg.cfg.d/snapos", r);
    if (!write_file(p, "path-exclude=/usr/share/doc/*\npath-exclude=/usr/share/man/*\npath-exclude=/usr/share/info/*\n", 0644)) return 0;

    pathf(p, sizeof p, "%s/etc/apt/apt.conf.d/90snapos", r);
    if (!write_file(p, "Acquire::Languages \"none\";\nAPT::Install-Recommends \"true\";\n", 0644)) return 0;

    pathf(text, sizeof text,
          "deb %s %s main contrib non-free non-free-firmware\n"
          "deb %s %s-updates main contrib non-free non-free-firmware\n"
          "deb http://security.debian.org/debian-security %s-security main contrib non-free non-free-firmware\n",
          mirror(), suite(), mirror(), suite(), suite());
    pathf(p, sizeof p, "%s/etc/apt/sources.list", r);
    if (!write_file(p, text, 0644)) return 0;

    pathf(p, sizeof p, "%s/etc/hostname", r);
    write_file(p, "snapos\n", 0644);

    /* Mount points for what `snap-deb run` brings in from SnapOS. */
    static const char *files[] = { "/etc/hosts", "/etc/machine-id", "/etc/localtime", NULL };
    for (int i = 0; files[i]; i++) {
        pathf(p, sizeof p, "%s%s", r, files[i]);
        struct stat st;
        if (lstat(p, &st) == 0 && S_ISREG(st.st_mode)) continue;
        unlink(p);
        write_file(p, "", 0644);
    }
    static const char *dirs[] = { "/nix/store", "/usr/local/share/fonts", "/usr/local/share/themes",
                                  "/usr/local/share/icons", "/media", "/mnt", "/var/tmp", NULL };
    for (int i = 0; dirs[i]; i++) {
        pathf(p, sizeof p, "%s%s", r, dirs[i]);
        mkdir_p(p);
    }
    return 1;
}

static int bootstrap(void) {
    char r[PATH_MAX], tmp[PATH_MAX + 8];
    rootfs(r, sizeof r);
    pathf(tmp, sizeof tmp, "%s.new", r);
    printf("\n  %s%sCreating the Debian layer%s (%s, from %s)\n", BLD, RED, RST, suite(), mirror());
    printf("  %sThis happens once and downloads about 300 MB.%s\n\n", DIM, RST);
    if (!exists(keyring())) {
        fprintf(stderr, "snap-deb: %s is missing; the Debian archive cannot be verified\n", keyring());
        return 0;
    }
    char keyopt[PATH_MAX + 16];
    pathf(keyopt, sizeof keyopt, "--keyring=%s", keyring());
    char *rm[] = { "rm", "-rf", tmp, NULL };
    run(rm);
    char *mk[] = { "mkdir", "-p", tmp, NULL };
    if (run(mk) != 0) return 0;
    char archopt[64];
    snprintf(archopt, sizeof archopt, "--arch=%s", host_arch());
    char *argv[] = { "debootstrap", "--variant=minbase", archopt, keyopt, (char *)suite(), tmp, (char *)mirror(), NULL };
    if (run(argv) != 0) {
        char log[PATH_MAX + 40];
        pathf(log, sizeof log, "%s/debootstrap/debootstrap.log", tmp);
        fprintf(stderr, "\nsnap-deb: debootstrap failed; is the network up? The end of its log:\n");
        char *tail[] = { "tail", "-n", "15", log, NULL };
        run(tail);
        run(rm);
        return 0;
    }
    if (rename(tmp, r) != 0) { perror("snap-deb: could not move the new layer into place"); return 0; }
    return configure_layer();
}

/* Runs a command as root inside the layer (for apt and dpkg). */
static int in_layer_root(char *const cmd[]) {
    char r[PATH_MAX], debs[PATH_MAX];
    rootfs(r, sizeof r);
    debs_dir(debs, sizeof debs);
    char *argv[96];
    int k = 0;
    argv[k++] = (char *)bwrap();
    argv[k++] = "--bind"; argv[k++] = r; argv[k++] = "/";
    argv[k++] = "--dev"; argv[k++] = "/dev";
    argv[k++] = "--proc"; argv[k++] = "/proc";
    /* apt verifies signatures as its own user, which needs a writable /tmp. */
    argv[k++] = "--perms"; argv[k++] = "1777"; argv[k++] = "--tmpfs"; argv[k++] = "/tmp";
    argv[k++] = "--tmpfs"; argv[k++] = "/run";
    argv[k++] = "--ro-bind-try"; argv[k++] = "/etc/resolv.conf"; argv[k++] = "/etc/resolv.conf";
    if (is_dir(debs)) { argv[k++] = "--ro-bind"; argv[k++] = debs; argv[k++] = DEBS_MOUNT; }
    /* dpkg changes owners, modes and file capabilities, and apt drops to its
     * own user: those powers are granted, and nothing else. No SYS_ADMIN and
     * no MKNOD, so an install script cannot mount, make device nodes or enter
     * the host's namespaces; its own pid, ipc and uts namespaces on top. */
    static const char *caps[] = { "CAP_CHOWN", "CAP_DAC_OVERRIDE", "CAP_DAC_READ_SEARCH", "CAP_FOWNER",
                                  "CAP_FSETID", "CAP_SETUID", "CAP_SETGID", "CAP_SETFCAP",
                                  "CAP_SYS_CHROOT", "CAP_KILL", NULL };
    for (int i = 0; caps[i]; i++) { argv[k++] = "--cap-add"; argv[k++] = (char *)caps[i]; }
    argv[k++] = "--unshare-pid"; argv[k++] = "--unshare-ipc"; argv[k++] = "--unshare-uts";
    argv[k++] = "--clearenv";
    argv[k++] = "--setenv"; argv[k++] = "PATH"; argv[k++] = "/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin";
    argv[k++] = "--setenv"; argv[k++] = "HOME"; argv[k++] = "/root";
    argv[k++] = "--setenv"; argv[k++] = "LANG"; argv[k++] = "C.UTF-8";
    argv[k++] = "--setenv"; argv[k++] = "DEBIAN_FRONTEND"; argv[k++] = "noninteractive";
    argv[k++] = "--setenv"; argv[k++] = "APT_LISTCHANGES_FRONTEND"; argv[k++] = "none";
    argv[k++] = "--die-with-parent";
    argv[k++] = "--";
    for (int i = 0; cmd[i] && k < 94; i++) argv[k++] = cmd[i];
    argv[k] = NULL;
    return run(argv);
}

/* The base set is installed once; the marker says it is there. */
static int ensure_base(void) {
    char marker[PATH_MAX];
    pathf(marker, sizeof marker, "%s/base-ok", layer());
    if (exists(marker)) return 1;
    printf("  %sInstalling the base of the Debian layer (graphics, fonts, settings)...%s\n", DIM, RST);
    char *upd[] = { "apt-get", "-q", "update", NULL };
    if (in_layer_root(upd) != 0) return 0;
    char *argv[32];
    int k = 0;
    argv[k++] = "apt-get"; argv[k++] = "-q"; argv[k++] = "install"; argv[k++] = "-y";
    for (int i = 0; BASE_PACKAGES[i] && k < 30; i++) argv[k++] = BASE_PACKAGES[i];
    argv[k] = NULL;
    if (in_layer_root(argv) != 0) return 0;
    return write_file(marker, "", 0644);
}

/* Lists the declared .deb files, sorted. */
static int declared(char names[][256], int max) {
    char dir[PATH_MAX];
    debs_dir(dir, sizeof dir);
    DIR *dh = opendir(dir);
    if (!dh) return 0;
    int n = 0;
    struct dirent *e;
    while (n < max && (e = readdir(dh))) {
        size_t len = strlen(e->d_name);
        if (len > 4 && !strcmp(e->d_name + len - 4, ".deb")) snprintf(names[n++], 256, "%s", e->d_name);
    }
    closedir(dh);
    for (int i = 1; i < n; i++)
        for (int j = i; j > 0 && strcmp(names[j - 1], names[j]) > 0; j--) {
            char t[256];
            memcpy(t, names[j], 256); memcpy(names[j], names[j - 1], 256); memcpy(names[j - 1], t, 256);
        }
    return n;
}

static int lists_fresh(void) {
    char p[PATH_MAX];
    pathf(p, sizeof p, "%s/rootfs/var/lib/apt/lists", layer());
    struct stat st;
    if (stat(p, &st) != 0) return 0;
    return time(NULL) - st.st_mtime < 6 * 3600;
}

/* ---- exports: menu entries, icons and commands ------------------------- */

static void export_desktop(const char *pkg, const char *src, const char *dst) {
    FILE *in = fopen(src, "r");
    if (!in) return;
    FILE *out = fopen(dst, "w");
    if (!out) { fclose(in); return; }
    char r[PATH_MAX], line[4096];
    rootfs(r, sizeof r);
    while (fgets(line, sizeof line, in)) {
        if (!strncmp(line, "TryExec=", 8) || !strncmp(line, "DBusActivatable=", 16)) continue;
        if (!strncmp(line, "Exec=", 5)) {
            fprintf(out, "Exec=snap-deb run %s", line + 5);
        } else if (!strncmp(line, "Icon=/", 6)) {
            fprintf(out, "Icon=%s%s", r, line + 5);
        } else {
            fputs(line, out);
            if (!strncmp(line, "[Desktop Entry]", 15)) fprintf(out, "X-SnapOS-Package=%s\n", pkg);
        }
    }
    fclose(in);
    fclose(out);
    chmod(dst, 0644);
}

static void mkdir_p(const char *path) {
    char tmp[PATH_MAX];
    snprintf(tmp, sizeof tmp, "%s", path);
    for (char *p = tmp + 1; *p; p++) {
        if (*p != '/') continue;
        *p = 0;
        mkdir(tmp, 0755);
        *p = '/';
    }
    mkdir(tmp, 0755);
}

static void export_package(const char *pkg) {
    char list[PATH_MAX], r[PATH_MAX], ex[PATH_MAX];
    if (!package_list(pkg, list, sizeof list)) return;
    rootfs(r, sizeof r);
    exports(ex, sizeof ex);
    FILE *f = fopen(list, "r");
    if (!f) return;
    char line[PATH_MAX];
    while (fgets(line, sizeof line, f)) {
        trim(line);
        char src[PATH_MAX * 2], dst[PATH_MAX * 2];
        pathf(src, sizeof src, "%s%s", r, line);
        struct stat st;
        if (stat(src, &st) != 0 || S_ISDIR(st.st_mode)) continue;
        const char *base = strrchr(line, '/');
        base = base ? base + 1 : line;
        size_t len = strlen(line);

        if (!strncmp(line, "/usr/share/applications/", 24) && len > 8 && !strcmp(line + len - 8, ".desktop")) {
            pathf(dst, sizeof dst, "%s/share/applications/%s", ex, base);
            export_desktop(pkg, src, dst);
        } else if (!strncmp(line, "/usr/share/icons/", 17) || !strncmp(line, "/usr/share/pixmaps/", 19)) {
            pathf(dst, sizeof dst, "%s/share%s", ex, line + 10);
            char *slash = strrchr(dst, '/');
            *slash = 0;
            mkdir_p(dst);
            *slash = '/';
            if (symlink(src, dst) != 0) continue;
        } else if ((!strncmp(line, "/usr/bin/", 9) || !strncmp(line, "/usr/games/", 11)) && (st.st_mode & S_IXUSR)) {
            pathf(dst, sizeof dst, "%s/bin/%s", ex, base);
            char text[600];
            pathf(text, sizeof text, "#!/bin/sh\nexec snap-deb run %s \"$@\"\n", base);
            write_file(dst, text, 0755);
        }
    }
    fclose(f);
}

static int do_export(void) {
    char ex[PATH_MAX], p[PATH_MAX + 32];
    exports(ex, sizeof ex);
    char *rm[] = { "rm", "-rf", ex, NULL };
    run(rm);
    pathf(p, sizeof p, "%s/share/applications", ex); mkdir_p(p);
    pathf(p, sizeof p, "%s/share/icons", ex); mkdir_p(p);
    pathf(p, sizeof p, "%s/bin", ex); mkdir_p(p);
    Entry e[MAX_PKGS];
    int n = read_state(e, MAX_PKGS);
    for (int i = 0; i < n; i++) export_package(e[i].pkg);
    return 0;
}

/* ---- sync: make the layer match the declared list ---------------------- */

static int needs_root(void) {
    return geteuid() != 0 && access(layer(), W_OK) != 0;
}

static void reexec_with_sudo(int argc, char **argv) {
    char self[PATH_MAX];
    ssize_t n = readlink("/proc/self/exe", self, sizeof self - 1);
    if (n <= 0) return;
    self[n] = 0;
    char **nargv = calloc((size_t)argc + 8, sizeof *nargv);
    if (!nargv) return;
    int k = 0;
    nargv[k++] = "sudo";
    nargv[k++] = "--preserve-env=SNAPOS_NIX_DIR,SNAPDEB_DIR,SNAPDEB_SUITE,SNAPDEB_MIRROR,SNAPDEB_ARCH";
    nargv[k++] = self;
    for (int i = 1; i < argc; i++) nargv[k++] = argv[i];
    fprintf(stderr, "snap-deb: needs root, asking sudo...\n");
    execvp("sudo", nargv);
    perror("snap-deb: could not run sudo");
    free(nargv);
}

static int cmd_sync(void) {
    char names[MAX_PKGS][256];
    int nd = declared(names, MAX_PKGS);
    char *mk[] = { "mkdir", "-p", (char *)layer(), NULL };
    if (run(mk) != 0) { fprintf(stderr, "snap-deb: could not create %s\n", layer()); return 1; }
    chmod(layer(), 0755);

    if (!layer_ready()) {
        if (nd == 0) { printf("  %sno .deb packages declared; the Debian layer is not needed yet%s\n", DIM, RST); return 0; }
        if (!bootstrap()) return 1;
    }
    if (!configure_layer()) { fprintf(stderr, "snap-deb: could not write the layer's settings\n"); return 1; }
    if (!ensure_base()) { fprintf(stderr, "snap-deb: the base of the Debian layer could not be installed; is the network up?\n"); return 1; }

    Entry state[MAX_PKGS];
    int ns = read_state(state, MAX_PKGS);

    /* What is declared but not installed from that exact file. */
    char *install[MAX_PKGS];
    Entry fresh[MAX_PKGS];
    int ni = 0, rc_skip = 0;
    for (int i = 0; i < nd; i++) {
        int have = 0;
        for (int j = 0; j < ns; j++)
            if (!strcmp(state[j].file, names[i]) && installed(state[j].pkg)) have = 1;
        if (have) continue;
        char path[PATH_MAX + 300];
        char dir[PATH_MAX];
        debs_dir(dir, sizeof dir);
        pathf(path, sizeof path, "%s/%s", dir, names[i]);
        DebInfo d;
        if (!read_info(path, &d)) { fprintf(stderr, "snap-deb: skipping %s: not a .deb\n", names[i]); continue; }
        if (refuse_service(&d, service_in(path))) {
            fprintf(stderr, "  Remove it with: snap-deb remove %s\n", d.pkg);
            rc_skip = 1;
            continue;
        }
        snprintf(fresh[ni].pkg, sizeof fresh[ni].pkg, "%s", d.pkg);
        snprintf(fresh[ni].file, sizeof fresh[ni].file, "%.255s", names[i]);
        install[ni] = fresh[ni].file;
        ni++;
    }

    /* What is installed but no longer declared. */
    char *purge[MAX_PKGS];
    int np = 0;
    for (int j = 0; j < ns; j++) {
        int still = 0;
        for (int i = 0; i < nd; i++) if (!strcmp(state[j].file, names[i])) still = 1;
        for (int i = 0; i < ni; i++) if (!strcmp(fresh[i].pkg, state[j].pkg)) still = 1;
        if (!still && installed(state[j].pkg)) purge[np++] = state[j].pkg;
    }

    int rc = rc_skip;
    if (ni > 0) {
        if (!lists_fresh()) {
            printf("  %sUpdating the Debian package lists...%s\n", DIM, RST);
            char *upd[] = { "apt-get", "-q", "update", NULL };
            if (in_layer_root(upd) != 0) { fprintf(stderr, "snap-deb: apt-get update failed; is the network up?\n"); return 1; }
        }
        for (int i = 0; i < ni; i++) {
            printf("\n  %s%sInstalling%s %s\n", BLD, RED, RST, install[i]);
            char path[400];
            pathf(path, sizeof path, DEBS_MOUNT "/%s", install[i]);
            char *ins[] = { "apt-get", "-q", "install", "-y", path, NULL };
            if (in_layer_root(ins) != 0) {
                fprintf(stderr, "  %s✗ %s could not be installed%s (the reason is in the apt output above)\n", RED, install[i], RST);
                rc = 1;
            }
        }
    }
    if (np > 0) {
        char *argv[MAX_PKGS + 8];
        int k = 0;
        argv[k++] = "apt-get"; argv[k++] = "-q"; argv[k++] = "purge"; argv[k++] = "-y";
        for (int i = 0; i < np; i++) { printf("  %sRemoving%s %s\n", DIM, RST, purge[i]); argv[k++] = purge[i]; }
        argv[k] = NULL;
        if (in_layer_root(argv) != 0) rc = 1;
    }
    if (ni > 0 || np > 0) {
        char *auto_rm[] = { "apt-get", "-q", "autoremove", "--purge", "-y", NULL };
        in_layer_root(auto_rm);
        char *cl[] = { "apt-get", "clean", NULL };
        in_layer_root(cl);
    }

    /* The state lists what is really installed from which file. */
    Entry next[MAX_PKGS];
    int nn = 0;
    for (int j = 0; j < ns && nn < MAX_PKGS; j++) {
        int replaced = 0;
        for (int i = 0; i < ni; i++) if (!strcmp(fresh[i].pkg, state[j].pkg)) replaced = 1;
        int gone = 0;
        for (int i = 0; i < np; i++) if (!strcmp(purge[i], state[j].pkg)) gone = 1;
        if (!replaced && !gone && installed(state[j].pkg)) next[nn++] = state[j];
    }
    for (int i = 0; i < ni && nn < MAX_PKGS; i++)
        if (installed(fresh[i].pkg)) next[nn++] = fresh[i];
    write_state(next, nn);
    do_export();

    if (rc == 0) printf("\n  %s✓ Debian layer: %d package%s installed%s\n", GRN, nn, nn == 1 ? "" : "s", RST);
    else printf("\n  %s✗ Debian layer: %d package%s installed, but not everything could be applied%s\n", RED, nn, nn == 1 ? "" : "s", RST);
    return rc;
}

/* ---- run: start a program inside the layer ----------------------------- */

static void setenv_arg(char **argv, int *k, const char *name, const char *value) {
    if (!value) return;
    argv[(*k)++] = "--setenv";
    argv[(*k)++] = (char *)name;
    argv[(*k)++] = (char *)value;
}

static int cmd_run(int argc, char **argv) {
    if (!layer_ready()) {
        fprintf(stderr, "snap-deb: the Debian layer does not exist yet; add a .deb first\n");
        return 2;
    }
    char r[PATH_MAX], runtime[PATH_MAX], cwd[PATH_MAX], localtime_path[PATH_MAX], parent[PATH_MAX];
    rootfs(r, sizeof r);
    const char *home = getenv("HOME");
    if (!home || !*home || home[0] != '/') home = "/tmp";
    /* The layer's root is read-only, so the home's parent becomes a tmpfs
     * where the mount point can be made. */
    snprintf(parent, sizeof parent, "%s", home);
    char *slash = strrchr(parent, '/');
    if (slash && slash != parent) *slash = 0; else snprintf(parent, sizeof parent, "/tmp");
    const char *xdg = getenv("XDG_RUNTIME_DIR");
    runtime[0] = 0;
    if (xdg && is_dir(xdg)) snprintf(runtime, sizeof runtime, "%s", xdg);
    if (!getcwd(cwd, sizeof cwd) || strncmp(cwd, home, strlen(home)) != 0) snprintf(cwd, sizeof cwd, "%s", home);
    if (!realpath("/etc/localtime", localtime_path)) localtime_path[0] = 0;

    char *a[160];
    int k = 0;
    a[k++] = (char *)bwrap();
    a[k++] = "--ro-bind"; a[k++] = r; a[k++] = "/";
    a[k++] = "--dev-bind"; a[k++] = "/dev"; a[k++] = "/dev";
    a[k++] = "--proc"; a[k++] = "/proc";
    a[k++] = "--ro-bind"; a[k++] = "/sys"; a[k++] = "/sys";
    a[k++] = "--perms"; a[k++] = "1777"; a[k++] = "--tmpfs"; a[k++] = "/tmp";
    a[k++] = "--ro-bind-try"; a[k++] = "/tmp/.X11-unix"; a[k++] = "/tmp/.X11-unix";
    a[k++] = "--tmpfs"; a[k++] = "/run";
    a[k++] = "--tmpfs"; a[k++] = "/var/tmp";
    if (runtime[0]) { a[k++] = "--bind"; a[k++] = runtime; a[k++] = runtime; }
    a[k++] = "--ro-bind-try"; a[k++] = "/run/dbus/system_bus_socket"; a[k++] = "/run/dbus/system_bus_socket";
    a[k++] = "--bind-try"; a[k++] = "/run/media"; a[k++] = "/run/media";
    a[k++] = "--bind-try"; a[k++] = "/media"; a[k++] = "/media";
    a[k++] = "--bind-try"; a[k++] = "/mnt"; a[k++] = "/mnt";
    if (strcmp(parent, "/tmp") != 0) { a[k++] = "--tmpfs"; a[k++] = parent; }
    a[k++] = "--bind"; a[k++] = (char *)home; a[k++] = (char *)home;
    static const char *etc[] = { "/etc/resolv.conf", "/etc/hosts", "/etc/passwd", "/etc/group", "/etc/machine-id", NULL };
    for (int i = 0; etc[i]; i++) {
        char inside[PATH_MAX + 32];
        pathf(inside, sizeof inside, "%s%s", r, etc[i]);
        if (!exists(inside)) continue;
        a[k++] = "--ro-bind-try"; a[k++] = (char *)etc[i]; a[k++] = (char *)etc[i];
    }
    if (localtime_path[0]) { a[k++] = "--ro-bind-try"; a[k++] = localtime_path; a[k++] = "/etc/localtime"; }
    /* SnapOS fonts, themes and icons show up where Debian programs look. */
    char nixstore[PATH_MAX + 16];
    pathf(nixstore, sizeof nixstore, "%s/nix/store", r);
    if (is_dir(nixstore)) {
        a[k++] = "--ro-bind-try"; a[k++] = "/nix/store"; a[k++] = "/nix/store";
        a[k++] = "--ro-bind-try"; a[k++] = "/run/current-system/sw/share/X11/fonts"; a[k++] = "/usr/local/share/fonts";
        a[k++] = "--ro-bind-try"; a[k++] = "/run/current-system/sw/share/themes"; a[k++] = "/usr/local/share/themes";
        a[k++] = "--ro-bind-try"; a[k++] = "/run/current-system/sw/share/icons"; a[k++] = "/usr/local/share/icons";
    }
    const char *xauth = getenv("XAUTHORITY");
    if (xauth && exists(xauth)) { a[k++] = "--ro-bind-try"; a[k++] = (char *)xauth; a[k++] = (char *)xauth; }
    a[k++] = "--chdir"; a[k++] = cwd;
    a[k++] = "--die-with-parent";
    a[k++] = "--clearenv";
    setenv_arg(a, &k, "HOME", home);
    setenv_arg(a, &k, "USER", getenv("USER"));
    setenv_arg(a, &k, "LOGNAME", getenv("LOGNAME"));
    setenv_arg(a, &k, "SHELL", "/bin/bash");
    setenv_arg(a, &k, "PATH", "/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin:/usr/games");
    setenv_arg(a, &k, "XDG_DATA_DIRS", "/usr/local/share:/usr/share");
    setenv_arg(a, &k, "LANG", "C.UTF-8");
    setenv_arg(a, &k, "TERM", getenv("TERM"));
    setenv_arg(a, &k, "COLORTERM", getenv("COLORTERM"));
    setenv_arg(a, &k, "DISPLAY", getenv("DISPLAY"));
    setenv_arg(a, &k, "WAYLAND_DISPLAY", getenv("WAYLAND_DISPLAY"));
    setenv_arg(a, &k, "XAUTHORITY", xauth && exists(xauth) ? xauth : NULL);
    setenv_arg(a, &k, "XDG_RUNTIME_DIR", runtime[0] ? runtime : NULL);
    setenv_arg(a, &k, "XDG_SESSION_TYPE", getenv("XDG_SESSION_TYPE"));
    setenv_arg(a, &k, "XDG_CURRENT_DESKTOP", getenv("XDG_CURRENT_DESKTOP"));
    setenv_arg(a, &k, "DESKTOP_SESSION", getenv("DESKTOP_SESSION"));
    setenv_arg(a, &k, "DBUS_SESSION_BUS_ADDRESS", getenv("DBUS_SESSION_BUS_ADDRESS"));
    setenv_arg(a, &k, "PULSE_SERVER", getenv("PULSE_SERVER"));
    setenv_arg(a, &k, "GTK_THEME", getenv("GTK_THEME"));
    setenv_arg(a, &k, "NO_AT_BRIDGE", "1");
    setenv_arg(a, &k, "SNAPDEB", "1");
    a[k++] = "--";
    for (int i = 0; i < argc && k < 158; i++) a[k++] = argv[i];
    a[k] = NULL;
    execvp(a[0], a);
    perror("snap-deb: could not start bwrap");
    return 127;
}

/* ---- the small commands ------------------------------------------------ */

/* Debian's security updates for the layer. Run by `snapos update`. */
static int cmd_upgrade(void) {
    if (!layer_ready()) { printf("  %sno Debian layer yet%s\n", DIM, RST); return 0; }
    if (!configure_layer()) return 1;
    printf("  %sUpdating the Debian layer...%s\n", DIM, RST);
    char *upd[] = { "apt-get", "-q", "update", NULL };
    if (in_layer_root(upd) != 0) { fprintf(stderr, "snap-deb: apt-get update failed; is the network up?\n"); return 1; }
    char *up[] = { "apt-get", "-q", "upgrade", "-y", NULL };
    if (in_layer_root(up) != 0) return 1;
    char *cl[] = { "apt-get", "clean", NULL };
    in_layer_root(cl);
    do_export();
    printf("  %s✓ Debian layer up to date%s\n", GRN, RST);
    return 0;
}

static int cmd_status(void) {
    char p[PATH_MAX], ver[64] = "";
    pathf(p, sizeof p, "%s/rootfs/etc/debian_version", layer());
    FILE *f = fopen(p, "r");
    if (f) { if (fgets(ver, sizeof ver, f)) trim(ver); fclose(f); }
    printf("  %sDebian layer%s   %s\n", DIM, RST, layer());
    if (ver[0]) printf("  %sDebian%s         %s (%s)\n", DIM, RST, ver, suite());
    else printf("  %sDebian%s         %snot created yet (it appears with the first .deb)%s\n", DIM, RST, YEL, RST);
    Entry e[MAX_PKGS];
    int n = read_state(e, MAX_PKGS);
    printf("  %sInstalled%s      %d package%s\n", DIM, RST, n, n == 1 ? "" : "s");
    char names[MAX_PKGS][256];
    int nd = declared(names, MAX_PKGS);
    printf("  %sDeclared%s       %d file%s in %s/debs\n", DIM, RST, nd, nd == 1 ? "" : "s", nixdir());
    char out[512];
    char *w1[] = { "sh", "-c", "command -v bwrap", NULL };
    char *w2[] = { "sh", "-c", "command -v debootstrap", NULL };
    printf("  %sbubblewrap%s     %s\n", DIM, RST, capture(w1, out, sizeof out) == 0 ? "ok" : RED "missing" RST);
    printf("  %sdebootstrap%s    %s\n", DIM, RST, capture(w2, out, sizeof out) == 0 ? "ok" : RED "missing" RST);
    return 0;
}

static int cmd_list(void) {
    char names[MAX_PKGS][256];
    int nd = declared(names, MAX_PKGS);
    Entry e[MAX_PKGS];
    int ns = read_state(e, MAX_PKGS);
    for (int i = 0; i < nd; i++) {
        int ok = 0;
        for (int j = 0; j < ns; j++) if (!strcmp(e[j].file, names[i]) && installed(e[j].pkg)) ok = 1;
        printf("  %s  %s\n", names[i], ok ? GRN "installed" RST : DIM "not applied yet (snapos rebuild)" RST);
    }
    if (!nd) printf("  %sno .deb packages declared%s\n", DIM, RST);
    return 0;
}

static int cmd_remove(const char *name) {
    char dir[PATH_MAX];
    debs_dir(dir, sizeof dir);
    /* The package name, or the file name as `snap-deb list` shows it. */
    char pkg[200];
    const char *base = strrchr(name, '/');
    snprintf(pkg, sizeof pkg, "%.199s", base ? base + 1 : name);
    char *us = strchr(pkg, '_');
    if (us) *us = 0;
    char prefix[200];
    clean(prefix, sizeof prefix, pkg);
    strncat(prefix, "_", sizeof prefix - strlen(prefix) - 1);
    DIR *dh = opendir(dir);
    int removed = 0;
    if (dh) {
        struct dirent *e;
        while ((e = readdir(dh))) {
            if (strncmp(e->d_name, prefix, strlen(prefix)) != 0) continue;
            char path[PATH_MAX + 300];
            pathf(path, sizeof path, "%s/%s", dir, e->d_name);
            char *rm[] = { "rm", "-f", path, NULL };
            if (as_owner(rm) == 0) { printf("  removed %s\n", e->d_name); removed++; }
        }
        closedir(dh);
    }
    if (!removed) { fprintf(stderr, "snap-deb: no declared package named %s\n", name); return 2; }
    printf("  %sApply it with: snapos rebuild%s\n", DIM, RST);
    return 0;
}

static int cmd_info(const char *deb) {
    DebInfo d;
    if (!check_file(deb, &d)) return 2;
    show_info(deb, &d);
    return 0;
}

static int cmd_add(const char *deb, int force) {
    DebInfo d;
    if (!check_file(deb, &d)) return 2;
    show_info(deb, &d);
    if (!arch_ok(&d)) {
        fprintf(stderr, "snap-deb: this package is for %s, not for this computer (%s)\n", d.arch, host_arch());
        return 2;
    }
    if (refuse_service(&d, service_in(deb))) return 2;
    int s = scan(deb);
    if (s == 1) return 1;
    if (s == 2 && !force) {
        fprintf(stderr, "snap-deb: not adding a file that could not be scanned (use --force to override)\n");
        return 2;
    }
    return declare(deb, &d);
}

static int rebuild(void) {
    char *argv[] = { "snapos", "rebuild", NULL };
    return run(argv);
}

static void wait_enter(void) {
    if (!is_tty()) return;
    printf("\n  %sPress Enter to close.%s", DIM, RST);
    fflush(stdout);
    char line[16];
    if (!fgets(line, sizeof line, stdin)) return;
}

static int interactive(const char *deb) {
    DebInfo d;
    printf("\n  %s%sSnapOS package opener%s\n", BLD, RED, RST);
    if (!check_file(deb, &d)) { wait_enter(); return 2; }
    show_info(deb, &d);
    if (!arch_ok(&d)) {
        printf("  %sThis package is for %s, not for this computer (%s).%s\n", RED, d.arch, host_arch(), RST);
        wait_enter();
        return 2;
    }
    if (refuse_service(&d, service_in(deb))) { wait_enter(); return 2; }
    int s = scan(deb);
    if (s == 1) { wait_enter(); return 1; }
    if (s == 2 && !ask("Continue without a scan?")) { wait_enter(); return 2; }

    for (;;) {
        printf("\n  %s1%s  Add to SnapOS and apply now\n", RED, RST);
        printf("  %s2%s  Add to SnapOS, apply later\n", RED, RST);
        printf("  %s3%s  Show what is inside\n", RED, RST);
        printf("  %sq%s  Cancel\n\n  > ", RED, RST);
        fflush(stdout);
        char line[16];
        if (!fgets(line, sizeof line, stdin)) return 2;
        if (line[0] == '1' || line[0] == '2') {
            if (declare(deb, &d) != 0) { wait_enter(); return 1; }
            if (line[0] == '1') {
                printf("\n");
                int rc = rebuild();
                printf(rc == 0 ? "\n  %s✓ %s is now part of SnapOS.%s\n" : "\n  %sThe rebuild failed. Remove the package with: snap-deb remove %s%s\n",
                       rc == 0 ? GRN : RED, d.pkg, RST);
            } else {
                printf("  %sApply it any time with: snapos rebuild%s\n", DIM, RST);
            }
            wait_enter();
            return 0;
        }
        if (line[0] == '3') {
            char *ls[] = { "dpkg-deb", "-c", (char *)deb, NULL };
            printf("\n");
            run(ls);
            continue;
        }
        if (line[0] == 'q' || line[0] == 'Q') return 0;
    }
}

static void usage(FILE *f) {
    fprintf(f,
        "snap-deb: Debian packages on SnapOS\n\n"
        "  snap-deb FILE.deb          scan it, then add it to SnapOS (asks first)\n"
        "  snap-deb add FILE.deb      scan it and declare it without asking [--force]\n"
        "  snap-deb info FILE.deb     show what it is\n"
        "  snap-deb list              declared packages and whether they are installed\n"
        "  snap-deb remove NAME       take one out again\n"
        "  snap-deb sync              install what is declared (snapos rebuild does this)\n"
        "  snap-deb upgrade           Debian's updates for the layer (snapos update does this)\n"
        "  snap-deb run CMD [ARGS]    start a program from the Debian layer\n"
        "  snap-deb shell             a shell inside the Debian layer\n"
        "  snap-deb status            the state of the Debian layer\n\n"
        "Declared packages live in %s/debs. They are installed, with the\n"
        "libraries they need from the Debian archive, in a Debian layer at\n"
        "%s, and their programs run from there with your home,\n"
        "display and sound. Menu entries and commands are exported automatically.\n",
        nixdir(), layer());
}

int main(int argc, char **argv) {
    if (argc < 2 || !strcmp(argv[1], "help") || !strcmp(argv[1], "-h") || !strcmp(argv[1], "--help")) {
        usage(argc < 2 ? stderr : stdout);
        return argc < 2 ? 2 : 0;
    }
    if (!strcmp(argv[1], "list")) return cmd_list();
    if (!strcmp(argv[1], "status")) return cmd_status();
    if (!strcmp(argv[1], "info") && argc > 2) return cmd_info(argv[2]);
    if (!strcmp(argv[1], "remove") && argc > 2) return cmd_remove(argv[2]);
    if (!strcmp(argv[1], "add") && argc > 2) {
        int force = argc > 3 && !strcmp(argv[3], "--force");
        return cmd_add(argv[2], force);
    }
    if (!strcmp(argv[1], "run") && argc > 2) return cmd_run(argc - 2, argv + 2);
    if (!strcmp(argv[1], "shell")) {
        char *sh[] = { "bash", "-l", NULL };
        return cmd_run(2, sh);
    }
    if (!strcmp(argv[1], "sync") || !strcmp(argv[1], "export") || !strcmp(argv[1], "upgrade")) {
        if (needs_root()) { reexec_with_sudo(argc, argv); return 1; }
        if (!strcmp(argv[1], "upgrade")) return cmd_upgrade();
        return !strcmp(argv[1], "sync") ? cmd_sync() : do_export();
    }
    if (!is_tty()) {
        DebInfo d;
        if (!check_file(argv[1], &d)) return 2;
        show_info(argv[1], &d);
        fprintf(stderr, "snap-deb: run it in a terminal to add this package, or use 'snap-deb add FILE'\n");
        return 2;
    }
    return interactive(argv[1]);
}
