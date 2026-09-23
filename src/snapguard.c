#define _POSIX_C_SOURCE 200809L
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <time.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <strings.h>
#include <limits.h>
#include <dirent.h>
#include <sys/socket.h>
#include <sys/un.h>

typedef struct { unsigned int h[8]; unsigned long long len; unsigned char buf[64]; int buflen; } SHA256_CTX;
static const unsigned int K[64] = {
0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,
0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,
0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,
0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2};
#define ROR(x,n) (((x)>>(n))|((x)<<(32-(n))))
static void sha256_block(SHA256_CTX *c, const unsigned char *p) {
    unsigned int w[64], a,b,cc,d,e,f,g,h,i;
    for (i=0;i<16;i++) w[i]=(p[i*4]<<24)|(p[i*4+1]<<16)|(p[i*4+2]<<8)|p[i*4+3];
    for (;i<64;i++){unsigned int s0=ROR(w[i-15],7)^ROR(w[i-15],18)^(w[i-15]>>3);
        unsigned int s1=ROR(w[i-2],17)^ROR(w[i-2],19)^(w[i-2]>>10); w[i]=w[i-16]+s0+w[i-7]+s1;}
    a=c->h[0];b=c->h[1];cc=c->h[2];d=c->h[3];e=c->h[4];f=c->h[5];g=c->h[6];h=c->h[7];
    for (i=0;i<64;i++){
        unsigned int S1=ROR(e,6)^ROR(e,11)^ROR(e,25), ch=(e&f)^(~e&g), t1=h+S1+ch+K[i]+w[i];
        unsigned int S0=ROR(a,2)^ROR(a,13)^ROR(a,22), maj=(a&b)^(a&cc)^(b&cc), t2=S0+maj;
        h=g;g=f;f=e;e=d+t1;d=cc;cc=b;b=a;a=t1+t2;
    }
    c->h[0]+=a;c->h[1]+=b;c->h[2]+=cc;c->h[3]+=d;c->h[4]+=e;c->h[5]+=f;c->h[6]+=g;c->h[7]+=h;
}
static void sha256_init(SHA256_CTX *c) {
    unsigned int iv[8]={0x6a09e667,0xbb67ae85,0x3c6ef372,0xa54ff53a,0x510e527f,0x9b05688c,0x1f83d9ab,0x5be0cd19};
    memcpy(c->h, iv, sizeof iv); c->len=0; c->buflen=0;
}
static void sha256_update(SHA256_CTX *c, const unsigned char *data, size_t n) {
    c->len += n;
    while (n) {
        size_t take = 64 - c->buflen; if (take > n) take = n;
        memcpy(c->buf + c->buflen, data, take); c->buflen += take; data += take; n -= take;
        if (c->buflen == 64) { sha256_block(c, c->buf); c->buflen = 0; }
    }
}
static void sha256_final(SHA256_CTX *c, unsigned char out[32]) {
    unsigned long long bitlen = c->len * 8;
    unsigned char pad = 0x80; sha256_update(c, &pad, 1);
    unsigned char zero = 0; while (c->buflen != 56) sha256_update(c, &zero, 1);
    unsigned char lenbuf[8]; for (int i=0;i<8;i++) lenbuf[i]=(unsigned char)(bitlen>>(56-8*i));
    sha256_update(c, lenbuf, 8);
    for (int i=0;i<8;i++) { out[i*4]=c->h[i]>>24; out[i*4+1]=c->h[i]>>16; out[i*4+2]=c->h[i]>>8; out[i*4+3]=c->h[i]; }
}
static int sha256_file(const char *path, char hex[65]) {
    FILE *f = fopen(path, "rb"); if (!f) return -1;
    SHA256_CTX c; sha256_init(&c);
    unsigned char buf[65536]; size_t n;
    while ((n = fread(buf, 1, sizeof buf, f)) > 0) sha256_update(&c, buf, n);
    fclose(f);
    unsigned char out[32]; sha256_final(&c, out);
    for (int i = 0; i < 32; i++) sprintf(hex + i * 2, "%02x", out[i]);
    hex[64] = 0;
    return 0;
}

static const char *R="", *B="", *DIM="", *RED="", *GRN="", *YEL="";
static void colors_init(void) {
    if (!isatty(1)) return;
    R="\033[0m"; B="\033[1m"; DIM="\033[2m"; RED="\033[31m"; GRN="\033[32m"; YEL="\033[33m";
}

static void user_dir(char *out, size_t n) {
    const char *xdg = getenv("XDG_DATA_HOME"), *home = getenv("HOME");
    if (xdg && *xdg) snprintf(out, n, "%s/snapos", xdg);
    else if (home && *home) snprintf(out, n, "%s/.local/share/snapos", home);
    else out[0] = 0;
}

static void get_paths(char *sigs, char *allow, char *quarantine, size_t n) {
    const char *e;
    char udir[512]; user_dir(udir, sizeof udir);
    int per_user = geteuid() != 0 && udir[0];
    if ((e = getenv("SNAP_GUARD_SIGS")))  { snprintf(sigs, n, "%s", e); }
    else if (access("/etc/snapos", F_OK) == 0) snprintf(sigs, n, "/etc/snapos/signatures.txt");
    else snprintf(sigs, n, "security/signatures.txt");

    if ((e = getenv("SNAP_GUARD_ALLOW"))) { snprintf(allow, n, "%s", e); }
    else if (per_user) snprintf(allow, n, "%s/allow.txt", udir);
    else if (access("/etc/snapos", F_OK) == 0) snprintf(allow, n, "/etc/snapos/allow.txt");
    else snprintf(allow, n, "security/allow.txt");

    if ((e = getenv("SNAP_GUARD_QUARANTINE"))) { snprintf(quarantine, n, "%s", e); }
    else if (per_user) snprintf(quarantine, n, "%s/quarantine", udir);
    else if (access("/var/lib/snapos", F_OK) == 0) snprintf(quarantine, n, "/var/lib/snapos/quarantine");
    else snprintf(quarantine, n, "security/quarantine");
}

static int find_hash(const char *path, const char *digest, char *label, size_t labelsz) {
    FILE *f = fopen(path, "r"); if (!f) return 0;
    char line[512];
    int found = 0;
    while (fgets(line, sizeof line, f)) {
        char *hash = strtok(line, "\r\n \t");
        if (!hash || hash[0] == '#') continue;
        if (!strcasecmp(hash, digest)) {
            char *rest = strtok(NULL, "\r\n");
            if (label) snprintf(label, labelsz, "%s", rest ? rest : "");
            found = 1; break;
        }
    }
    fclose(f);
    return found;
}

static const char *find_program(const char *name, char *path, size_t n) {
    const char *dirs = getenv("PATH");
    if (!dirs) return NULL;
    char copy[4096]; snprintf(copy, sizeof copy, "%s", dirs);
    char *save, *dir = strtok_r(copy, ":", &save);
    while (dir) {
        snprintf(path, n, "%s/%s", dir, name);
        if (access(path, X_OK) == 0) return path;
        dir = strtok_r(NULL, ":", &save);
    }
    return NULL;
}

static const char *clamd_socket(void) {
    const char *e = getenv("SNAP_GUARD_CLAMD_SOCKET");
    return e ? e : "/run/clamav/clamd.ctl";
}

static const char *clam_db_dir(void) {
    const char *e = getenv("SNAP_GUARD_CLAMDB");
    return e ? e : "/var/lib/clamav";
}

static int clamd_running(void) {
    struct sockaddr_un a;
    memset(&a, 0, sizeof a);
    a.sun_family = AF_UNIX;
    snprintf(a.sun_path, sizeof a.sun_path, "%s", clamd_socket());
    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) return 0;
    int ok = connect(fd, (struct sockaddr *)&a, sizeof a) == 0;
    close(fd);
    return ok;
}

static int clam_db_files(void) {
    static const char *ext[] = { ".cvd", ".cld", ".hdb", ".hsb", ".ndb", ".ldb" };
    DIR *d = opendir(clam_db_dir());
    if (!d) return 0;
    int n = 0;
    struct dirent *e;
    while ((e = readdir(d))) {
        const char *dot = strrchr(e->d_name, '.');
        if (!dot) continue;
        for (size_t i = 0; i < sizeof ext / sizeof ext[0]; i++)
            if (!strcmp(dot, ext[i])) { n++; break; }
    }
    closedir(d);
    return n;
}

/* Runs one ClamAV scanner on one file. Returns its exit code (0 clean, 1 found,
 * 2 error) and leaves its output (stdout and stderr) in out. */
static int run_clam(const char *bin, int fdpass, const char *path, char *out, size_t n) {
    int fds[2];
    out[0] = 0;
    if (pipe(fds) != 0) return 2;
    pid_t pid = fork();
    if (pid < 0) { close(fds[0]); close(fds[1]); return 2; }
    if (pid == 0) {
        close(fds[0]);
        dup2(fds[1], 1);
        dup2(fds[1], 2);
        close(fds[1]);
        if (fdpass) execl(bin, bin, "--no-summary", "--fdpass", path, (char *)NULL);
        else        execl(bin, bin, "--no-summary", path, (char *)NULL);
        _exit(127);
    }
    close(fds[1]);
    size_t used = 0;
    char tmp[512];
    ssize_t r;
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
    if (!WIFEXITED(st) || WEXITSTATUS(st) == 127) return 2;
    return WEXITSTATUS(st);
}

static void clam_found(const char *out, char *detail, size_t n) {
    const char *f = strstr(out, " FOUND");
    if (!f) { snprintf(detail, n, "ClamAV: infected"); return; }
    const char *s = f;
    while (s > out && s[-1] != ' ') s--;
    snprintf(detail, n, "ClamAV: %.*s", (int)(f - s), s);
}

static void clam_error(const char *out, char *detail, size_t n) {
    char line[256] = "";
    char copy[1024];
    snprintf(copy, sizeof copy, "%s", out);
    char *save, *l = strtok_r(copy, "\n", &save);
    while (l) {
        if (!line[0]) snprintf(line, sizeof line, "%s", l);
        if (strstr(l, "ERROR") || strstr(l, "Error")) { snprintf(line, sizeof line, "%s", l); break; }
        l = strtok_r(NULL, "\n", &save);
    }
    if (line[0]) snprintf(detail, n, "ClamAV could not scan: %s", line);
    else snprintf(detail, n, "ClamAV could not scan this file");
}

static int clamd_down;

/* Tries the ClamAV daemon first (fast, and --fdpass lets it read the user's
 * files without any permissions), then falls back to the stand-alone scanner. */
static int clamav_scan(const char *path, char *detail, size_t n) {
    char dpath[512], spath[512], out[1024];
    const char *clamd = find_program("clamdscan", dpath, sizeof dpath);
    const char *solo = find_program("clamscan", spath, sizeof spath);
    if (!clamd && !solo) { snprintf(detail, n, "ClamAV not installed"); return 2; }

    int rc = 2;
    out[0] = 0;
    if (clamd && !clamd_down) {
        rc = run_clam(clamd, 1, path, out, sizeof out);
        if (rc == 2 && (strstr(out, "connect") || strstr(out, "Connect"))) clamd_down = 1;
    }
    if (rc == 2 && solo) rc = run_clam(solo, 0, path, out, sizeof out);

    if (rc == 0) { snprintf(detail, n, "ClamAV: OK"); return 0; }
    if (rc == 1) { clam_found(out, detail, n); return 1; }
    clam_error(out, detail, n);
    return 2;
}

/* 0 clean, 1 infected, 2 unknown, 3 the file still needs ClamAV */
static int precheck(const char *path, char *detail, size_t n) {
    if (access(path, F_OK) != 0) { snprintf(detail, n, "no such file"); return 2; }
    char digest[65];
    if (sha256_file(path, digest) != 0) { snprintf(detail, n, "unreadable"); return 2; }

    char sigs[512], allow[512], quarantine[512];
    get_paths(sigs, allow, quarantine, sizeof sigs);
    char label[256];
    if (find_hash(allow, digest, label, sizeof label) ||
        (!getenv("SNAP_GUARD_ALLOW") && find_hash("/etc/snapos/allow.txt", digest, label, sizeof label))) {
        snprintf(detail, n, "trusted (allowlisted)"); return 0;
    }
    if (find_hash(sigs, digest, label, sizeof label)) {
        snprintf(detail, n, "SnapOS signature: %s", label[0] ? label : "known-bad hash");
        return 1;
    }
    return 3;
}

static int scan_one(const char *path, char *detail, size_t n) {
    int v = precheck(path, detail, n);
    return v == 3 ? clamav_scan(path, detail, n) : v;
}

#define MANY_MAX 64

/* One stand-alone ClamAV run for many files, so the signature database is
 * loaded once instead of once per file. */
static void clamscan_many(const char *bin, char **files, int n, int *v, char (*detail)[300]) {
    static char out[1 << 17];
    char *argv[MANY_MAX + 4];
    int k = 0;
    argv[k++] = (char *)bin;
    argv[k++] = "--no-summary";
    for (int i = 0; i < n && k < MANY_MAX + 3; i++) argv[k++] = files[i];
    argv[k] = NULL;

    int fds[2];
    out[0] = 0;
    size_t used = 0;
    if (pipe(fds) == 0) {
        pid_t pid = fork();
        if (pid == 0) {
            close(fds[0]); dup2(fds[1], 1); dup2(fds[1], 2); close(fds[1]);
            execv(bin, argv);
            _exit(127);
        }
        close(fds[1]);
        char tmp[4096];
        ssize_t r;
        while ((r = read(fds[0], tmp, sizeof tmp)) > 0) {
            size_t take = (size_t)r;
            if (used + take >= sizeof out) take = sizeof out - 1 - used;
            memcpy(out + used, tmp, take);
            used += take;
        }
        out[used] = 0;
        close(fds[0]);
        int st;
        waitpid(pid, &st, 0);
    }

    for (int i = 0; i < n; i++) {
        size_t len = strlen(files[i]);
        v[i] = 2;
        clam_error(out, detail[i], 300);
        char *save, *copy = strdup(out), *line = strtok_r(copy, "\n", &save);
        while (line) {
            if (!strncmp(line, files[i], len) && line[len] == ':' && line[len + 1] == ' ') {
                const char *what = line + len + 2;
                if (!strcmp(what, "OK")) { v[i] = 0; snprintf(detail[i], 300, "ClamAV: OK"); }
                else if (strstr(what, " FOUND")) { v[i] = 1; clam_found(line, detail[i], 300); }
                else snprintf(detail[i], 300, "ClamAV could not scan: %s", what);
                break;
            }
            line = strtok_r(NULL, "\n", &save);
        }
        free(copy);
    }
}

static void print_result(const char *path, int v, const char *detail) {
    if (v == 1) printf("  %s✗ INFECTED%s  %s  — %s\n", RED, R, path, detail);
    else if (v == 2) printf("  %s? UNKNOWN %s  %s  — %s\n", YEL, R, path, detail);
    else printf("  %s✓ clean   %s  %s  %s(%s)%s\n", GRN, R, path, DIM, detail, R);
    fflush(stdout);
}

static int scan_files(int argc, char **argv, long *counts) {
    int worst = 0;
    char dpath[512], spath[512];
    const char *clamd = find_program("clamdscan", dpath, sizeof dpath);
    const char *solo = find_program("clamscan", spath, sizeof spath);
    int many = argc > 1 && argc <= MANY_MAX && solo && !(clamd && clamd_running());

    if (!many) {
        for (int i = 0; i < argc; i++) {
            char detail[300];
            int v = scan_one(argv[i], detail, sizeof detail);
            print_result(argv[i], v, detail);
            counts[v == 1 ? 1 : v == 2 ? 2 : 0]++;
            if (v == 1) worst = 1;
            else if (v == 2 && worst != 1) worst = 2;
        }
        return worst;
    }

    int v[MANY_MAX], vv[MANY_MAX];
    char detail[MANY_MAX][300], dd[MANY_MAX][300];
    char *need[MANY_MAX];
    int idx[MANY_MAX], nneed = 0;
    for (int i = 0; i < argc; i++) {
        v[i] = precheck(argv[i], detail[i], sizeof detail[i]);
        if (v[i] == 3) { need[nneed] = argv[i]; idx[nneed++] = i; }
    }
    if (nneed) {
        clamscan_many(solo, need, nneed, vv, dd);
        for (int j = 0; j < nneed; j++) { v[idx[j]] = vv[j]; snprintf(detail[idx[j]], 300, "%.290s", dd[j]); }
    }
    for (int i = 0; i < argc; i++) {
        print_result(argv[i], v[i], detail[i]);
        counts[v[i] == 1 ? 1 : v[i] == 2 ? 2 : 0]++;
        if (v[i] == 1) worst = 1;
        else if (v[i] == 2 && worst != 1) worst = 2;
    }
    return worst;
}

#define MAX_FILE (100L * 1024 * 1024)

static void collect(const char *path, char ***list, size_t *n, size_t *cap, int depth) {
    struct stat st;
    if (lstat(path, &st) != 0) {
        if (*n == *cap) { *cap = *cap ? *cap * 2 : 64; *list = realloc(*list, *cap * sizeof **list); }
        (*list)[(*n)++] = strdup(path);
        return;
    }
    if (S_ISDIR(st.st_mode)) {
        if (depth > 24) return;
        DIR *d = opendir(path);
        if (!d) return;
        struct dirent *e;
        while ((e = readdir(d))) {
            if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue;
            char child[PATH_MAX];
            snprintf(child, sizeof child, "%s/%s", path, e->d_name);
            collect(child, list, n, cap, depth + 1);
        }
        closedir(d);
        return;
    }
    if (S_ISREG(st.st_mode) && st.st_size <= MAX_FILE) {
        if (*n == *cap) { *cap = *cap ? *cap * 2 : 64; *list = realloc(*list, *cap * sizeof **list); }
        (*list)[(*n)++] = strdup(path);
    }
}

/* Files and folders. Folders are walked; the files go to the scanner in groups. */
static int cmd_scan(int argc, char **argv) {
    if (argc < 1) { fprintf(stderr, "usage: snapguard scan <file or folder> [...]\n"); return 2; }
    char **list = NULL;
    size_t n = 0, cap = 0;
    for (int i = 0; i < argc; i++) collect(argv[i], &list, &n, &cap, 0);
    long counts[3] = { 0, 0, 0 };
    int worst = 0;
    for (size_t i = 0; i < n; i += MANY_MAX) {
        int m = (int)(n - i < MANY_MAX ? n - i : MANY_MAX);
        int w = scan_files(m, list + i, counts);
        if (w == 1) worst = 1;
        else if (w == 2 && worst != 1) worst = 2;
    }
    if (n > 1)
        printf("\n  %ld file%s checked: %s%ld threat%s%s, %ld could not be checked\n", (long)n, n == 1 ? "" : "s",
               counts[1] ? RED : GRN, counts[1], counts[1] == 1 ? "" : "s", R, counts[2]);
    for (size_t i = 0; i < n; i++) free(list[i]);
    free(list);
    return worst;
}

static void cmd_status(void) {
    char sigs[512], allow[512], quarantine[512];
    get_paths(sigs, allow, quarantine, sizeof sigs);
    char dpath[512], spath[512];
    const char *clamd = find_program("clamdscan", dpath, sizeof dpath);
    const char *solo = find_program("clamscan", spath, sizeof spath);
    const char *bin = clamd ? clamd : solo;
    int running = clamd_running();
    int dbfiles = clam_db_files();

    printf("snapguard — SnapGuard, the SnapOS antivirus\n");
    if (bin) printf("  ClamAV engine : %spresent (%s)%s\n", GRN, bin, R);
    else     printf("  ClamAV engine : %snot installed — hash signatures only%s\n", YEL, R);
    if (running) printf("  ClamAV daemon : %srunning%s\n", GRN, R);
    else         printf("  ClamAV daemon : %snot running (each scan starts ClamAV by itself, slower)%s\n", YEL, R);
    if (dbfiles > 0) printf("  virus database: %s%d files in %s%s\n", GRN, dbfiles, clam_db_dir(), R);
    else             printf("  virus database: %sMISSING in %s (downloads on first boot with internet)%s\n", YEL, clam_db_dir(), R);
    if (bin && dbfiles > 0) printf("  protection    : %sACTIVE%s (ClamAV and SnapOS signatures)\n", GRN, R);
    else                    printf("  protection    : %sLIMITED%s (SnapOS signatures only)\n", YEL, R);
    char pidfile[512];
    const char *rt = getenv("XDG_RUNTIME_DIR");
    if (rt && *rt) snprintf(pidfile, sizeof pidfile, "%s/snapguard-watch.pid", rt);
    else snprintf(pidfile, sizeof pidfile, "/tmp/snapguard-watch-%d.pid", (int)getuid());
    int watching = 0;
    FILE *pf = fopen(pidfile, "r");
    if (pf) { int pid = 0; if (fscanf(pf, "%d", &pid) == 1 && pid > 0 && kill(pid, 0) == 0) watching = 1; fclose(pf); }
    if (watching) printf("  real-time     : %son%s (new files in Downloads and on USB drives are scanned)\n", GRN, R);
    else          printf("  real-time     : %soff%s (snapguard-watch starts at login)\n", YEL, R);
    printf("  signatures    : %s\n", sigs);
    printf("  allowlist     : %s\n", allow);
    printf("  quarantine    : %s\n", quarantine);
}

static int cmd_trust(int argc, char **argv) {
    if (argc < 1) { fprintf(stderr, "usage: snapguard trust <path>\n"); return 2; }
    char digest[65];
    if (sha256_file(argv[0], digest) != 0) { fprintf(stderr, "no such file: %s\n", argv[0]); return 2; }
    char sigs[512], allow[512], quarantine[512];
    get_paths(sigs, allow, quarantine, sizeof sigs);
    FILE *f = fopen(allow, "a");
    if (!f) { fprintf(stderr, "cannot write %s: %s\n", allow, strerror(errno)); return 2; }
    const char *base = strrchr(argv[0], '/'); base = base ? base + 1 : argv[0];
    fprintf(f, "%s  %s\n", digest, base); fclose(f);
    printf("%strusted: %s  (%s)%s\n", GRN, digest, base, R);
    return 0;
}

static void mkdir_p(const char *dir) {
    char tmp[1024]; snprintf(tmp, sizeof tmp, "%s", dir);
    for (char *p = tmp + 1; *p; p++) if (*p == '/') { *p = 0; mkdir(tmp, 0755); *p = '/'; }
    mkdir(tmp, 0755);
}

static int cmd_quarantine(int argc, char **argv) {
    if (argc < 1) { fprintf(stderr, "usage: snapguard quarantine <path>\n"); return 2; }
    char src[PATH_MAX]; if (!realpath(argv[0], src)) { fprintf(stderr, "no such file: %s\n", argv[0]); return 2; }
    char digest[65]; if (sha256_file(src, digest) != 0) { fprintf(stderr, "unreadable: %s\n", src); return 2; }
    char sigs[512], allow[512], quarantine[512]; get_paths(sigs, allow, quarantine, sizeof sigs);
    mkdir_p(quarantine);
    const char *base = strrchr(src, '/'); base = base ? base + 1 : src;
    char basecut[400]; snprintf(basecut, sizeof basecut, "%.380s", base);
    char name[600], dst[1200];
    snprintf(name, sizeof name, "%.400s.%.12s", basecut, digest);
    snprintf(dst, sizeof dst, "%s/%s", quarantine, name);
    if (rename(src, dst) != 0) { fprintf(stderr, "could not contain %s: %s\n", src, strerror(errno)); return 2; }
    chmod(dst, 0600);
    char idx[1250]; snprintf(idx, sizeof idx, "%s/index.tsv", quarantine);
    FILE *f = fopen(idx, "a");
    if (f) { fprintf(f, "%s\t%s\t%s\t%ld\n", name, src, digest, (long)time(NULL)); fclose(f); }
    printf("%scontained: %s%s  -> quarantine/%s\n", YEL, src, R, name);
    printf("%snothing was deleted. keep+trust: snapguard restore %s --trust   |   delete: snapguard delete %s%s\n", DIM, name, name, R);
    return 0;
}

static int index_lookup(const char *quarantine, const char *name, char *orig, size_t n, char *digest, size_t dn) {
    char idx[1250]; snprintf(idx, sizeof idx, "%s/index.tsv", quarantine);
    FILE *f = fopen(idx, "r"); if (!f) return 0;
    char line[2048]; int found = 0;
    while (fgets(line, sizeof line, f)) {
        char *n_ = strtok(line, "\t");
        char *o_ = strtok(NULL, "\t");
        char *d_ = strtok(NULL, "\t");
        if (n_ && !strcmp(n_, name)) { snprintf(orig, n, "%s", o_ ? o_ : ""); snprintf(digest, dn, "%s", d_ ? d_ : ""); found = 1; break; }
    }
    fclose(f);
    return found;
}

static void index_remove(const char *quarantine, const char *name) {
    char idx[1250], tmp[1260];
    snprintf(idx, sizeof idx, "%s/index.tsv", quarantine);
    snprintf(tmp, sizeof tmp, "%s.tmp", idx);
    FILE *in = fopen(idx, "r"); if (!in) return;
    FILE *out = fopen(tmp, "w"); if (!out) { fclose(in); return; }
    char line[2048], copy[2048];
    while (fgets(line, sizeof line, in)) {
        snprintf(copy, sizeof copy, "%s", line);
        char *n_ = strtok(copy, "\t");
        if (n_ && !strcmp(n_, name)) continue;
        fputs(line, out);
    }
    fclose(in); fclose(out);
    rename(tmp, idx);
}

static int cmd_restore(int argc, char **argv) {
    if (argc < 1) { fprintf(stderr, "usage: snapguard restore <name> [--trust]\n"); return 2; }
    const char *name = argv[0];
    int trust = (argc > 1 && !strcmp(argv[1], "--trust"));
    char sigs[512], allow[512], quarantine[512]; get_paths(sigs, allow, quarantine, sizeof sigs);
    char orig[1024], digest[65];
    if (!index_lookup(quarantine, name, orig, sizeof orig, digest, sizeof digest)) {
        fprintf(stderr, "not in quarantine: %s\n", name); return 2;
    }
    char src[1300]; snprintf(src, sizeof src, "%s/%s", quarantine, name);
    char origdir[1024]; snprintf(origdir, sizeof origdir, "%s", orig);
    char *slash = strrchr(origdir, '/'); if (slash) { *slash = 0; mkdir_p(origdir); }
    if (rename(src, orig) != 0) { fprintf(stderr, "restore failed: %s\n", strerror(errno)); return 2; }
    chmod(orig, 0755);
    index_remove(quarantine, name);
    printf("%srestored: %s%s\n", GRN, orig, R);
    if (trust) {
        FILE *f = fopen(allow, "a");
        if (f) { const char *base = strrchr(orig, '/'); base = base ? base + 1 : orig;
                  fprintf(f, "%s  %s (kept by user)\n", digest, base); fclose(f);
                  printf("%swhitelisted: %s%s\n", GRN, digest, R); }
    }
    return 0;
}

static int cmd_delete(int argc, char **argv) {
    if (argc < 1) { fprintf(stderr, "usage: snapguard delete <name>\n"); return 2; }
    char sigs[512], allow[512], quarantine[512]; get_paths(sigs, allow, quarantine, sizeof sigs);
    char path[1300]; snprintf(path, sizeof path, "%s/%s", quarantine, argv[0]);
    if (unlink(path) != 0) { fprintf(stderr, "not in quarantine: %s\n", argv[0]); return 2; }
    index_remove(quarantine, argv[0]);
    printf("%sdeleted from quarantine: %s%s\n", RED, argv[0], R);
    return 0;
}

static void cmd_list_quarantine(void) {
    char sigs[512], allow[512], quarantine[512]; get_paths(sigs, allow, quarantine, sizeof sigs);
    char idx[1250]; snprintf(idx, sizeof idx, "%s/index.tsv", quarantine);
    FILE *f = fopen(idx, "r");
    if (!f) { printf("%squarantine is empty.%s\n", DIM, R); return; }
    char line[2048]; int any = 0;
    while (fgets(line, sizeof line, f)) {
        char *n_ = strtok(line, "\t"); char *o_ = strtok(NULL, "\t");
        if (!n_) continue;
        char qp[1300]; snprintf(qp, sizeof qp, "%s/%s", quarantine, n_);
        if (access(qp, F_OK) != 0) continue;
        any = 1;
        printf("  %s\n      was: %s\n", n_, o_ ? o_ : "?");
    }
    fclose(f);
    if (!any) printf("%squarantine is empty.%s\n", DIM, R);
}

static const char *help =
"snapguard -- SnapGuard, the SnapOS antivirus\n"
"\n"
"  snapguard                       open the window\n"
"  snapguard scan <path>...        scan files or folders\n"
"  snapguard status                is the antivirus working?\n"
"  snapguard quarantine <path>     contain a file\n"
"  snapguard list                  show what is in quarantine\n"
"  snapguard restore <name> [--trust]   put a contained file back\n"
"  snapguard delete <name>         delete a contained file for good\n"
"  snapguard trust <path>          always trust this file\n";

/* Starts the SnapGuard window. Returns only if it could not. */
static int open_window(int quiet) {
    char path[PATH_MAX + 64], self[PATH_MAX];
    if (!getenv("DISPLAY") && !getenv("WAYLAND_DISPLAY")) {
        if (!quiet) fprintf(stderr, "snapguard: there is no graphical session to open the window in\n");
        return 2;
    }
    if (find_program("snapguard-gui", path, sizeof path)) { execv(path, (char *[]){ path, NULL }); }
    ssize_t k = readlink("/proc/self/exe", self, sizeof self - 1);
    if (k > 0) {
        self[k] = 0;
        char *slash = strrchr(self, '/');
        if (slash) {
            *slash = 0;
            snprintf(path, sizeof path, "%s/snapguard-gui", self);
            if (access(path, X_OK) == 0) execv(path, (char *[]){ path, NULL });
        }
    }
    if (!quiet) fprintf(stderr, "snapguard: the window (snapguard-gui) is not installed\n");
    return 2;
}

int main(int argc, char **argv) {
    colors_init();
    if (argc < 2) {
        if (open_window(1) == 2) printf("%s", help);
        return 0;
    }
    const char *cmd = argv[1];
    if (!strcmp(cmd, "-h") || !strcmp(cmd, "--help") || !strcmp(cmd, "help")) { printf("%s", help); return 0; }
    if (!strcmp(cmd, "gui") || !strcmp(cmd, "window")) return open_window(0);
    if (!strcmp(cmd, "scan"))            return cmd_scan(argc - 2, argv + 2);
    if (!strcmp(cmd, "status"))          { cmd_status(); return 0; }
    if (!strcmp(cmd, "trust"))           return cmd_trust(argc - 2, argv + 2);
    if (!strcmp(cmd, "quarantine") || !strcmp(cmd, "quarentine")) return cmd_quarantine(argc - 2, argv + 2);
    if (!strcmp(cmd, "restore"))         return cmd_restore(argc - 2, argv + 2);
    if (!strcmp(cmd, "delete"))          return cmd_delete(argc - 2, argv + 2);
    if (!strcmp(cmd, "list") || !strcmp(cmd, "list-quarantine")) { cmd_list_quarantine(); return 0; }
    fprintf(stderr, "snapguard: unknown command '%s' (try: snapguard help)\n", cmd);
    return 2;
}
