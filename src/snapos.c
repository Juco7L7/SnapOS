#define _POSIX_C_SOURCE 200809L
#include <dirent.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <sys/utsname.h>
#include <sys/wait.h>

/* snapos: the system commands. The system lives in /etc/snapos (older installs
 * have it in /etc/nixos, which `snapos update` moves). */

#define SNAPOS_DIR  "/etc/snapos"
#define LEGACY_DIR  "/etc/nixos"
#define FLAKE_ATTR  "snapos"
#define UPDATE_API     "https://api.github.com/repos/Juco7L7/SnapOS"

#define RED "\033[91m"
#define GRN "\033[32m"
#define YEL "\033[33m"
#define DIM "\033[2m"
#define BLD "\033[1m"
#define RST "\033[0m"

static const char *nixdir(void) {
    const char *d = getenv("SNAPOS_NIX_DIR");
    if (d && *d) return d;
    if (access(SNAPOS_DIR "/configuration.nix", F_OK) == 0) return SNAPOS_DIR;
    if (access(LEGACY_DIR "/configuration.nix", F_OK) == 0) return LEGACY_DIR;
    return SNAPOS_DIR;
}

/* Where an update goes: /etc/snapos, or the test directory. */
static const char *target_dir(void) {
    const char *d = getenv("SNAPOS_NIX_DIR");
    return (d && *d) ? d : SNAPOS_DIR;
}

static const char *env_or(const char *name, const char *fallback) {
    const char *v = getenv(name);
    return (v && *v) ? v : fallback;
}

static void usage(FILE *out) {
    fprintf(out,
        "snapos — manage this SnapOS system\n"
        "\n"
        "  snapos config            open configuration.nix in $EDITOR (nano if unset)\n"
        "  snapos rebuild [MODE]    apply the config; MODE = switch (default), test,\n"
        "                           boot or dry-build. Extra args go to nixos-rebuild.\n"
        "  snapos update [--force]  install the latest SnapOS release for the next boot\n"
        "                           (keeps your files; --force allows an older release)\n"
        "  snapos update check      is there a newer release? (exit 10 when there is)\n"
        "  snapos version           the release this system runs\n"
        "  snapos doctor            show graphics, boot and antivirus problems (GPU, failed units,\n"
        "                           display manager and X errors)\n"
        "  snapos help              this text\n"
        "\n"
        "Runs through sudo automatically when you are not root.\n");
}

static int run(char *const argv[]) {
    fflush(NULL);
    pid_t pid = fork();
    if (pid < 0) return -1;
    if (pid == 0) {
        execvp(argv[0], argv);
        perror(argv[0]);
        _exit(127);
    }
    int st;
    waitpid(pid, &st, 0);
    return WIFEXITED(st) ? WEXITSTATUS(st) : -1;
}

static int run_quiet(char *const argv[]) {
    fflush(NULL);
    pid_t pid = fork();
    if (pid < 0) return -1;
    if (pid == 0) {
        int fd = open("/dev/null", 1);
        if (fd >= 0) { dup2(fd, 1); dup2(fd, 2); }
        execvp(argv[0], argv);
        _exit(127);
    }
    int st;
    waitpid(pid, &st, 0);
    return WIFEXITED(st) ? WEXITSTATUS(st) : -1;
}

static int exists(const char *p) { return access(p, F_OK) == 0; }

static int is_dir(const char *p) {
    struct stat st;
    return stat(p, &st) == 0 && S_ISDIR(st.st_mode);
}

static int read_line(const char *path, char *dst, size_t n) {
    FILE *f = fopen(path, "r");
    dst[0] = 0;
    if (!f) return 0;
    if (!fgets(dst, (int)n, f)) { fclose(f); return 0; }
    fclose(f);
    dst[strcspn(dst, "\r\n")] = 0;
    return dst[0] != 0;
}

static void reexec_with_sudo(int argc, char **argv) {
    char self[PATH_MAX];
    ssize_t n = readlink("/proc/self/exe", self, sizeof self - 1);
    if (n <= 0) { perror("snapos: cannot locate myself"); return; }
    self[n] = '\0';

    char **nargv = calloc((size_t)argc + 4, sizeof *nargv);
    if (!nargv) { perror("snapos"); return; }
    int k = 0;
    nargv[k++] = "sudo";
    nargv[k++] = "--preserve-env=SNAPOS_NIX_DIR,SNAPOS_UPDATE_API,SNAPOS_STATE_DIR,SNAPOS_PROFILE,SNAPDEB_DIR,SNAPOS_OS_RELEASE";
    nargv[k++] = self;
    for (int i = 1; i < argc; i++) nargv[k++] = argv[i];
    fprintf(stderr, "snapos: needs root, asking sudo...\n");
    execvp("sudo", nargv);
    perror("snapos: could not run sudo");
    free(nargv);
}

static int cmd_config(void) {
    char path[PATH_MAX];
    snprintf(path, sizeof path, "%s/configuration.nix", nixdir());
    if (access(path, F_OK) != 0) {
        fprintf(stderr,
            "snapos: %s not found.\n"
            "  This does not look like an installed SnapOS system.\n", path);
        return 1;
    }
    const char *editor = getenv("EDITOR");
    if (!editor || !*editor) editor = "nano";
    fprintf(stderr, "snapos: opening %s with %s\n"
                    "snapos: when you are done, apply it with: snapos rebuild\n\n",
            path, editor);
    execlp(editor, editor, path, (char *)NULL);
    perror("snapos: could not start the editor");
    return 1;
}

/* The Debian layer follows the .deb files declared in <nixdir>/debs. */
static int has_debs(void) {
    char dir[PATH_MAX];
    snprintf(dir, sizeof dir, "%s/debs", nixdir());
    DIR *dh = opendir(dir);
    if (!dh) return access("/var/lib/snapdeb/installed", F_OK) == 0;
    struct dirent *e;
    int n = 0;
    while ((e = readdir(dh))) {
        size_t len = strlen(e->d_name);
        if (len > 4 && !strcmp(e->d_name + len - 4, ".deb")) n++;
    }
    closedir(dh);
    return n > 0 || access("/var/lib/snapdeb/installed", F_OK) == 0;
}

static int nixos_rebuild(const char *dir, const char *mode, int argc, char **argv, int extra) {
    char flake[PATH_MAX + 16];
    snprintf(flake, sizeof flake, "path:%s#%s", dir, FLAKE_ATTR);
    char **nargv = calloc((size_t)argc + 6, sizeof *nargv);
    if (!nargv) { perror("snapos"); return 1; }
    int k = 0;
    nargv[k++] = "nixos-rebuild";
    nargv[k++] = (char *)mode;
    nargv[k++] = "--flake";
    nargv[k++] = flake;
    for (int i = extra; i < argc; i++) nargv[k++] = argv[i];
    fprintf(stderr, "snapos: nixos-rebuild %s --flake %s\n\n", mode, flake);
    int rc = run(nargv);
    free(nargv);
    return rc < 0 ? 1 : rc;
}

static int cmd_rebuild(int argc, char **argv) {
    const char *mode = "switch";
    int extra = 2;
    if (argc > 2 && argv[2][0] != '-') { mode = argv[2]; extra = 3; }
    if (strcmp(mode, "switch") && strcmp(mode, "test") &&
        strcmp(mode, "boot")   && strcmp(mode, "dry-build")) {
        fprintf(stderr, "snapos: unknown rebuild mode '%s' "
                        "(use switch, test, boot or dry-build)\n", mode);
        return 2;
    }
    int rc = nixos_rebuild(nixdir(), mode, argc, argv, extra);
    if (rc != 0) {
        fprintf(stderr, "snapos: nixos-rebuild failed (is this a NixOS/SnapOS system?)\n");
        return rc;
    }
    if (strcmp(mode, "dry-build") != 0 && has_debs()) {
        fprintf(stderr, "\nsnapos: snap-deb sync\n\n");
        char *sync[] = { "snap-deb", "sync", NULL };
        rc = run(sync);
        if (rc != 0) fprintf(stderr, "snapos: the Debian layer is not up to date; see 'snap-deb status'\n");
    }
    return rc < 0 ? 1 : rc;
}

static int cmd_doctor(void) {
    const char *script =
        "echo '== GPU'; lspci 2>/dev/null | grep -iE 'vga|3d|display'\n"
        "echo; echo '== kernel'; uname -r\n"
        "echo; echo '== SnapOS release'; snapos version 2>&1\n"
        "echo; echo '== network'; nmcli -t -f DEVICE,TYPE,STATE device 2>&1; lspci -nnk 2>/dev/null | grep -A3 -iE 'network|wireless'; rfkill list 2>/dev/null | grep -iE 'wireless|blocked'; dmesg 2>/dev/null | grep -iE 'firmware|wlan|iwl|ath[0-9]|brcm|rtw|mt76' | tail -6\n"
        "echo; echo '== failed units'; systemctl --failed --no-legend\n"
        "echo; echo '== display manager'; journalctl -u display-manager -b --no-pager -n 25\n"
        "echo; echo '== X errors'; grep -E '\\(EE\\)' /var/log/X.0.log 2>/dev/null | head -20\n"
        "echo; echo '== graphics kernel messages'; dmesg 2>/dev/null | grep -iE 'drm|radeon|amdgpu|nouveau|i915|firmware' | tail -15\n"
        "echo; echo '== Debian layer'; snap-deb status 2>&1\n"
        "echo; echo '== antivirus'; snapguard status 2>&1\n"
        "echo; systemctl is-active clamav-freshclam clamav-daemon 2>&1\n"
        "echo; journalctl -u clamav-freshclam -b --no-pager -n 6 2>&1\n";
    execl("/bin/sh", "sh", "-c", script, (char *)NULL);
    perror("snapos: could not run the checks");
    return 1;
}

/* ---- updates ------------------------------------------------------------ */

/* The release file holds the commit this system was installed from. */
static void release_file(char *dst, size_t n) { snprintf(dst, n, "%s/release", nixdir()); }

static int installed_commit(char *dst, size_t n) {
    char p[PATH_MAX];
    release_file(p, sizeof p);
    if (read_line(p, dst, n)) return 1;
    if (read_line("/etc/snapos-build", dst, n)) return 1;
    snprintf(dst, n, "unknown");
    return 0;
}

static int fetch(const char *url, const char *out) {
    char *argv[] = { "curl", "-fsSL", "-A", "snapos-update", "-H", "Accept: application/vnd.github+json",
                     "-o", (char *)out, (char *)url, NULL };
    return run_quiet(argv) == 0;
}

static char *slurp(const char *path) {
    FILE *f = fopen(path, "r");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (n < 0 || n > 4 * 1024 * 1024) { fclose(f); return NULL; }
    char *s = malloc((size_t)n + 1);
    if (!s) { fclose(f); return NULL; }
    size_t got = fread(s, 1, (size_t)n, f);
    s[got] = 0;
    fclose(f);
    return s;
}

/* Reads the string value of a JSON key at the top level of the text, with
 * the common escapes turned back into characters. */
static int json_string(const char *json, const char *key, char *dst, size_t n) {
    char pat[128];
    snprintf(pat, sizeof pat, "\"%s\"", key);
    const char *p = strstr(json, pat);
    dst[0] = 0;
    if (!p) return 0;
    p = strchr(p + strlen(pat), ':');
    if (!p) return 0;
    p++;
    while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r') p++;
    if (*p != '"') return 0;
    p++;
    size_t j = 0;
    while (*p && *p != '"' && j + 1 < n) {
        if (*p == '\\' && p[1]) {
            p++;
            switch (*p) {
            case 'n': dst[j++] = '\n'; break;
            case 't': dst[j++] = '\t'; break;
            case 'r': break;
            case 'u': if (strlen(p) >= 5) p += 4; dst[j++] = '?'; break;
            default:  dst[j++] = *p; break;
            }
        } else {
            dst[j++] = *p;
        }
        p++;
    }
    dst[j] = 0;
    return 1;
}

typedef struct {
    char tag[128], name[256], sha[64], date[64];
    char notes[4096];
    char source_url[1024], sum_url[1024];
} Release;

/* Where the update guard keeps its markers, and the NixOS system profile. */
static const char *state_dir(void) { return env_or("SNAPOS_STATE_DIR", "/var/lib/snapos/update"); }
static const char *profile_link(void) { return env_or("SNAPOS_PROFILE", "/nix/var/nix/profiles/system"); }

/* Downloads land in a folder only root can enter: a shared /tmp would let
 * another user plant a link and have root overwrite a file of their choice. */
static const char *work_dir(void) {
    static char dir[PATH_MAX];
    snprintf(dir, sizeof dir, "%s/work", state_dir());
    char *mk[] = { "mkdir", "-p", dir, NULL };
    run_quiet(mk);
    chmod(dir, 0700);
    return dir;
}

/* Right after login the Wi-Fi may still be connecting. */
static void wait_for_network(void) {
    if (getenv("SNAPOS_UPDATE_API")) return;
    char *argv[] = { "nm-online", "-q", "-t", "30", NULL };
    run_quiet(argv);
}

/* The release's source package and its checksum, attached by CI. */
static void find_assets(const char *json, Release *r) {
    const char *p = json;
    char url[1024];
    while ((p = strstr(p, "\"browser_download_url\""))) {
        if (!json_string(p, "browser_download_url", url, sizeof url)) break;
        size_t n = strlen(url);
        if (n > 21 && !strcmp(url + n - 21, "/snapos-source.tar.gz")) snprintf(r->source_url, sizeof r->source_url, "%s", url);
        if (n > 28 && !strcmp(url + n - 28, "/snapos-source.tar.gz.sha256")) snprintf(r->sum_url, sizeof r->sum_url, "%s", url);
        p += 22;
    }
}

/* The latest release: its tag, name, notes, files and the commit the tag points at. */
static int latest_release(Release *r) {
    memset(r, 0, sizeof *r);
    wait_for_network();
    const char *api = env_or("SNAPOS_UPDATE_API", UPDATE_API);
    char url[1024], tmp[PATH_MAX];
    snprintf(tmp, sizeof tmp, "%s/release.json", work_dir());
    snprintf(url, sizeof url, "%s/releases/latest", api);
    if (!fetch(url, tmp)) { unlink(tmp); return 0; }
    char *json = slurp(tmp);
    unlink(tmp);
    if (!json) return 0;
    json_string(json, "tag_name", r->tag, sizeof r->tag);
    json_string(json, "name", r->name, sizeof r->name);
    json_string(json, "published_at", r->date, sizeof r->date);
    json_string(json, "body", r->notes, sizeof r->notes);
    find_assets(json, r);
    free(json);
    if (!r->tag[0]) return 0;

    snprintf(url, sizeof url, "%s/git/ref/tags/%s", api, r->tag);
    if (!fetch(url, tmp)) { unlink(tmp); return 0; }
    json = slurp(tmp);
    unlink(tmp);
    if (!json) return 0;
    char type[32];
    json_string(json, "sha", r->sha, sizeof r->sha);
    json_string(json, "type", type, sizeof type);
    free(json);
    /* an annotated tag points at a tag object, which points at the commit */
    if (!strcmp(type, "tag") && r->sha[0]) {
        snprintf(url, sizeof url, "%s/git/tags/%s", api, r->sha);
        if (!fetch(url, tmp)) { unlink(tmp); return 0; }
        json = slurp(tmp);
        unlink(tmp);
        if (!json) return 0;
        const char *obj = strstr(json, "\"object\"");
        r->sha[0] = 0;
        if (obj) json_string(obj, "sha", r->sha, sizeof r->sha);
        free(json);
    }
    return r->sha[0] != 0;
}

static int same_commit(const char *a, const char *b) {
    size_t n = strlen(a) < strlen(b) ? strlen(a) : strlen(b);
    return n >= 7 && strncmp(a, b, n) == 0;
}

/* The release file: the commit, then the release date and name. */
static int installed_date(char *dst, size_t n) {
    char p[PATH_MAX];
    release_file(p, sizeof p);
    dst[0] = 0;
    FILE *f = fopen(p, "r");
    if (!f) return 0;
    char line[256];
    if (fgets(line, sizeof line, f) && fgets(line, sizeof line, f)) {
        line[strcspn(line, "\r\n")] = 0;
        snprintf(dst, n, "%.*s", (int)n - 1, line);
    }
    fclose(f);
    return dst[0] != 0;
}

static void write_release_file(const char *dir, const Release *r) {
    char rel[PATH_MAX];
    snprintf(rel, sizeof rel, "%s/release", dir);
    FILE *f = fopen(rel, "w");
    if (f) { fprintf(f, "%s\n%s\n%s\n", r->sha, r->date, r->name); fclose(f); }
}

static int version_of(const char *dir, char *dst, size_t n);
static int version_cmp(const char *a, const char *b);
static int read_pending(int *previous, int *attempts, char *name, size_t n);

/* The version of the system that is running now, from /etc/os-release
 * (the files in /etc/snapos may already be newer than what runs). */
static int running_version(char *dst, size_t n) {
    const char *p = env_or("SNAPOS_OS_RELEASE", "/etc/os-release");
    FILE *f = fopen(p, "r");
    snprintf(dst, n, "?");
    if (!f) return 0;
    char line[256];
    int ok = 0;
    while (fgets(line, sizeof line, f)) {
        if (strncmp(line, "VERSION_ID=", 11)) continue;
        char *v = line + 11;
        if (*v == '"') v++;
        v[strcspn(v, "\"\r\n")] = 0;
        if (*v) { snprintf(dst, n, "%.*s", (int)n - 1, v); ok = 1; }
        break;
    }
    fclose(f);
    return ok;
}

/* The version a release announces in its name ("SnapOS installer V2.1"). */
static int release_version(const Release *r, char *dst, size_t n) {
    const char *from[] = { r->name, r->tag };
    for (int i = 0; i < 2; i++) {
        for (const char *p = from[i]; *p; p++) {
            if ((*p == 'V' || *p == 'v') && p[1] >= '0' && p[1] <= '9') {
                size_t k = 0;
                for (const char *q = p + 1; (*q >= '0' && *q <= '9') || *q == '.'; q++)
                    if (k + 1 < n) dst[k++] = *q;
                dst[k] = 0;
                return 1;
            }
        }
    }
    snprintf(dst, n, "?");
    return 0;
}

/* 0 = this system is the latest release; 10 = a newer release exists;
 * 11 = the newer release is already built and only waits for a restart. */
static int update_state(const char *cur, const Release *r, char *pending, size_t n) {
    int previous, attempts;
    int built = read_pending(&previous, &attempts, pending, n);
    if (!built) pending[0] = 0;
    if (!same_commit(cur, r->sha)) return 10;
    if (built) return 11;
    char run[64], rel[64];
    if (running_version(run, sizeof run) && release_version(r, rel, sizeof rel) && version_cmp(rel, run) > 0) return 10;
    return 0;
}

/* Prints what a caller (the login check) needs. Exit 10 = update available,
 * 11 = built, restart to use it. */
static int cmd_update_check(void) {
    Release r;
    char cur[128], pending[256], run[64];
    installed_commit(cur, sizeof cur);
    if (!latest_release(&r)) {
        fprintf(stderr, "snapos: could not read the latest release (no network?)\n");
        return 1;
    }
    char ver[64];
    version_of(nixdir(), ver, sizeof ver);
    running_version(run, sizeof run);
    int st = update_state(cur, &r, pending, sizeof pending);
    printf("tag %s\nversion %s\nrunning %s\ncurrent %.7s\nlatest %.7s\nname %s\nstate %s\nnotes\n%s\n", r.tag, ver, run, cur, r.sha, r.name,
           st == 0 ? "current" : st == 10 ? "available" : "built", r.notes);
    return st;
}

/* The SnapOS version of a system tree, from its VERSION file. */
static int version_of(const char *dir, char *dst, size_t n) {
    char p[PATH_MAX + 16];
    snprintf(p, sizeof p, "%s/VERSION", dir);
    if (read_line(p, dst, n)) return 1;
    snprintf(dst, n, "?");
    return 0;
}

/* Compares versions like 2.0 and 2.10 part by part. */
static int version_cmp(const char *a, const char *b) {
    while (*a || *b) {
        long x = strtol(a, (char **)&a, 10), y = strtol(b, (char **)&b, 10);
        if (x != y) return x < y ? -1 : 1;
        if (*a == '.') a++;
        if (*b == '.') b++;
    }
    return 0;
}

static int cmd_version(void) {
    char cur[128], date[64], ver[64];
    installed_commit(cur, sizeof cur);
    installed_date(date, sizeof date);
    version_of(nixdir(), ver, sizeof ver);
    printf("SnapOS V%s (build %.7s%s%.10s)\n", ver, cur, date[0] ? ", released " : "", date);
    return 0;
}

/* The update screen looks like the installer: a header, numbered steps. */
static int ustep = 0;
static void uheader(const char *stepname) {
    char cur[128];
    installed_commit(cur, sizeof cur);
    if (isatty(1)) printf("\033[2J\033[H");
    printf("\n  %s%sSnapOS%s  %sUpdater   build %.7s%s\n", BLD, RED, RST, DIM, cur, RST);
    if (stepname) printf("  %sStep %d/4  ·  %s%s\n", DIM, ustep, stepname, RST);
    printf("  %s──────────────────────────────────────────────%s\n\n", DIM, RST);
}
static void uok(const char *s)   { printf("  %s✓%s %s\n", GRN, RST, s); }
static void uwarn(const char *s) { printf("  %s%s%s\n", YEL, s, RST); }
static void ufail(const char *s) { printf("\n  %s✗ %s%s\n", RED, s, RST); }

static int copy_keep(const char *from, const char *to) {
    static const char *files[] = { "configuration.nix", "hardware-configuration.nix", "local.nix",
                                   "graphics.nix", "release", NULL };
    for (int i = 0; files[i]; i++) {
        char a[PATH_MAX + 64], b[PATH_MAX + 64];
        snprintf(a, sizeof a, "%s/%s", from, files[i]);
        snprintf(b, sizeof b, "%s/%s", to, files[i]);
        if (!exists(a)) continue;
        if (!strcmp(files[i], "configuration.nix") && exists(b)) {
            /* the user's file stays; the release's example is kept beside it when it differs */
            char *cmp[] = { "cmp", "-s", a, b, NULL };
            if (run_quiet(cmp) != 0) {
                char nb[PATH_MAX + 72];
                snprintf(nb, sizeof nb, "%s.new", b);
                char *mv[] = { "mv", "-f", b, nb, NULL };
                run(mv);
            }
        }
        char *cp[] = { "cp", "-a", a, b, NULL };
        if (run(cp) != 0) return 0;
    }
    char debs[PATH_MAX];
    snprintf(debs, sizeof debs, "%s/debs", from);
    if (is_dir(debs)) {
        char *cp[] = { "cp", "-a", debs, (char *)to, NULL };
        if (run(cp) != 0) return 0;
    }
    return 1;
}

/* The checklist. Everything here is looked at before anything changes. */
static int preflight(const Release *r, int force) {
    char line[512];
    int ok = 1;
    struct utsname u;
    if (uname(&u) == 0 && strcmp(u.machine, "x86_64") != 0) {
        snprintf(line, sizeof line, "This computer is %s; SnapOS releases are built for x86_64.", u.machine);
        ufail(line); ok = 0;
    } else uok("Architecture: x86_64");

    const char *where = getenv("SNAPOS_NIX_DIR") ? target_dir() : "/nix";
    struct statvfs vfs;
    long need_mb = atol(env_or("SNAPOS_MIN_FREE_MB", "4000"));
    if (statvfs(where, &vfs) == 0) {
        long free_mb = (long)((unsigned long long)vfs.f_bavail * vfs.f_frsize / (1024 * 1024));
        snprintf(line, sizeof line, "Free space: %ld MB on %s (needs %ld MB)", free_mb, where, need_mb);
        if (free_mb < need_mb) { ufail(line); ok = 0; } else uok(line);
    }

    char date[64];
    if (installed_date(date, sizeof date) && r->date[0] && strcmp(r->date, date) < 0) {
        snprintf(line, sizeof line, "The latest release (%s) is older than the installed one (%s).", r->date, date);
        if (force) { uwarn(line); uwarn("Going ahead because of --force."); }
        else { ufail(line); uwarn("Use --force to install it anyway."); ok = 0; }
    } else uok("Not a downgrade");

    if (!r->source_url[0] || !r->sum_url[0]) {
        ufail("This release has no source package with a checksum; it cannot be installed this way.");
        ok = 0;
    } else uok("The release ships its source package and checksum");
    return ok;
}

/* The checksum file says "<sha256>  snapos-source.tar.gz"; sha256sum of the
 * download must say the same. */
static int verify_download(const char *tarball, const char *sumfile) {
    char want[128], got[512];
    if (!read_line(sumfile, want, sizeof want)) return 0;
    want[strcspn(want, " \t")] = 0;
    char *argv[] = { "sha256sum", (char *)tarball, NULL };
    int fds[2];
    if (pipe(fds) != 0) return 0;
    fflush(NULL);
    pid_t pid = fork();
    if (pid < 0) return 0;
    if (pid == 0) { close(fds[0]); dup2(fds[1], 1); close(fds[1]); execvp(argv[0], argv); _exit(127); }
    close(fds[1]);
    ssize_t n = read(fds[0], got, sizeof got - 1);
    close(fds[0]);
    int st;
    waitpid(pid, &st, 0);
    if (n <= 0) return 0;
    got[n] = 0;
    got[strcspn(got, " \t\n")] = 0;
    return strlen(want) == 64 && strcmp(want, got) == 0;
}

static int current_generation(void) {
    char link[PATH_MAX];
    ssize_t n = readlink(profile_link(), link, sizeof link - 1);
    if (n <= 0) return -1;
    link[n] = 0;
    const char *b = strrchr(link, '/');
    b = b ? b + 1 : link;
    int gen = -1;
    if (sscanf(b, "system-%d-link", &gen) != 1) return -1;
    return gen;
}

static void pending_path(char *dst, size_t n) { snprintf(dst, n, "%s/update-pending", state_dir()); }
static void rolled_back_path(char *dst, size_t n) { snprintf(dst, n, "%s/update-rolled-back", state_dir()); }

static int read_pending(int *previous, int *attempts, char *name, size_t n) {
    char p[PATH_MAX];
    pending_path(p, sizeof p);
    FILE *f = fopen(p, "r");
    if (!f) return 0;
    *previous = -1; *attempts = 0; name[0] = 0;
    char line[512];
    while (fgets(line, sizeof line, f)) {
        line[strcspn(line, "\r\n")] = 0;
        if (!strncmp(line, "previous=", 9)) *previous = atoi(line + 9);
        else if (!strncmp(line, "attempts=", 9)) *attempts = atoi(line + 9);
        else if (!strncmp(line, "name=", 5)) snprintf(name, n, "%.*s", (int)n - 1, line + 5);
    }
    fclose(f);
    return 1;
}

static int write_pending(int previous, int attempts, const char *name) {
    char p[PATH_MAX];
    pending_path(p, sizeof p);
    char *mk[] = { "mkdir", "-p", (char *)state_dir(), NULL };
    run_quiet(mk);
    FILE *f = fopen(p, "w");
    if (!f) return 0;
    fprintf(f, "previous=%d\nattempts=%d\nname=%s\n", previous, attempts, name);
    fclose(f);
    return 1;
}

static int cmd_update(int force) {
    Release r;
    char cur[128], line[512];
    const char *old = nixdir();
    const char *target = target_dir();
    installed_commit(cur, sizeof cur);

    ustep = 1; uheader("Checking");
    if (!latest_release(&r)) { ufail("Could not read the latest release. Is the network up?"); return 1; }
    snprintf(line, sizeof line, "Installed: build %.7s", cur); uok(line);
    snprintf(line, sizeof line, "Latest:    %s (%.7s)", r.name[0] ? r.name : r.tag, r.sha); uok(line);
    char pending[256], runver[64];
    int st = update_state(cur, &r, pending, sizeof pending);
    if (st == 0) { printf("\n  %sSnapOS is up to date.%s\n", BLD, RST); return 0; }
    if (st == 11) {
        printf("\n  %s✓ SnapOS %s is already built%s and starts at the next boot.\n", GRN, pending, RST);
        if (isatty(0) && !getenv("SNAPOS_NO_REBOOT")) {
            printf("\n  Restart now? [y/N] ");
            fflush(stdout);
            char buf[16];
            if (fgets(buf, sizeof buf, stdin) && (buf[0] == 'y' || buf[0] == 'Y')) {
                char *reboot[] = { "systemctl", "reboot", NULL };
                run(reboot);
            }
        }
        return 0;
    }
    if (running_version(runver, sizeof runver) && same_commit(cur, r.sha)) {
        snprintf(line, sizeof line, "The running system is V%s; the release files here are newer. Installing again.", runver);
        uwarn(line);
    }
    if (!preflight(&r, force)) { printf("\n  %sNothing was changed.%s\n", DIM, RST); return 1; }

    ustep = 2; uheader("Downloading");
    char newdir[PATH_MAX + 8], olddir[PATH_MAX + 8], tarball[PATH_MAX], sumfile[PATH_MAX];
    snprintf(newdir, sizeof newdir, "%s.new", target);
    snprintf(olddir, sizeof olddir, "%s.old", target);
    snprintf(tarball, sizeof tarball, "%s/snapos-source.tar.gz", work_dir());
    snprintf(sumfile, sizeof sumfile, "%s/snapos-source.tar.gz.sha256", work_dir());
    char *rm_new[] = { "rm", "-rf", newdir, NULL };
    run(rm_new);
    char *mk[] = { "mkdir", "-p", newdir, NULL };
    if (run(mk) != 0) return 1;
    printf("  %s%s%s\n", DIM, r.source_url, RST);
    if (!fetch(r.source_url, tarball) || !fetch(r.sum_url, sumfile)) { ufail("The download failed."); run(rm_new); return 1; }
    if (!verify_download(tarball, sumfile)) {
        ufail("The download does not match its checksum. Nothing was changed; try again later.");
        unlink(tarball); unlink(sumfile); run(rm_new);
        return 1;
    }
    uok("Checksum matches");
    char *tar[] = { "tar", "-xzf", tarball, "-C", newdir, "--strip-components=1", NULL };
    if (run(tar) != 0) { ufail("The download could not be unpacked."); unlink(tarball); unlink(sumfile); run(rm_new); return 1; }
    unlink(tarball); unlink(sumfile);
    char check[PATH_MAX + 32];
    snprintf(check, sizeof check, "%s/flake.nix", newdir);
    if (!exists(check)) { ufail("The download does not look like SnapOS."); run(rm_new); return 1; }
    uok("Downloaded and unpacked");
    char newver[64], oldver[64];
    version_of(newdir, newver, sizeof newver);
    version_of(old, oldver, sizeof oldver);
    snprintf(line, sizeof line, "Version: V%s installed, V%s in the release", oldver, newver);
    if (newver[0] != '?' && oldver[0] != '?' && version_cmp(newver, oldver) < 0) {
        if (force) { uwarn(line); uwarn("An older version; going ahead because of --force."); }
        else { ufail(line); uwarn("That is a downgrade. Use --force to install it anyway."); run(rm_new); return 1; }
    } else uok(line);

    ustep = 3; uheader("Keeping your files");
    if (!copy_keep(old, newdir)) { ufail("Could not keep your files."); run(rm_new); return 1; }
    uok("configuration.nix, hardware-configuration.nix, local.nix, graphics.nix, debs/");
    snprintf(check, sizeof check, "%s/configuration.nix.new", newdir);
    if (exists(check)) uwarn("This release changed the example configuration.nix; it is saved as configuration.nix.new.");

    ustep = 4; uheader("Building the next system");
    int previous = current_generation();
    char *rm_old[] = { "rm", "-rf", olddir, NULL };
    run(rm_old);
    if (rename(old, olddir) != 0) { ufail("Could not move the current system aside."); run(rm_new); return 1; }
    if (rename(newdir, target) != 0) {
        ufail("Could not put the new system in place.");
        rename(olddir, old);
        run(rm_new);
        return 1;
    }
    /* an older install kept the system in /etc/nixos; a link keeps that path working */
    if (strcmp(old, target) != 0 && !exists(old)) {
        if (symlink(target, old) != 0) uwarn("Could not link the old location to the new one.");
    }
    /* the new system is only made the default for the next boot; what runs now is untouched */
    char *none[] = { NULL };
    int rc = nixos_rebuild(target, "boot", 0, none, 0);
    if (rc != 0) {
        ufail("The new system could not be built. Nothing changed: you are still on the current one.");
        char *rm_t[] = { "rm", "-rf", (char *)target, NULL };
        if (strcmp(old, target) != 0) unlink(old);
        run(rm_t);
        rename(olddir, old);
        return 1;
    }
    write_release_file(target, &r);
    write_pending(previous, 0, r.name[0] ? r.name : r.tag);
    char rb[PATH_MAX];
    rolled_back_path(rb, sizeof rb);
    unlink(rb);
    snprintf(line, sizeof line, "Built. Generation %d stays available; the new one boots next.", previous);
    uok(line);
    if (has_debs()) {
        char *sync[] = { "snap-deb", "sync", NULL };
        if (run(sync) == 0) uok("Debian layer in step with the declared packages");
        char *up[] = { "snap-deb", "upgrade", NULL };
        if (run(up) == 0) uok("Debian layer security updates applied");
    }
    printf("\n  %s✓ SnapOS %s is ready.%s Restart to use it.\n", GRN, r.name[0] ? r.name : r.tag, RST);
    printf("  %sIf the new system does not reach the login screen, SnapOS goes back to the\n  previous one by itself on the next start.%s\n", DIM, RST);
    if (isatty(0) && !getenv("SNAPOS_NO_REBOOT")) {
        printf("\n  Restart now? [y/N] ");
        fflush(stdout);
        char buf[16];
        if (fgets(buf, sizeof buf, stdin) && (buf[0] == 'y' || buf[0] == 'Y')) {
            char *reboot[] = { "systemctl", "reboot", NULL };
            run(reboot);
        }
    }
    return 0;
}

/* ---- the boot guard ----------------------------------------------------- */

/* Back to the generation that worked, and the files that go with it. */
static int rollback(int previous, const char *name) {
    char gen[32], stc[PATH_MAX + 32];
    snprintf(gen, sizeof gen, "%d", previous);
    fprintf(stderr, "snapos: going back to generation %d (the update to %s did not come up)\n", previous, name);
    char *sw[] = { "nix-env", "-p", (char *)profile_link(), "--switch-generation", gen, NULL };
    if (previous > 0 && run(sw) != 0) fprintf(stderr, "snapos: nix-env could not switch the generation\n");
    snprintf(stc, sizeof stc, "%s/bin/switch-to-configuration", profile_link());
    char *boot[] = { stc, "boot", NULL };
    if (previous > 0) run(boot);
    const char *target = target_dir();
    char olddir[PATH_MAX + 8];
    snprintf(olddir, sizeof olddir, "%s.old", target);
    if (is_dir(olddir)) {
        char failed[PATH_MAX + 16];
        snprintf(failed, sizeof failed, "%s.failed", target);
        char *rm[] = { "rm", "-rf", failed, NULL };
        run_quiet(rm);
        rename(target, failed);
        rename(olddir, target);
    }
    char p[PATH_MAX];
    rolled_back_path(p, sizeof p);
    FILE *f = fopen(p, "w");
    if (f) { fprintf(f, "%s\n", name); fclose(f); chmod(p, 0644); }
    pending_path(p, sizeof p);
    unlink(p);
    if (!getenv("SNAPOS_GUARD_NO_REBOOT")) {
        char *reboot[] = { "systemctl", "reboot", NULL };
        run(reboot);
    }
    return 0;
}

/* At boot: the first start of a new system is allowed; a second one means the
 * first never got approved, so the previous system comes back. */
static int cmd_guard_boot(void) {
    int previous, attempts;
    char name[256];
    if (!read_pending(&previous, &attempts, name, sizeof name)) return 0;
    if (attempts >= 1) return rollback(previous, name);
    write_pending(previous, 1, name);
    fprintf(stderr, "snapos: first start of %s; it is approved once someone logs in\n", name);
    return 0;
}

/* After the desktop is up: approved when a normal user has logged in; if
 * nobody manages to within the timeout, back to the previous system. */
static int someone_logged_in(void) {
    char out[4096];
    char *argv[] = { "loginctl", "list-users", "--no-legend", NULL };
    int fds[2];
    if (pipe(fds) != 0) return 0;
    fflush(NULL);
    pid_t pid = fork();
    if (pid < 0) return 0;
    if (pid == 0) { close(fds[0]); dup2(fds[1], 1); close(fds[1]); execvp(argv[0], argv); _exit(127); }
    close(fds[1]);
    ssize_t n = read(fds[0], out, sizeof out - 1);
    close(fds[0]);
    int st;
    waitpid(pid, &st, 0);
    if (n <= 0) return 0;
    out[n] = 0;
    for (char *line = strtok(out, "\n"); line; line = strtok(NULL, "\n")) {
        while (*line == ' ') line++;
        int uid = atoi(line);
        if (uid >= 1000) return 1;
    }
    return 0;
}

static int cmd_guard_approve(void) {
    int previous, attempts;
    char name[256];
    if (!read_pending(&previous, &attempts, name, sizeof name)) return 0;
    long timeout = atol(env_or("SNAPOS_APPROVE_TIMEOUT", "600"));
    long waited = 0;
    long step = timeout < 10 ? 1 : 10;
    while (waited <= timeout) {
        if (someone_logged_in()) {
            char p[PATH_MAX], olddir[PATH_MAX + 8];
            pending_path(p, sizeof p);
            unlink(p);
            rolled_back_path(p, sizeof p);
            unlink(p);
            snprintf(olddir, sizeof olddir, "%s.old", target_dir());
            char *rm[] = { "rm", "-rf", olddir, NULL };
            run_quiet(rm);
            fprintf(stderr, "snapos: %s approved: someone logged in\n", name);
            return 0;
        }
        sleep((unsigned)step);
        waited += step;
    }
    fprintf(stderr, "snapos: nobody logged in after %ld seconds\n", timeout);
    return rollback(previous, name);
}

int main(int argc, char **argv) {
    if (argc < 2 || !strcmp(argv[1], "help") ||
        !strcmp(argv[1], "-h") || !strcmp(argv[1], "--help")) {
        usage(argc < 2 ? stderr : stdout);
        return argc < 2 ? 2 : 0;
    }

    int is_config  = !strcmp(argv[1], "config");
    int is_rebuild = !strcmp(argv[1], "rebuild");
    int is_doctor  = !strcmp(argv[1], "doctor");
    int is_update  = !strcmp(argv[1], "update");
    int is_guard   = !strcmp(argv[1], "guard") && argc > 2;
    if (!strcmp(argv[1], "version")) return cmd_version();
    if (is_update && argc > 2 && !strcmp(argv[2], "check")) return cmd_update_check();
    if (!is_config && !is_rebuild && !is_doctor && !is_update && !is_guard) {
        fprintf(stderr, "snapos: unknown command '%s'\n\n", argv[1]);
        usage(stderr);
        return 2;
    }

    int writable = access(target_dir(), W_OK) == 0 && getenv("SNAPOS_NIX_DIR");
    if (geteuid() != 0 && !writable) {
        reexec_with_sudo(argc, argv);
        return 1;
    }
    if (is_doctor) return cmd_doctor();
    if (is_guard) {
        if (!strcmp(argv[2], "boot")) return cmd_guard_boot();
        if (!strcmp(argv[2], "approve")) return cmd_guard_approve();
        fprintf(stderr, "snapos: guard boot | approve\n");
        return 2;
    }
    if (is_update) {
        int rc = cmd_update(argc > 2 && !strcmp(argv[2], "--force"));
        /* in a terminal opened just for this, the message must stay readable */
        if (rc != 0 && isatty(0) && !getenv("SNAPOS_NO_REBOOT")) {
            printf("\n  %sPress Enter to close.%s", DIM, RST);
            fflush(stdout);
            char buf[16];
            if (!fgets(buf, sizeof buf, stdin)) return rc;
        }
        return rc;
    }
    return is_config ? cmd_config() : cmd_rebuild(argc, argv);
}
