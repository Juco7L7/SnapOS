#define _POSIX_C_SOURCE 200809L
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

#define MAX_COLS 400
#define FRAME_NS 55000000L
#define MAX_FRAMES 260

static const char *WORDS[] = { "larp", "nullsec", "opsec", "larp", "nullsec", "snapos", "root", "0day" };
#define NWORDS (sizeof WORDS / sizeof WORDS[0])

static const char GLYPHS[] = "01abcdefghijklmnopqrstuvwxyz$#%&*+=<>/\\|";

static struct termios saved;
static int raw_on;

static void restore_terminal(void) {
    static const char seq[] = "\033[0m\033[?25h\033[?1049l";
    if (raw_on) tcsetattr(STDIN_FILENO, TCSANOW, &saved);
    if (write(STDOUT_FILENO, seq, sizeof seq - 1) < 0) return;
}

static void on_signal(int sig) {
    (void)sig;
    restore_terminal();
    _exit(130);
}

typedef struct {
    int y;
    int period;
    int len;
    const char *word;
    int wpos;
} Column;

static int rnd(int lo, int hi) {
    return lo + rand() % (hi - lo + 1);
}

static void respawn(Column *c, int rows, int fresh) {
    c->y = fresh ? -rnd(0, rows) : -rnd(1, rows / 2);
    c->period = rnd(1, 3);
    c->len = rnd(6, rows > 14 ? 16 : rows - 2);
    c->word = rnd(0, 5) == 0 ? WORDS[rnd(0, (int)NWORDS - 1)] : NULL;
    c->wpos = 0;
}

static char next_glyph(Column *c) {
    if (c->word && c->word[c->wpos]) return c->word[c->wpos++];
    c->word = NULL;
    return GLYPHS[rand() % (int)(sizeof GLYPHS - 1)];
}

static int append(char *buf, int used, int cap, const char *fmt, int a, int b, const char *tail) {
    int n = snprintf(buf + used, (size_t)(cap - used), fmt, a, b, tail);
    return n > 0 && used + n < cap ? used + n : used;
}

static void rain(void) {
    struct winsize ws;
    int cols = 80, rows = 24;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0 && ws.ws_col > 0 && ws.ws_row > 0) {
        cols = ws.ws_col;
        rows = ws.ws_row;
    }
    if (cols > MAX_COLS) cols = MAX_COLS;
    if (rows < 8) rows = 8;

    Column col[MAX_COLS];
    for (int i = 0; i < cols; i++) respawn(&col[i], rows, 1);

    struct sigaction sa;
    memset(&sa, 0, sizeof sa);
    sa.sa_handler = on_signal;
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);

    if (isatty(STDIN_FILENO) && tcgetattr(STDIN_FILENO, &saved) == 0) {
        struct termios raw = saved;
        raw.c_lflag &= (tcflag_t) ~(ICANON | ECHO);
        raw.c_cc[VMIN] = 0;
        raw.c_cc[VTIME] = 0;
        tcsetattr(STDIN_FILENO, TCSANOW, &raw);
        raw_on = 1;
    }

    printf("\033[?1049h\033[?25l\033[2J");
    fflush(stdout);

    static char buf[1 << 17];
    for (int frame = 0; frame < MAX_FRAMES; frame++) {
        int used = 0;
        for (int x = 0; x < cols; x++) {
            Column *c = &col[x];
            if (frame % c->period) continue;
            c->y++;
            if (c->y >= 1 && c->y <= rows) {
                used = append(buf, used, (int)sizeof buf, "\033[%d;%dH\033[1;38;5;217m%s", c->y, x + 1, (char[]){ next_glyph(c), 0 });
                if (c->y > 1)
                    used = append(buf, used, (int)sizeof buf, "\033[%d;%dH\033[38;5;160m%s", c->y - 1, x + 1, (char[]){ GLYPHS[rand() % (int)(sizeof GLYPHS - 1)], 0 });
            }
            int mid = c->y - c->len / 2;
            if (mid >= 1 && mid <= rows)
                used = append(buf, used, (int)sizeof buf, "\033[%d;%dH\033[38;5;88m%s", mid, x + 1, (char[]){ GLYPHS[rand() % (int)(sizeof GLYPHS - 1)], 0 });
            int tail = c->y - c->len;
            if (tail >= 1 && tail <= rows)
                used = append(buf, used, (int)sizeof buf, "\033[%d;%dH\033[0m%s", tail, x + 1, " ");
            if (c->y - c->len > rows) respawn(c, rows, 0);
        }
        if (write(STDOUT_FILENO, buf, (size_t)used) < 0) break;

        struct timespec ts = { 0, FRAME_NS };
        nanosleep(&ts, NULL);
        if (raw_on) {
            struct pollfd p = { STDIN_FILENO, POLLIN, 0 };
            if (poll(&p, 1, 0) > 0) break;
        }
    }
    restore_terminal();
    raw_on = 0;
}

static void hint(const char *what) {
    fprintf(stderr,
        "apt: SnapOS is declarative, so there is no apt.\n"
        "  Install a program:   add '%s' to your configuration (snapos config), then snapos rebuild\n"
        "  Or draft it:         snapctl run %s, then snapctl save\n"
        "  Debian packages:     dpkg -c file.deb  (inspect)   dpkg-deb -x file.deb dir  (unpack)\n",
        what, what);
}

int main(int argc, char **argv) {
    const char *cmd = NULL;
    const char *first_pkg = NULL;
    int opsec = 0;
    for (int i = 1; i < argc; i++) {
        if (argv[i][0] == '-') continue;
        if (!cmd) { cmd = argv[i]; continue; }
        if (!first_pkg) first_pkg = argv[i];
        if (!strcasecmp(argv[i], "opsec")) opsec = 1;
    }

    int installing = cmd && (!strcmp(cmd, "install") || !strcmp(cmd, "instal") || !strcmp(cmd, "add"));
    if (installing && opsec) {
        srand((unsigned)time(NULL) ^ (unsigned)getpid());
        if (isatty(STDOUT_FILENO)) rain();
        return 0;
    }
    if (installing && first_pkg) {
        hint(first_pkg);
        return 1;
    }
    fprintf(stderr, "apt: SnapOS is declarative. Use snapos config and snapos rebuild, or snapctl.\n");
    return 1;
}
