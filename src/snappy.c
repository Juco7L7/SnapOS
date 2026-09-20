#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <sys/stat.h>

static const char *R = "", *B = "", *DIM = "", *RED = "";
static int tty;
static void colors_init(void) {
    tty = isatty(1);
    if (!tty) return;
    R = "\033[0m"; B = "\033[1m"; DIM = "\033[2m"; RED = "\033[91m";
}

typedef struct { long xp; } State;

static void mkdir_p(const char *dir) {
    char tmp[1024]; snprintf(tmp, sizeof tmp, "%s", dir);
    for (char *p = tmp + 1; *p; p++) if (*p == '/') { *p = 0; mkdir(tmp, 0755); *p = '/'; }
    mkdir(tmp, 0755);
}

static char *state_path(void) {
    const char *xdg = getenv("XDG_DATA_HOME");
    const char *home = getenv("HOME");
    static char buf[1024];
    if (xdg && *xdg) snprintf(buf, sizeof buf, "%s/snapos", xdg);
    else snprintf(buf, sizeof buf, "%s/.local/share/snapos", home ? home : ".");
    mkdir_p(buf);
    static char full[1200];
    snprintf(full, sizeof full, "%s/snappy.state", buf);
    return full;
}

static State load(void) {
    State s = { .xp = 0 };
    FILE *f = fopen(state_path(), "r");
    if (!f) return s;
    char key[32]; long val;
    while (fscanf(f, "%31s %ld", key, &val) == 2) {
        if (!strcmp(key, "xp")) s.xp = val;
    }
    fclose(f);
    return s;
}

static void save(State *s) {
    FILE *f = fopen(state_path(), "w");
    if (!f) return;
    fprintf(f, "xp %ld\n", s->xp);
    fclose(f);
}

static long mem_available_mb(void) {
    FILE *f = fopen("/proc/meminfo", "r");
    if (!f) return 512;
    char line[256]; long kb = 512 * 1024;
    while (fgets(line, sizeof line, f)) {
        if (!strncmp(line, "MemAvailable:", 13)) { sscanf(line + 13, "%ld", &kb); break; }
    }
    fclose(f);
    return kb / 1024;
}

#define SPRITE_ROWS 8

static void print_snappy(const char *eye, const char *mouth) {
    printf("%s        ◢▲◣   ◢▲◣   ◢▲◣%s\n", RED, R);
    printf("%s      ◢████████████████████◣%s\n", RED, R);
    printf("%s     ▟ ▒▓▒▓▒▓▒▓▒▓▒▓▒▓▒▓▒▓ ▙%s\n", RED, R);
    printf("%s    ▐  ╲__________________╱  ▌%s\n", RED, R);
    printf("%s    ▐    ╭╮  %s      %s  ╭╮    ▌%s\n", RED, eye, eye, R);
    printf("%s     ╲       ╲ %s ╱       ╱%s\n", RED, mouth, R);
    printf("%s      ▜▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▛%s\n", RED, R);
    printf("%s        ╨      ╨      ╾▶▶%s\n", RED, R);
}

static void nap(long ms) {
    struct timespec ts = { ms / 1000, (ms % 1000) * 1000000L };
    nanosleep(&ts, NULL);
}

#define EYE_CLOSED "-"
static void print_snappy_blinking(const char *eye, const char *mouth) {
    print_snappy(eye, mouth);
    if (!tty) return;
    fflush(stdout);
    nap(900);
    for (int i = 0; i < 2; i++) {
        printf("\033[%dA", SPRITE_ROWS); print_snappy(EYE_CLOSED, mouth); fflush(stdout);
        nap(140);
        printf("\033[%dA", SPRITE_ROWS); print_snappy(eye, mouth); fflush(stdout);
        nap(i == 0 ? 320 : 0);
    }
}

static void face_for(const char *mood, const char **eye, const char **mouth) {
    if (!strcmp(mood, "happy"))       { *eye = "^"; *mouth = "╲▽▽▽▽╱"; }
    else if (!strcmp(mood, "hungry")) { *eye = "◉"; *mouth = "╲▁▁▁▁╱"; }
    else if (!strcmp(mood, "eat1"))   { *eye = "◉"; *mouth = "╲◤██◥╱"; }
    else if (!strcmp(mood, "eat2"))   { *eye = "-"; *mouth = "╲◣██◢╱"; }
    else if (!strcmp(mood, "full"))   { *eye = "◉"; *mouth = "╲◡◡◡◡╱"; }
    else                              { *eye = "◉"; *mouth = "╲▼▼▼▼╱"; }
}

static void show_status(State *s) {
    const char *eye, *mouth; face_for("idle", &eye, &mouth);
    printf("\n"); print_snappy_blinking(eye, mouth); printf("\n");
    printf("   %sSnappy%s\n", B, R);
    printf("   XP : %ld\n\n", s->xp);
}

static void feed(State *s) {
    long avail = mem_available_mb();
    long target = avail * 2 / 5; if (target < 64) target = 64; if (target > 700) target = 700;
    printf("\n   %sSnappy sniffs the air... %ld MB of RAM! \U0001f356%s\n\n", B, target, R);
    long chunk_mb = 32, eaten = 0;
    size_t nblocks = (target + chunk_mb - 1) / chunk_mb;
    void **blocks = calloc(nblocks, sizeof(void *));
    size_t nb = 0;
    while (eaten < target) {
        long take = target - eaten < chunk_mb ? target - eaten : chunk_mb;
        size_t bytes = (size_t)take * 1024 * 1024;
        void *p = malloc(bytes);
        if (!p) break;
        for (size_t off = 0; off < bytes; off += 4096) ((char *)p)[off] = 1;
        blocks[nb++] = p;
        eaten += take;
        int fill = (int)(20 * eaten / target);
        printf("\r   %snom%s [%s", RED, R, RED);
        for (int i = 0; i < fill; i++) printf("█");
        printf("%s", DIM); for (int i = fill; i < 20; i++) printf("·"); printf("%s] %ld MB   ", R, eaten);
        fflush(stdout);
        { struct timespec ts={0,120000000L}; nanosleep(&ts,NULL); }
    }
    printf("\r   %sNOM NOM NOM%s  ate %ld MB of RAM        \n\n", RED, R, eaten);
    const char *eyes, *mouth; face_for("eat2", &eyes, &mouth);
    print_snappy(eyes, mouth);
    { struct timespec ts={1,400000000L}; nanosleep(&ts,NULL); }
    for (size_t i = 0; i < nb; i++) free(blocks[i]);
    free(blocks);
    s->xp += 10 + eaten / 32;
    save(s);
    printf("\n");
    face_for("full", &eyes, &mouth); print_snappy_blinking(eyes, mouth);
    printf("\n   %s*burp* \U0001f60c  Snappy gave the %ld MB back. +%ld XP%s\n", RED, eaten, 10 + eaten / 32, R);
    printf("   %s(the RAM was only borrowed -- it's free again)%s\n\n", DIM, R);
}

static void pet(State *s) {
    s->xp += 2; save(s);
    printf("\n");
    const char *eyes, *mouth; face_for("happy", &eyes, &mouth);
    print_snappy_blinking(eyes, mouth);
    printf("\n   %sSnappy snaps happily. ♥  +2 XP%s\n\n", RED, R);
}

int main(int argc, char **argv) {
    colors_init();
    State s = load();
    const char *cmd = argc > 1 ? argv[1] : "status";
    if (!strcmp(cmd, "-h") || !strcmp(cmd, "--help") || !strcmp(cmd, "help")) {
        printf("snappy -- SnapOS's pet\n  snappy | snappy feed | snappy pet | snappy help\n");
    } else if (!strcmp(cmd, "feed") || !strcmp(cmd, "f")) {
        feed(&s);
    } else if (!strcmp(cmd, "pet") || !strcmp(cmd, "p")) {
        pet(&s);
    } else {
        show_status(&s);
    }
    save(&s);
    return 0;
}
