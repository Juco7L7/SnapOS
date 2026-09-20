#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <dirent.h>
#include <limits.h>
#include <ctype.h>
#include <sys/stat.h>
#include <sys/wait.h>

#define RED "\033[91m"
#define GRN "\033[32m"
#define YEL "\033[33m"
#define DIM "\033[2m"
#define BLD "\033[1m"
#define RST "\033[0m"

typedef struct {
    char pkg[128], ver[128], arch[32], desc[256], depends[1024];
} DebInfo;

static const char *nixdir(void) {
    const char *d = getenv("SNAPOS_NIX_DIR");
    if (d) return d;
    if (access("/etc/nixos/configuration.nix", F_OK) == 0) return "/etc/nixos";
    return "nix";
}

static int run(char *const argv[]) {
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
    snprintf(dst, n, "%s", out);
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

/* Keeps only characters that are safe in a file name and a Nix store name. */
static void clean(char *dst, size_t n, const char *src) {
    size_t j = 0;
    for (size_t i = 0; src[i] && j + 1 < n; i++) {
        unsigned char c = (unsigned char)src[i];
        dst[j++] = (isalnum(c) || c == '.' || c == '+' || c == '-') ? (char)c : '-';
    }
    dst[j] = 0;
    if (dst[0] == '.') dst[0] = '-';
}

static void debs_dir(char *dst, size_t n) { snprintf(dst, n, "%s/debs", nixdir()); }

static void target_name(const DebInfo *d, char *dst, size_t n) {
    char p[128], v[128], a[32];
    clean(p, sizeof p, d->pkg);
    clean(v, sizeof v, d->ver[0] ? d->ver : "0");
    clean(a, sizeof a, d->arch[0] ? d->arch : "amd64");
    snprintf(dst, n, "%s_%s_%s.deb", p, v, a);
}

/* Runs a command as root when the SnapOS directory is not writable. */
static int as_owner(char *const argv[]) {
    if (geteuid() == 0 || access(nixdir(), W_OK) == 0) return run(argv);
    char *full[32];
    int k = 0;
    full[k++] = "sudo";
    for (int i = 0; argv[i] && k < 30; i++) full[k++] = argv[i];
    full[k] = NULL;
    return run(full);
}

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

static int arch_ok(const DebInfo *d) {
    return !d->arch[0] || !strcmp(d->arch, "amd64") || !strcmp(d->arch, "all");
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
    snprintf(prefix, sizeof prefix, "%s_", p);
    DIR *dh = opendir(dir);
    if (!dh) return;
    struct dirent *e;
    while ((e = readdir(dh))) {
        if (strncmp(e->d_name, prefix, strlen(prefix)) != 0 || strcmp(e->d_name, keep) == 0) continue;
        char path[PATH_MAX + 300];
        snprintf(path, sizeof path, "%s/%s", dir, e->d_name);
        char *rm[] = { "rm", "-f", path, NULL };
        as_owner(rm);
    }
    closedir(dh);
}

/* Declares the package: the file goes to <nixdir>/debs, and the system builds
 * every file there on the next rebuild. */
static int declare(const char *deb, const DebInfo *d) {
    char dir[PATH_MAX], name[400], target[PATH_MAX + 400];
    debs_dir(dir, sizeof dir);
    target_name(d, name, sizeof name);
    snprintf(target, sizeof target, "%s/%s", dir, name);
    char *mk[] = { "install", "-d", dir, NULL };
    if (as_owner(mk) != 0) { fprintf(stderr, "snap-deb: could not create %s\n", dir); return 1; }
    char *cp[] = { "install", "-m", "644", (char *)deb, target, NULL };
    if (as_owner(cp) != 0) { fprintf(stderr, "snap-deb: could not copy the package to %s\n", dir); return 1; }
    remove_older(dir, d, name);
    printf("  %s✓ Declared:%s %s\n", GRN, RST, target);
    return 0;
}

static int rebuild(void) {
    char *argv[] = { "snapos", "rebuild", NULL };
    return run(argv);
}

static int check_file(const char *deb, DebInfo *d) {
    if (!read_info(deb, d)) {
        fprintf(stderr, "snap-deb: %s is not a readable .deb package\n", deb);
        return 0;
    }
    return 1;
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
        fprintf(stderr, "snap-deb: this package is for %s, not for this computer (amd64)\n", d.arch);
        return 2;
    }
    int s = scan(deb);
    if (s == 1) return 1;
    if (s == 2 && !force) {
        fprintf(stderr, "snap-deb: not adding a file that could not be scanned (use --force to override)\n");
        return 2;
    }
    return declare(deb, &d);
}

static int cmd_list(void) {
    char dir[PATH_MAX];
    debs_dir(dir, sizeof dir);
    DIR *dh = opendir(dir);
    int n = 0;
    if (dh) {
        struct dirent *e;
        while ((e = readdir(dh))) {
            size_t len = strlen(e->d_name);
            if (len > 4 && !strcmp(e->d_name + len - 4, ".deb")) {
                printf("  %s\n", e->d_name);
                n++;
            }
        }
        closedir(dh);
    }
    if (!n) printf("  %sno .deb packages declared%s\n", DIM, RST);
    return 0;
}

static int cmd_remove(const char *name) {
    char dir[PATH_MAX];
    debs_dir(dir, sizeof dir);
    char prefix[200];
    clean(prefix, sizeof prefix, name);
    strncat(prefix, "_", sizeof prefix - strlen(prefix) - 1);
    DIR *dh = opendir(dir);
    int removed = 0;
    if (dh) {
        struct dirent *e;
        while ((e = readdir(dh))) {
            if (strncmp(e->d_name, prefix, strlen(prefix)) != 0) continue;
            char path[PATH_MAX + 300];
            snprintf(path, sizeof path, "%s/%s", dir, e->d_name);
            char *rm[] = { "rm", "-f", path, NULL };
            if (as_owner(rm) == 0) { printf("  removed %s\n", e->d_name); removed++; }
        }
        closedir(dh);
    }
    if (!removed) { fprintf(stderr, "snap-deb: no declared package named %s\n", name); return 2; }
    printf("  %sApply it with: snapos rebuild%s\n", DIM, RST);
    return 0;
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
        printf("  %sThis package is for %s, not for this computer (amd64).%s\n", RED, d.arch, RST);
        wait_enter();
        return 2;
    }
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
        "snap-deb: open a .deb package the SnapOS way\n\n"
        "  snap-deb FILE.deb          scan it, then add it to SnapOS (asks first)\n"
        "  snap-deb add FILE.deb      scan it and declare it without asking [--force]\n"
        "  snap-deb info FILE.deb     show what it is\n"
        "  snap-deb list              packages added this way\n"
        "  snap-deb remove NAME       take one out again\n\n"
        "Declared packages live in %s/debs and are built on the next\n"
        "'snapos rebuild'. Not every .deb works: a program that needs Debian\n"
        "services or a specific Debian library may not start.\n", nixdir());
}

int main(int argc, char **argv) {
    if (argc < 2 || !strcmp(argv[1], "help") || !strcmp(argv[1], "-h") || !strcmp(argv[1], "--help")) {
        usage(argc < 2 ? stderr : stdout);
        return argc < 2 ? 2 : 0;
    }
    if (!strcmp(argv[1], "list")) return cmd_list();
    if (!strcmp(argv[1], "info") && argc > 2) return cmd_info(argv[2]);
    if (!strcmp(argv[1], "remove") && argc > 2) return cmd_remove(argv[2]);
    if (!strcmp(argv[1], "add") && argc > 2) {
        int force = argc > 3 && !strcmp(argv[3], "--force");
        return cmd_add(argv[2], force);
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
