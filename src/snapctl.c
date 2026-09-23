#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/wait.h>

static const char *nixdir(void) {
    const char *d = getenv("SNAPOS_NIX_DIR");
    if (d) return d;
    if (access("/etc/snapos/configuration.nix", F_OK) == 0) return "/etc/snapos";
    if (access("/etc/nixos/configuration.nix", F_OK) == 0) return "/etc/nixos";
    return "nix";
}
static char *joinp(const char *a, const char *b) {
    size_t n = strlen(a) + strlen(b) + 2;
    char *s = malloc(n);
    snprintf(s, n, "%s/%s", a, b);
    return s;
}

typedef struct { char **v; size_t n, cap; } List;
static void l_init(List *l) { l->v = NULL; l->n = l->cap = 0; }
static int  l_has(List *l, const char *s) {
    for (size_t i = 0; i < l->n; i++) if (strcmp(l->v[i], s) == 0) return 1;
    return 0;
}
static void l_add(List *l, const char *s) {
    if (l_has(l, s)) return;
    if (l->n == l->cap) { l->cap = l->cap ? l->cap * 2 : 8; l->v = realloc(l->v, l->cap * sizeof *l->v); }
    l->v[l->n++] = strdup(s);
}
static int  cmpstr(const void *a, const void *b) { return strcmp(*(char *const *)a, *(char *const *)b); }
static void l_sort(List *l) { qsort(l->v, l->n, sizeof *l->v, cmpstr); }

static char *read_file(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END); long sz = ftell(f); fseek(f, 0, SEEK_SET);
    if (sz < 0) { fclose(f); return NULL; }
    char *buf = malloc(sz + 1);
    size_t rd = fread(buf, 1, sz, f); buf[rd] = 0;
    fclose(f);
    return buf;
}
#define BEGIN "# snapos:packages:begin"
#define END   "# snapos:packages:end"

static void parse_declared(const char *cfg, List *out) {
    if (!cfg) return;
    const char *b = strstr(cfg, BEGIN);
    const char *e = strstr(cfg, END);
    if (!b || !e || e < b) return;
    const char *p = strchr(b, '[');
    if (!p || p > e) return;
    p++;
    char tok[256]; size_t ti = 0;
    for (; p < e && *p != ']'; p++) {
        if (isalnum((unsigned char)*p) || strchr("._+-", *p)) {
            if (ti < sizeof tok - 1) tok[ti++] = *p;
        } else if (ti) {
            tok[ti] = 0; ti = 0;
            if (strcmp(tok, "with") && strcmp(tok, "pkgs")) l_add(out, tok);
        }
    }
    if (ti) { tok[ti] = 0; if (strcmp(tok, "with") && strcmp(tok, "pkgs")) l_add(out, tok); }
}

static void read_pending(List *out) {
    char *pp = joinp(nixdir(), "pending");
    char *txt = read_file(pp); free(pp);
    if (!txt) return;
    char *save, *line = strtok_r(txt, "\n", &save);
    while (line) {
        while (*line == ' ' || *line == '\t') line++;
        char *end = line + strlen(line);
        while (end > line && (end[-1] == ' ' || end[-1] == '\t' || end[-1] == '\r')) *--end = 0;
        if (*line && *line != '#') l_add(out, line);
        line = strtok_r(NULL, "\n", &save);
    }
    free(txt);
}
static void write_pending(List *l) {
    char *pp = joinp(nixdir(), "pending");
    FILE *f = fopen(pp, "wb"); free(pp);
    if (!f) return;
    for (size_t i = 0; i < l->n; i++) fprintf(f, "%s\n", l->v[i]);
    fclose(f);
}

static int write_declared(List *decl) {
    char *cp = joinp(nixdir(), "configuration.nix");
    char *cfg = read_file(cp);
    if (!cfg) { fprintf(stderr, "snapctl: cannot read %s\n", cp); free(cp); return -1; }
    char *b = strstr(cfg, BEGIN), *e = strstr(cfg, END);
    if (!b || !e) { fprintf(stderr, "snapctl: managed markers not found\n"); free(cfg); free(cp); return -1; }
    char *after_begin = strchr(b, '\n'); if (!after_begin) { free(cfg); free(cp); return -1; }
    after_begin++;
    char *end_line = e;
    while (end_line > cfg && end_line[-1] != '\n') end_line--;

    l_sort(decl);
    FILE *f = fopen(cp, "wb");
    if (!f) { free(cfg); free(cp); return -1; }
    fwrite(cfg, 1, (size_t)(after_begin - cfg), f);
    fputs("  environment.systemPackages = with pkgs; [\n", f);
    for (size_t i = 0; i < decl->n; i++) fprintf(f, "    %s\n", decl->v[i]);
    fputs("  ];\n", f);
    fputs(end_line, f);
    fclose(f);
    free(cfg); free(cp);
    return 0;
}

static int find_in_path(const char *prog, char *out, size_t outsz) {
    if (strchr(prog, '/')) { if (access(prog, X_OK) == 0) { snprintf(out, outsz, "%s", prog); return 1; } return 0; }
    const char *path = getenv("PATH"); if (!path) return 0;
    char *dup = strdup(path), *save, *dir = strtok_r(dup, ":", &save);
    int found = 0;
    while (dir) {
        snprintf(out, outsz, "%s/%s", dir, prog);
        if (access(out, X_OK) == 0) { found = 1; break; }
        dir = strtok_r(NULL, ":", &save);
    }
    free(dup);
    return found;
}

static int guard_scan(const char *exe) {
    char guard[512];
    const char *g = getenv("SNAP_GUARD");
    if (g) snprintf(guard, sizeof guard, "%s", g);
    else if (!find_in_path("snapguard", guard, sizeof guard)) return 0;
    pid_t pid = fork();
    if (pid == 0) {
        int nul = open("/dev/null", O_WRONLY);
        if (nul >= 0) { dup2(nul, 1); }
        execl(guard, guard, "scan", exe, (char *)NULL);
        _exit(127);
    }
    int st; waitpid(pid, &st, 0);
    return (WIFEXITED(st) && WEXITSTATUS(st) == 1) ? 1 : 0;
}

static const char *cR = "", *cB = "", *cDIM = "", *cRED = "", *cGRN = "", *cYEL = "", *cCYN = "";
static void colors_init(void) {
    if (!isatty(1)) return;
    cR = "\033[0m"; cB = "\033[1m"; cDIM = "\033[2m";
    cRED = "\033[31m"; cGRN = "\033[32m"; cYEL = "\033[33m"; cCYN = "\033[36m";
}

static void cmd_status(void) {
    List decl; l_init(&decl); char *cfg = read_file(joinp(nixdir(), "configuration.nix"));
    parse_declared(cfg, &decl); free(cfg);
    List pend; l_init(&pend); read_pending(&pend);
    l_sort(&decl);
    printf("%sSnapOS%s  %s(declarative)%s\n\n", cB, cR, cDIM, cR);
    printf("%sDeclared (%zu):%s\n", cGRN, decl.n, cR);
    for (size_t i = 0; i < decl.n; i++) printf("  %s✓%s %s\n", cGRN, cR, decl.v[i]);
    printf("\n");
    if (pend.n) {
        printf("%sPending — drafted, not declared (%zu):%s\n", cYEL, pend.n, cR);
        for (size_t i = 0; i < pend.n; i++) printf("  %s+%s %s\n", cYEL, cR, pend.v[i]);
        printf("\n  %s→ run `snapctl save` to declare these.%s\n", cDIM, cR);
    } else {
        printf("%sNothing pending. System matches its declaration.%s\n", cDIM, cR);
    }
}

static int cmd_run(int argc, char **argv) {
    if (argc < 1) { fprintf(stderr, "usage: snapctl run <prog> [args]\n"); return 2; }
    const char *prog = argv[0];
    List decl; l_init(&decl); char *cfg = read_file(joinp(nixdir(), "configuration.nix"));
    parse_declared(cfg, &decl); free(cfg);
    List pend; l_init(&pend); read_pending(&pend);

    int declared = l_has(&decl, prog);
    char exe[512];
    int have = find_in_path(prog, exe, sizeof exe);

    if (have && !declared) {
        if (guard_scan(exe)) {
            fprintf(stderr, "%ssnapos: ⛔ THREAT contained — '%s' was NOT run.%s\n", cRED, prog, cR);
            return 1;
        }
    }
    if (!declared && !l_has(&pend, prog)) {
        l_add(&pend, prog); write_pending(&pend);
        printf("%ssnapos:%s '%s' was not declared. %sDrafted.%s %sRun `snapctl save` to keep it.%s\n",
               cCYN, cR, prog, cCYN, cR, cDIM, cR);
    }
    if (!have) { fprintf(stderr, "snapctl: '%s' not found on PATH\n", prog); return 127; }
    execvp(prog, argv);
    fprintf(stderr, "snapctl: exec %s: %s\n", prog, strerror(errno));
    return 127;
}

static int cmd_save(void) {
    List decl; l_init(&decl); char *cfg = read_file(joinp(nixdir(), "configuration.nix"));
    parse_declared(cfg, &decl); free(cfg);
    List pend; l_init(&pend); read_pending(&pend);
    if (!pend.n) { printf("%sNothing to save — no pending drafts.%s\n", cDIM, cR); return 0; }
    for (size_t i = 0; i < pend.n; i++) l_add(&decl, pend.v[i]);
    if (write_declared(&decl) != 0) return 1;
    List empty; l_init(&empty); write_pending(&empty);
    printf("%sSaved %zu program(s) into the declaration:%s\n", cGRN, pend.n, cR);
    for (size_t i = 0; i < pend.n; i++) printf("  %s✓%s %s\n", cGRN, cR, pend.v[i]);
    return 0;
}

static int cmd_discard(int argc, char **argv) {
    List pend; l_init(&pend); read_pending(&pend);
    if (!pend.n) { printf("%sNo pending drafts.%s\n", cDIM, cR); return 0; }
    if (argc >= 1 && strcmp(argv[0], "--all") != 0) {
        List keep; l_init(&keep); int found = 0;
        for (size_t i = 0; i < pend.n; i++) { if (strcmp(pend.v[i], argv[0]) == 0) found = 1; else l_add(&keep, pend.v[i]); }
        write_pending(&keep);
        if (found) printf("%sDiscarded draft: %s%s\n", cYEL, argv[0], cR);
        else       printf("not pending: %s\n", argv[0]);
    } else {
        List empty; l_init(&empty); size_t n = pend.n; write_pending(&empty);
        printf("%sDiscarded %zu pending draft(s).%s\n", cYEL, n, cR);
    }
    return 0;
}

static void cmd_list(void) {
    List decl; l_init(&decl); char *cfg = read_file(joinp(nixdir(), "configuration.nix"));
    parse_declared(cfg, &decl); free(cfg); l_sort(&decl);
    for (size_t i = 0; i < decl.n; i++) printf("%s\n", decl.v[i]);
}

static void cmd_diff(void) {
    List pend; l_init(&pend); read_pending(&pend);
    if (!pend.n) { printf("%sNo changes. `save` would do nothing.%s\n", cDIM, cR); return; }
    printf("`snapctl save` would add to declared:\n");
    for (size_t i = 0; i < pend.n; i++) printf("%s  + %s%s\n", cGRN, pend.v[i], cR);
}

int main(int argc, char **argv) {
    colors_init();
    if (argc < 2 || !strcmp(argv[1], "-h") || !strcmp(argv[1], "--help")) {
        printf("snapctl — SnapOS declarative controller\n"
               "  status | run <prog> [args] | save | discard [name|--all] | list | diff\n");
        return 0;
    }
    const char *cmd = argv[1];
    if (!strcmp(cmd, "status"))  { cmd_status(); return 0; }
    if (!strcmp(cmd, "run"))     return cmd_run(argc - 2, argv + 2);
    if (!strcmp(cmd, "save"))    return cmd_save();
    if (!strcmp(cmd, "discard")) return cmd_discard(argc - 2, argv + 2);
    if (!strcmp(cmd, "list"))    { cmd_list(); return 0; }
    if (!strcmp(cmd, "diff"))    { cmd_diff(); return 0; }
    fprintf(stderr, "snapctl: unknown command '%s'\n", cmd);
    return 2;
}
