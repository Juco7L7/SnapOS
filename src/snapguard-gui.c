#define _GNU_SOURCE
#include <gtk/gtk.h>
#include <limits.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define CW 11.0
#define LH 20.0
#define BATCH 8
#define MAX_SIZE (100L * 1024 * 1024)
#define MAX_DEPTH 24

static const char *SPRITE[] = {
    "        ◢▲◣   ◢▲◣   ◢▲◣",
    "      ◢████████████████████◣",
    "     ▟ ▒▓▒▓▒▓▒▓▒▓▒▓▒▓▒▓▒▓ ▙",
    "    ▐  ╲__________________╱  ▌",
    "    ▐    ╭╮  ◉      ◉  ╭╮    ▌",
    "     ╲       ╲ ╲▼▼▼▼╱ ╱       ╱",
    "      ▜▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▛",
    "        ╨      ╨      ╾▶▶",
};
#define SPRITE_ROWS (sizeof SPRITE / sizeof SPRITE[0])

typedef enum { MOOD_IDLE, MOOD_SCAN, MOOD_CHOMP, MOOD_HAPPY, MOOD_SAD } Mood;

typedef struct {
    GtkWidget *win, *canvas, *headline, *speech, *engine, *bar, *current, *count;
    GtkWidget *btn_quick, *btn_folder, *btn_file, *btn_stop;
    GtkWidget *results, *quarantine, *stack;
    Mood mood;
    unsigned tick;
    unsigned chomp_left;
    int scanning;
    volatile int cancel;
    long threats, checked, total, unknown, skipped;
    gint64 started;
    int engine_ok;
    GMutex lock;
    GSubprocess *sub;
    char *guard;
} App;

static App app;

typedef enum { M_PROGRESS, M_THREAT, M_UNKNOWN, M_CLEAN, M_DONE } MsgKind;
typedef struct {
    MsgKind kind;
    long checked, total;
    char *a, *b;
} Msg;

static const char *EYE_OPEN = "◉";
static const char *EYE_SHUT = "-";

static void set_speech(const char *text) {
    gtk_label_set_text(GTK_LABEL(app.speech), text);
}

static void set_headline(const char *text) {
    gtk_label_set_text(GTK_LABEL(app.headline), text);
}

static void rect(cairo_t *cr, double x, double y, double w, double h) {
    cairo_rectangle(cr, x, y, w, h);
    cairo_fill(cr);
}

static void tri(cairo_t *cr, double x1, double y1, double x2, double y2, double x3, double y3) {
    cairo_move_to(cr, x1, y1);
    cairo_line_to(cr, x2, y2);
    cairo_line_to(cr, x3, y3);
    cairo_close_path(cr);
    cairo_fill(cr);
}

static void line(cairo_t *cr, double x1, double y1, double x2, double y2, double w) {
    cairo_set_line_width(cr, w);
    cairo_move_to(cr, x1, y1);
    cairo_line_to(cr, x2, y2);
    cairo_stroke(cr);
}

static void draw_cell(cairo_t *cr, gunichar ch, double x, double y) {
    double w = CW, h = LH, m = w / 2, q = h / 2;
    cairo_set_source_rgb(cr, 0.886, 0.165, 0.11);
    switch (ch) {
    case ' ': return;
    case 0x2588: rect(cr, x, y, w, h); break;
    case 0x2592: cairo_set_source_rgb(cr, 0.59, 0.11, 0.08); rect(cr, x, y, w, h); break;
    case 0x2593: cairo_set_source_rgb(cr, 0.75, 0.14, 0.10); rect(cr, x, y, w, h); break;
    case 0x2584: rect(cr, x, y + q, w, q); break;
    case 0x258C: rect(cr, x, y, m, h); break;
    case 0x2590: rect(cr, x + m, y, m, h); break;
    case 0x259F: rect(cr, x + m, y, m, q); rect(cr, x, y + q, w, q); break;
    case 0x2599: rect(cr, x, y, m, q); rect(cr, x, y + q, w, q); break;
    case 0x259C: rect(cr, x, y, w, q); rect(cr, x + m, y + q, m, q); break;
    case 0x259B: rect(cr, x, y, w, q); rect(cr, x, y + q, m, q); break;
    case 0x25E2: tri(cr, x + w, y, x + w, y + h, x, y + h); break;
    case 0x25E3: tri(cr, x, y, x, y + h, x + w, y + h); break;
    case 0x25E4: tri(cr, x, y, x + w, y, x, y + h); break;
    case 0x25E5: tri(cr, x, y, x + w, y, x + w, y + h); break;
    case 0x25B2: tri(cr, x + m, y, x + w, y + h, x, y + h); break;
    case 0x25BC: tri(cr, x, y, x + w, y, x + m, y + h); break;
    case 0x25BD:
        cairo_set_line_width(cr, 2);
        cairo_move_to(cr, x, y + 3); cairo_line_to(cr, x + w, y + 3);
        cairo_line_to(cr, x + m, y + h - 3); cairo_close_path(cr);
        cairo_stroke(cr);
        break;
    case 0x2581: rect(cr, x, y + h - 4, w, 3); break;
    case 0x25B6: tri(cr, x, y + 3, x + w, y + q, x, y + h - 3); break;
    case 0x2572: line(cr, x, y, x + w, y + h, 2); break;
    case 0x2571: line(cr, x + w, y, x, y + h, 2); break;
    case '_': line(cr, x, y + h - 3, x + w, y + h - 3, 2); break;
    case 0x25C9:
        cairo_set_line_width(cr, 2);
        cairo_arc(cr, x + m, y + q, 5.5, 0, 2 * G_PI);
        cairo_stroke(cr);
        cairo_arc(cr, x + m, y + q, 2.6, 0, 2 * G_PI);
        cairo_fill(cr);
        break;
    case '-': line(cr, x + 1, y + q, x + w - 1, y + q, 3); break;
    case '^':
        cairo_set_line_width(cr, 3);
        cairo_move_to(cr, x + 1, y + q + 4); cairo_line_to(cr, x + m, y + q - 4);
        cairo_line_to(cr, x + w - 1, y + q + 4);
        cairo_stroke(cr);
        break;
    case 0x25E1:
        cairo_set_line_width(cr, 2);
        cairo_arc(cr, x + m, y + q - 2, m, 0, G_PI);
        cairo_stroke(cr);
        break;
    case 0x256D:
        cairo_set_line_width(cr, 2);
        cairo_move_to(cr, x + m, y + h);
        cairo_curve_to(cr, x + m, y + q, x + m + 2, y + q, x + w, y + q);
        cairo_stroke(cr);
        break;
    case 0x256E:
        cairo_set_line_width(cr, 2);
        cairo_move_to(cr, x, y + q);
        cairo_curve_to(cr, x + m - 2, y + q, x + m, y + q, x + m, y + h);
        cairo_stroke(cr);
        break;
    case 0x2568:
        line(cr, x + 3, y, x + 3, y + q, 2);
        line(cr, x + w - 3, y, x + w - 3, y + q, 2);
        line(cr, x, y + q, x + w, y + q, 2);
        break;
    case 0x257E: line(cr, x, y + q, x + w, y + q, 3); break;
    default: break;
    }
}

static gboolean blinking(void) {
    unsigned t = app.tick;
    if (t == 8 || t == 9 || t == 15 || t == 16) return TRUE;
    return t > 30 && (t % 42) < 2;
}

static void face(const char **eye, const char **mouth) {
    *eye = blinking() ? EYE_SHUT : EYE_OPEN;
    *mouth = "╲▼▼▼▼╱";
    switch (app.mood) {
    case MOOD_SCAN: *mouth = "╲▁▁▁▁╱"; break;
    case MOOD_CHOMP:
        *mouth = (app.tick & 1) ? "╲◣██◢╱" : "╲◤██◥╱";
        if (!(app.tick & 1)) *eye = EYE_OPEN; else *eye = EYE_SHUT;
        break;
    case MOOD_HAPPY: *mouth = "╲▽▽▽▽╱"; *eye = blinking() ? EYE_SHUT : "^"; break;
    case MOOD_SAD: *mouth = "╲▁▁▁▁╱"; *eye = EYE_SHUT; break;
    default: break;
    }
}

static gboolean on_draw(GtkWidget *w, cairo_t *cr, gpointer data) {
    (void)data;
    int width = gtk_widget_get_allocated_width(w);
    double scale = 0.78;
    double sw = 32 * CW * scale;
    int height = gtk_widget_get_allocated_height(w);
    double ox = (width - sw) / 2, oy = (height - SPRITE_ROWS * LH * scale) / 2;
    if (app.mood == MOOD_SCAN) oy += (app.tick % 6 < 3) ? 0 : 2;
    if (app.mood == MOOD_CHOMP) oy += (app.tick & 1) ? 0 : -3;
    const char *eye, *mouth;
    face(&eye, &mouth);
    cairo_translate(cr, ox, oy);
    cairo_scale(cr, scale, scale);
    ox = 0;
    oy = 0;
    for (size_t r = 0; r < SPRITE_ROWS; r++) {
        GString *ln = g_string_new(SPRITE[r]);
        g_string_replace(ln, "◉", eye, 0);
        g_string_replace(ln, "╲▼▼▼▼╱", mouth, 0);
        double x = ox;
        for (const char *p = ln->str; *p; p = g_utf8_next_char(p)) {
            draw_cell(cr, g_utf8_get_char(p), x, oy + r * LH);
            x += CW;
        }
        g_string_free(ln, TRUE);
    }
    return FALSE;
}

static gboolean on_tick(gpointer data) {
    (void)data;
    app.tick++;
    if (app.mood == MOOD_CHOMP) {
        if (app.chomp_left > 0) app.chomp_left--;
        if (app.chomp_left == 0) app.mood = app.scanning ? MOOD_SCAN : MOOD_HAPPY;
    }
    gtk_widget_queue_draw(app.canvas);
    return G_SOURCE_CONTINUE;
}

static char *find_guard(void) {
    char *p = g_find_program_in_path("snapguard");
    if (p) return p;
    char self[PATH_MAX];
    ssize_t n = readlink("/proc/self/exe", self, sizeof self - 1);
    if (n > 0) {
        self[n] = 0;
        char *d = g_path_get_dirname(self);
        char *c = g_build_filename(d, "snapguard", NULL);
        g_free(d);
        if (access(c, X_OK) == 0) return c;
        g_free(c);
    }
    return g_strdup("snapguard");
}

static char *run_guard(char **argv, int *status) {
    char *out = NULL;
    GError *err = NULL;
    if (!g_spawn_sync(NULL, argv, NULL, G_SPAWN_STDERR_TO_DEV_NULL, NULL, NULL, &out, NULL, status, &err)) {
        if (err) g_error_free(err);
        if (status) *status = -1;
        return g_strdup("");
    }
    return out ? out : g_strdup("");
}

static void refresh_engine(void) {
    char *argv[] = { app.guard, "status", NULL };
    int st = 0;
    char *out = run_guard(argv, &st);
    app.engine_ok = strstr(out, "ACTIVE") != NULL;
    if (strstr(out, "ACTIVE"))
        gtk_label_set_text(GTK_LABEL(app.engine), "ClamAV engine and SnapOS signatures");
    else if (strstr(out, "present"))
        gtk_label_set_text(GTK_LABEL(app.engine), "SnapOS signatures (ClamAV is downloading its virus database)");
    else
        gtk_label_set_text(GTK_LABEL(app.engine), "SnapOS signatures only (ClamAV not found)");
    g_free(out);
}

static void post(MsgKind kind, long checked, long total, const char *a, const char *b);
static gboolean on_msg(gpointer data);

static void walk(const char *dir, GPtrArray *out, int depth) {
    if (app.cancel || depth > MAX_DEPTH) return;
    GDir *d = g_dir_open(dir, 0, NULL);
    if (!d) return;
    const char *name;
    while (!app.cancel && (name = g_dir_read_name(d))) {
        char *path = g_build_filename(dir, name, NULL);
        struct stat st;
        if (lstat(path, &st) == 0) {
            if (S_ISDIR(st.st_mode)) walk(path, out, depth + 1);
            else if (S_ISREG(st.st_mode) && st.st_size <= MAX_SIZE) { g_ptr_array_add(out, path); continue; }
            else if (S_ISREG(st.st_mode)) app.skipped++;
        }
        g_free(path);
    }
    g_dir_close(d);
}

static void contain(const char *path) {
    char *argv[] = { app.guard, "quarantine", (char *)path, NULL };
    int st;
    g_free(run_guard(argv, &st));
}

static const char *match_path(const char *line, char **files, guint n) {
    for (guint j = 0; j < n; j++) {
        char *pat = g_strdup_printf("  %s  ", files[j]);
        gboolean hit = strstr(line, pat) != NULL;
        g_free(pat);
        if (hit) return files[j];
    }
    return NULL;
}

/* Runs snapguard on a few files and reports each result as soon as it is
 * printed, so the window moves file by file. */
static void scan_batch(char **files, guint n, long *checked, long total) {
    GPtrArray *argv = g_ptr_array_new();
    g_ptr_array_add(argv, app.guard);
    g_ptr_array_add(argv, "scan");
    for (guint j = 0; j < n; j++) g_ptr_array_add(argv, files[j]);
    g_ptr_array_add(argv, NULL);

    GSubprocess *p = g_subprocess_newv((const gchar *const *)argv->pdata,
                                       G_SUBPROCESS_FLAGS_STDOUT_PIPE | G_SUBPROCESS_FLAGS_STDERR_SILENCE, NULL);
    g_ptr_array_free(argv, TRUE);
    gboolean *seen = g_new0(gboolean, n);

    if (p) {
        g_mutex_lock(&app.lock);
        app.sub = p;
        g_mutex_unlock(&app.lock);
        GDataInputStream *in = g_data_input_stream_new(g_subprocess_get_stdout_pipe(p));
        char *ln;
        while ((ln = g_data_input_stream_read_line(in, NULL, NULL, NULL))) {
            const char *path = match_path(ln, files, n);
            if (path) {
                guint idx = 0;
                while (files[idx] != path) idx++;
                seen[idx] = TRUE;
                const char *detail = strstr(ln, "— ");
                detail = detail ? detail + strlen("— ") : "";
                if (strstr(ln, "INFECTED")) {
                    post(M_THREAT, *checked, total, path, detail[0] ? detail : "infected");
                    contain(path);
                } else if (strstr(ln, "UNKNOWN")) {
                    post(M_UNKNOWN, *checked, total, path, detail);
                } else if (total <= 30) {
                    post(M_CLEAN, *checked, total, path, NULL);
                }
                (*checked)++;
                post(M_PROGRESS, *checked, total, path, NULL);
            }
            g_free(ln);
        }
        g_object_unref(in);
        g_subprocess_wait(p, NULL, NULL);
        g_mutex_lock(&app.lock);
        app.sub = NULL;
        g_mutex_unlock(&app.lock);
        g_object_unref(p);
    }
    for (guint j = 0; j < n && !app.cancel; j++) {
        if (seen[j]) continue;
        post(M_UNKNOWN, *checked, total, files[j], "no answer from SnapGuard");
        (*checked)++;
        post(M_PROGRESS, *checked, total, files[j], NULL);
    }
    g_free(seen);
}

static gpointer scan_thread(gpointer data) {
    char *target = data;
    GPtrArray *files = g_ptr_array_new_with_free_func(g_free);
    struct stat st;
    if (lstat(target, &st) == 0 && S_ISREG(st.st_mode)) g_ptr_array_add(files, g_strdup(target));
    else walk(target, files, 0);
    long total = files->len, checked = 0;
    post(M_PROGRESS, 0, total, "", NULL);
    for (guint i = 0; i < files->len && !app.cancel; i += BATCH)
        scan_batch((char **)files->pdata + i, MIN((guint)BATCH, files->len - i), &checked, total);
    post(M_DONE, checked, total, target, app.cancel ? "stopped" : NULL);
    g_ptr_array_free(files, TRUE);
    g_free(target);
    return NULL;
}

static void post(MsgKind kind, long checked, long total, const char *a, const char *b) {
    Msg *m = g_new0(Msg, 1);
    m->kind = kind;
    m->checked = checked;
    m->total = total;
    m->a = g_strdup(a);
    m->b = g_strdup(b);
    g_idle_add(on_msg, m);
}

static void add_result_row(const char *path, const char *detail, int bottom) {
    GtkWidget *row = gtk_list_box_row_new();
    char *base = g_path_get_basename(path);
    char *mk = g_markup_printf_escaped("<b>%s</b>  <span alpha='70%%'>%s</span>\n<span size='small' alpha='60%%'>%s</span>", base, detail, path);
    GtkWidget *lb = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(lb), mk);
    gtk_label_set_xalign(GTK_LABEL(lb), 0);
    gtk_label_set_ellipsize(GTK_LABEL(lb), PANGO_ELLIPSIZE_MIDDLE);
    gtk_widget_set_margin_start(lb, 10);
    gtk_widget_set_margin_end(lb, 10);
    gtk_widget_set_margin_top(lb, 6);
    gtk_widget_set_margin_bottom(lb, 6);
    gtk_container_add(GTK_CONTAINER(row), lb);
    gtk_list_box_insert(GTK_LIST_BOX(app.results), row, bottom ? -1 : 0);
    gtk_widget_show_all(row);
    g_free(mk);
    g_free(base);
}

static void clear_list(GtkWidget *list) {
    GList *kids = gtk_container_get_children(GTK_CONTAINER(list));
    for (GList *k = kids; k; k = k->next) gtk_widget_destroy(GTK_WIDGET(k->data));
    g_list_free(kids);
}

static void refresh_quarantine(void);

static char *fmt_duration(gint64 secs) {
    if (secs >= 3600) return g_strdup_printf("%ldh %02ldm", (long)(secs / 3600), (long)((secs % 3600) / 60));
    if (secs >= 60) return g_strdup_printf("%ldm %02lds", (long)(secs / 60), (long)(secs % 60));
    return g_strdup_printf("%lds", (long)secs);
}

static char *summary_text(int finished) {
    char *d = fmt_duration((g_get_monotonic_time() - app.started) / G_USEC_PER_SEC);
    GString *s = g_string_new(NULL);
    if (app.total > 0)
        g_string_append_printf(s, "%s%ld of %ld files checked", finished ? "Finished: " : "", app.checked, app.total);
    else
        g_string_append(s, finished ? "Finished: no files to check" : "counting files...");
    g_string_append_printf(s, "  ·  %ld threat%s", app.threats, app.threats == 1 ? "" : "s");
    if (app.unknown) g_string_append_printf(s, "  ·  %ld could not be checked", app.unknown);
    if (app.skipped) g_string_append_printf(s, "  ·  %ld over 100 MB skipped", app.skipped);
    g_string_append_printf(s, "  ·  %s", d);
    g_free(d);
    return g_string_free(s, FALSE);
}

static void update_count(int finished) {
    char *t = summary_text(finished);
    gtk_label_set_text(GTK_LABEL(app.count), t);
    g_free(t);
}

static gboolean on_second(gpointer d) {
    (void)d;
    if (!app.scanning) return G_SOURCE_REMOVE;
    update_count(FALSE);
    if (app.total == 0) gtk_progress_bar_pulse(GTK_PROGRESS_BAR(app.bar));
    return G_SOURCE_CONTINUE;
}

static void notify_done(const char *title, const char *body) {
    GApplication *ga = g_application_get_default();
    if (!ga) return;
    GNotification *n = g_notification_new(title);
    g_notification_set_body(n, body);
    g_application_send_notification(ga, "scan-done", n);
    g_object_unref(n);
}

static void finish_scan(const char *target, int stopped) {
    char *what = g_path_get_basename(target);
    char *d = fmt_duration((g_get_monotonic_time() - app.started) / G_USEC_PER_SEC);
    char *head, *say;

    if (stopped) {
        app.mood = MOOD_IDLE;
        head = g_strdup("Scan stopped");
        say = g_strdup_printf("Stopped after %ld of %ld files.", app.checked, app.total);
    } else if (app.total == 0) {
        app.mood = MOOD_IDLE;
        head = g_strdup("Nothing to scan");
        say = g_strdup_printf("There were no files to check in '%s'.", what);
    } else if (app.threats == 0) {
        app.mood = MOOD_HAPPY;
        head = g_strdup("No threats found");
        say = g_strdup_printf("Checked %ld file%s in %s. Nothing suspicious was found.",
                              app.checked, app.checked == 1 ? "" : "s", d);
    } else {
        app.mood = MOOD_HAPPY;
        head = g_strdup_printf("%ld threat%s contained", app.threats, app.threats == 1 ? "" : "s");
        say = g_strdup_printf("Checked %ld file%s in %s. Review the threats in the Quarantine tab.",
                              app.checked, app.checked == 1 ? "" : "s", d);
    }
    set_headline(head);

    GString *more = g_string_new(say);
    if (app.unknown)
        g_string_append_printf(more, " %ld file%s could not be checked.", app.unknown, app.unknown == 1 ? "" : "s");
    if (!stopped && !app.engine_ok)
        g_string_append(more, " ClamAV is still getting its virus database (it needs internet), so only the SnapOS signatures were used.");
    set_speech(more->str);

    char *sum = summary_text(TRUE);
    gtk_label_set_text(GTK_LABEL(app.count), sum);
    char *plain = summary_text(FALSE);
    char *row = g_strdup_printf("%s — %s", head, plain);
    add_result_row(target, row, 0);
    g_free(plain);
    if (!stopped) notify_done("SnapGuard", more->str);

    g_free(row);
    g_free(sum);
    g_string_free(more, TRUE);
    g_free(head);
    g_free(say);
    g_free(d);
    g_free(what);
}

static gboolean on_msg(gpointer data) {
    Msg *m = data;
    if (m->kind == M_PROGRESS) {
        app.checked = m->checked;
        app.total = m->total;
        if (m->total > 0) gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(app.bar), (double)m->checked / m->total);
        update_count(FALSE);
        if (m->a && *m->a) {
            char *b = g_path_get_basename(m->a);
            char *s = g_strdup_printf("Checked: %s", b);
            gtk_label_set_text(GTK_LABEL(app.current), s);
            g_free(s);
            g_free(b);
            if (app.scanning && app.mood == MOOD_SCAN) {
                char *sp = g_strdup_printf("Checking %ld of %ld files.", m->checked + 1 > m->total ? m->total : m->checked + 1, m->total);
                set_speech(sp);
                g_free(sp);
            }
        } else if (m->total > 0) {
            char *sp = g_strdup_printf("Found %ld file%s to check.", m->total, m->total == 1 ? "" : "s");
            set_speech(sp);
            g_free(sp);
        }
    } else if (m->kind == M_THREAT) {
        app.threats++;
        app.mood = MOOD_CHOMP;
        app.chomp_left = 14;
        char *b = g_path_get_basename(m->a);
        char *s = g_strdup_printf("Contained '%s'. Nothing was deleted.", b);
        set_headline("Threat contained");
        set_speech(s);
        g_free(s);
        g_free(b);
        add_result_row(m->a, m->b ? m->b : "infected", 0);
    } else if (m->kind == M_UNKNOWN) {
        app.unknown++;
        if (app.unknown <= 50) {
            char *d = g_strdup_printf("could not be checked: %s", m->b ? m->b : "unknown reason");
            add_result_row(m->a, d, 0);
            g_free(d);
        }
    } else if (m->kind == M_CLEAN) {
        add_result_row(m->a, "clean", 1);
    } else {
        app.scanning = FALSE;
        gtk_widget_set_sensitive(app.btn_quick, TRUE);
        gtk_widget_set_sensitive(app.btn_folder, TRUE);
        gtk_widget_set_sensitive(app.btn_file, TRUE);
        gtk_widget_set_sensitive(app.btn_stop, FALSE);
        gtk_label_set_text(GTK_LABEL(app.current), "");
        if (m->total > 0 && !m->b) gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(app.bar), 1.0);
        app.checked = m->checked;
        app.total = m->total;
        finish_scan(m->a ? m->a : "", m->b != NULL);
        refresh_quarantine();
    }
    g_free(m->a);
    g_free(m->b);
    g_free(m);
    return G_SOURCE_REMOVE;
}

static void start_scan(const char *target) {
    if (app.scanning) return;
    app.scanning = TRUE;
    app.cancel = 0;
    app.threats = 0;
    app.checked = 0;
    app.total = 0;
    app.unknown = 0;
    app.skipped = 0;
    app.started = g_get_monotonic_time();
    refresh_engine();
    app.mood = MOOD_SCAN;
    clear_list(app.results);
    gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(app.bar), 0);
    gtk_label_set_text(GTK_LABEL(app.count), "counting files...");
    gtk_widget_set_sensitive(app.btn_quick, FALSE);
    gtk_widget_set_sensitive(app.btn_folder, FALSE);
    gtk_widget_set_sensitive(app.btn_file, FALSE);
    gtk_widget_set_sensitive(app.btn_stop, TRUE);
    gtk_stack_set_visible_child_name(GTK_STACK(app.stack), "scan");
    char *b = g_path_get_basename(target);
    char *s = g_strdup_printf("Checking '%s'.", b);
    set_headline("Scanning");
    set_speech(s);
    g_free(s);
    g_free(b);
    gtk_label_set_text(GTK_LABEL(app.current), "");
    g_timeout_add(500, on_second, NULL);
    g_thread_unref(g_thread_new("scan", scan_thread, g_strdup(target)));
}

static void on_quick(GtkButton *b, gpointer d) {
    (void)b; (void)d;
    const char *dl = g_get_user_special_dir(G_USER_DIRECTORY_DOWNLOAD);
    start_scan(dl ? dl : g_get_home_dir());
}

static void choose(GtkFileChooserAction action, const char *title) {
    GtkWidget *dlg = gtk_file_chooser_dialog_new(title, GTK_WINDOW(app.win), action,
                                                 "_Cancel", GTK_RESPONSE_CANCEL, "_Scan", GTK_RESPONSE_ACCEPT, NULL);
    if (gtk_dialog_run(GTK_DIALOG(dlg)) == GTK_RESPONSE_ACCEPT) {
        char *f = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(dlg));
        gtk_widget_destroy(dlg);
        if (f) { start_scan(f); g_free(f); }
        return;
    }
    gtk_widget_destroy(dlg);
}

static void on_folder(GtkButton *b, gpointer d) { (void)b; (void)d; choose(GTK_FILE_CHOOSER_ACTION_SELECT_FOLDER, "Choose a folder to scan"); }
static void on_file(GtkButton *b, gpointer d) { (void)b; (void)d; choose(GTK_FILE_CHOOSER_ACTION_OPEN, "Choose a file to scan"); }
static void on_stop(GtkButton *b, gpointer d) {
    (void)b; (void)d;
    app.cancel = 1;
    g_mutex_lock(&app.lock);
    if (app.sub) g_subprocess_force_exit(app.sub);
    g_mutex_unlock(&app.lock);
}

static void on_keep(GtkButton *b, gpointer name) {
    (void)b;
    char *argv[] = { app.guard, "restore", name, "--trust", NULL };
    int st;
    g_free(run_guard(argv, &st));
    set_headline("File restored");
    set_speech("The file was put back and is now trusted.");
    app.mood = MOOD_HAPPY;
    refresh_quarantine();
}

static void on_delete(GtkButton *b, gpointer name) {
    (void)b;
    GtkWidget *dlg = gtk_message_dialog_new(GTK_WINDOW(app.win), GTK_DIALOG_MODAL, GTK_MESSAGE_WARNING, GTK_BUTTONS_NONE,
                                            "Delete this file permanently?");
    gtk_message_dialog_format_secondary_text(GTK_MESSAGE_DIALOG(dlg), "This cannot be undone.");
    gtk_dialog_add_buttons(GTK_DIALOG(dlg), "_Cancel", GTK_RESPONSE_CANCEL, "_Delete", GTK_RESPONSE_OK, NULL);
    int r = gtk_dialog_run(GTK_DIALOG(dlg));
    gtk_widget_destroy(dlg);
    if (r != GTK_RESPONSE_OK) return;
    char *argv[] = { app.guard, "delete", name, NULL };
    int st;
    g_free(run_guard(argv, &st));
    set_headline("File deleted");
    set_speech("The file was permanently deleted.");
    refresh_quarantine();
}

static void free_data(gpointer d, GClosure *c) {
    (void)c;
    g_free(d);
}

static void add_quarantine_row(const char *name, const char *orig) {
    GtkWidget *row = gtk_list_box_row_new();
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    char *mk = g_markup_printf_escaped("<b>%s</b>\n<span size='small' alpha='60%%'>was: %s</span>", name, orig);
    GtkWidget *lb = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(lb), mk);
    gtk_label_set_xalign(GTK_LABEL(lb), 0);
    gtk_label_set_ellipsize(GTK_LABEL(lb), PANGO_ELLIPSIZE_MIDDLE);
    gtk_widget_set_hexpand(lb, TRUE);
    GtkWidget *keep = gtk_button_new_with_label("Keep & trust");
    GtkWidget *del = gtk_button_new_with_label("Delete");
    g_signal_connect_data(keep, "clicked", G_CALLBACK(on_keep), g_strdup(name), free_data, 0);
    g_signal_connect_data(del, "clicked", G_CALLBACK(on_delete), g_strdup(name), free_data, 0);
    gtk_box_pack_start(GTK_BOX(box), lb, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(box), keep, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(box), del, FALSE, FALSE, 0);
    gtk_widget_set_margin_start(box, 10);
    gtk_widget_set_margin_end(box, 10);
    gtk_widget_set_margin_top(box, 6);
    gtk_widget_set_margin_bottom(box, 6);
    gtk_container_add(GTK_CONTAINER(row), box);
    gtk_list_box_insert(GTK_LIST_BOX(app.quarantine), row, -1);
    g_free(mk);
}

static void refresh_quarantine(void) {
    clear_list(app.quarantine);
    char *argv[] = { app.guard, "list-quarantine", NULL };
    int st;
    char *out = run_guard(argv, &st);
    char **lines = g_strsplit(out, "\n", -1);
    char *name = NULL;
    int any = 0;
    for (char **l = lines; *l; l++) {
        if (g_str_has_prefix(*l, "      was: ")) {
            if (name) { add_quarantine_row(name, *l + strlen("      was: ")); any = 1; g_free(name); name = NULL; }
        } else if (g_str_has_prefix(*l, "  ") && (*l)[2] != ' ') {
            g_free(name);
            name = g_strdup(g_strstrip(*l));
        }
    }
    g_free(name);
    g_strfreev(lines);
    g_free(out);
    if (!any) {
        GtkWidget *row = gtk_list_box_row_new();
        GtkWidget *lb = gtk_label_new("Quarantine is empty.");
        gtk_widget_set_margin_top(lb, 18);
        gtk_widget_set_margin_bottom(lb, 18);
        gtk_container_add(GTK_CONTAINER(row), lb);
        gtk_list_box_insert(GTK_LIST_BOX(app.quarantine), row, -1);
    }
    gtk_widget_show_all(app.quarantine);
}

static void on_stack_changed(GObject *o, GParamSpec *p, gpointer d) {
    (void)o; (void)p; (void)d;
    const char *n = gtk_stack_get_visible_child_name(GTK_STACK(app.stack));
    if (n && !strcmp(n, "quarantine")) refresh_quarantine();
}

static const char *CSS =
    "window, .root { background-color: #121212; }"
    "label { color: #e8e8e8; }"
    ".appname { color: #8f8f8f; font-size: 11px; font-weight: bold; letter-spacing: 2px; }"
    ".headline { color: #ffffff; font-size: 26px; font-weight: 600; }"
    ".caption { color: #a8a8a8; font-size: 13px; }"
    ".muted { color: #8f8f8f; font-size: 12px; }"
    ".card { background-color: #1a1a1a; border: 1px solid #2c2c2c; border-radius: 12px; }"
    "button { background-image: none; background-color: #232323; color: #e8e8e8; border: 1px solid #333333; border-radius: 8px; padding: 8px 16px; box-shadow: none; text-shadow: none; }"
    "button label { color: #e8e8e8; }"
    "button:hover { background-color: #2c2c2c; }"
    "button:disabled { background-color: #171717; border-color: #262626; }"
    "button:disabled label { color: #5a5a5a; }"
    "button.primary { background-color: #e22a1c; border-color: #e22a1c; }"
    "button.primary label { color: #ffffff; font-weight: 600; }"
    "button.primary:hover { background-color: #f0392b; }"
    "button.primary:disabled { background-color: #4a1712; border-color: #4a1712; }"
    "progressbar trough { background-color: #262626; border: none; border-radius: 3px; min-height: 6px; }"
    "progressbar progress { background-image: none; background-color: #e22a1c; border: none; border-radius: 3px; min-height: 6px; }"
    "list, row { background-color: #121212; }"
    "row { border-bottom: 1px solid #222222; }"
    "row:hover { background-color: #181818; }"
    "scrolledwindow { border: 1px solid #2c2c2c; border-radius: 10px; }"
    "stackswitcher button { background-color: transparent; border: none; border-bottom: 2px solid transparent; border-radius: 0; padding: 8px 14px; }"
    "stackswitcher button:checked { border-bottom-color: #e22a1c; }"
    "stackswitcher button:checked label { color: #ffffff; font-weight: 600; }";

/* On a light desktop every text is black or red, so the dark palette above is
 * swapped for a light one. /etc/snapos/appearance holds "dark" or "light". */
static int is_light(void) {
    const char *e = g_getenv("SNAPOS_APPEARANCE");
    if (e) return !strcmp(e, "light");
    char *s = NULL;
    int light = 0;
    if (g_file_get_contents("/etc/snapos/appearance", &s, NULL, NULL)) {
        light = g_str_has_prefix(s, "light");
        g_free(s);
    }
    return light;
}

static char *light_css(const char *dark) {
    static const char *const pairs[][2] = {
        { "#ffffff", "#111111" }, { "#121212", "#f6f4f1" }, { "#e8e8e8", "#1c1c1c" },
        { "#8f8f8f", "#6b6661" }, { "#a8a8a8", "#4a4540" }, { "#1a1a1a", "#ffffff" },
        { "#2c2c2c", "#d4d0cb" }, { "#232323", "#ece9e5" }, { "#333333", "#cfcac4" },
        { "#171717", "#f0eeeb" }, { "#262626", "#dedad5" }, { "#5a5a5a", "#a09a94" },
        { "#4a1712", "#f0b4ae" }, { "#222222", "#e2ded9" }, { "#181818", "#efece8" },
    };
    GString *s = g_string_new(dark);
    for (size_t i = 0; i < sizeof pairs / sizeof pairs[0]; i++) g_string_replace(s, pairs[i][0], pairs[i][1], 0);
    return g_string_free(s, FALSE);
}

static GtkWidget *scrolled(GtkWidget *child) {
    GtkWidget *sw = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(sw), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_widget_set_vexpand(sw, TRUE);
    gtk_container_add(GTK_CONTAINER(sw), child);
    return sw;
}

static GtkWidget *button(const char *label, GCallback cb, const char *klass) {
    GtkWidget *b = gtk_button_new_with_label(label);
    g_signal_connect(b, "clicked", cb, NULL);
    if (klass) gtk_style_context_add_class(gtk_widget_get_style_context(b), klass);
    return b;
}

static void activate(GtkApplication *a, gpointer d) {
    (void)d;
    GtkCssProvider *css = gtk_css_provider_new();
    char *css_text = is_light() ? light_css(CSS) : g_strdup(CSS);
    gtk_css_provider_load_from_data(css, css_text, -1, NULL);
    g_free(css_text);
    gtk_style_context_add_provider_for_screen(gdk_screen_get_default(), GTK_STYLE_PROVIDER(css),
                                              GTK_STYLE_PROVIDER_PRIORITY_USER);

    app.win = gtk_application_window_new(a);
    gtk_window_set_title(GTK_WINDOW(app.win), "SnapGuard");
    gtk_window_set_icon_name(GTK_WINDOW(app.win), "snapguard");
    gtk_window_set_default_size(GTK_WINDOW(app.win), 860, 600);
    gtk_style_context_add_class(gtk_widget_get_style_context(app.win), "root");

    GtkWidget *root = gtk_box_new(GTK_ORIENTATION_VERTICAL, 16);
    gtk_container_set_border_width(GTK_CONTAINER(root), 24);
    gtk_container_add(GTK_CONTAINER(app.win), root);

    GtkWidget *top = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 24);
    gtk_box_pack_start(GTK_BOX(root), top, FALSE, FALSE, 0);

    GtkWidget *card = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_style_context_add_class(gtk_widget_get_style_context(card), "card");
    app.canvas = gtk_drawing_area_new();
    gtk_widget_set_size_request(app.canvas, 300, 150);
    g_signal_connect(app.canvas, "draw", G_CALLBACK(on_draw), NULL);
    gtk_box_pack_start(GTK_BOX(card), app.canvas, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(top), card, FALSE, FALSE, 0);

    GtkWidget *right = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
    gtk_box_pack_start(GTK_BOX(top), right, TRUE, TRUE, 0);

    GtkWidget *name = gtk_label_new("SNAPGUARD");
    gtk_label_set_xalign(GTK_LABEL(name), 0);
    gtk_style_context_add_class(gtk_widget_get_style_context(name), "appname");
    gtk_box_pack_start(GTK_BOX(right), name, FALSE, FALSE, 0);

    app.headline = gtk_label_new("Ready to scan");
    gtk_label_set_xalign(GTK_LABEL(app.headline), 0);
    gtk_style_context_add_class(gtk_widget_get_style_context(app.headline), "headline");
    gtk_box_pack_start(GTK_BOX(right), app.headline, FALSE, FALSE, 0);

    app.speech = gtk_label_new("Snappy is on guard.");
    gtk_label_set_xalign(GTK_LABEL(app.speech), 0);
    gtk_label_set_line_wrap(GTK_LABEL(app.speech), TRUE);
    gtk_style_context_add_class(gtk_widget_get_style_context(app.speech), "caption");
    gtk_box_pack_start(GTK_BOX(right), app.speech, FALSE, FALSE, 0);

    app.engine = gtk_label_new("");
    gtk_label_set_xalign(GTK_LABEL(app.engine), 0);
    gtk_style_context_add_class(gtk_widget_get_style_context(app.engine), "muted");
    gtk_box_pack_start(GTK_BOX(right), app.engine, FALSE, FALSE, 0);

    GtkWidget *btns = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_widget_set_margin_top(btns, 10);
    app.btn_quick = button("Quick scan", G_CALLBACK(on_quick), "primary");
    app.btn_folder = button("Scan folder", G_CALLBACK(on_folder), NULL);
    app.btn_file = button("Scan file", G_CALLBACK(on_file), NULL);
    app.btn_stop = button("Stop", G_CALLBACK(on_stop), NULL);
    gtk_widget_set_sensitive(app.btn_stop, FALSE);
    gtk_box_pack_start(GTK_BOX(btns), app.btn_quick, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(btns), app.btn_folder, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(btns), app.btn_file, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(btns), app.btn_stop, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(right), btns, FALSE, FALSE, 0);

    app.bar = gtk_progress_bar_new();
    gtk_widget_set_margin_top(app.bar, 10);
    gtk_box_pack_start(GTK_BOX(right), app.bar, FALSE, FALSE, 0);
    app.count = gtk_label_new("");
    gtk_label_set_xalign(GTK_LABEL(app.count), 0);
    gtk_style_context_add_class(gtk_widget_get_style_context(app.count), "muted");
    gtk_box_pack_start(GTK_BOX(right), app.count, FALSE, FALSE, 0);
    app.current = gtk_label_new("");
    gtk_label_set_xalign(GTK_LABEL(app.current), 0);
    gtk_label_set_ellipsize(GTK_LABEL(app.current), PANGO_ELLIPSIZE_MIDDLE);
    gtk_style_context_add_class(gtk_widget_get_style_context(app.current), "muted");
    gtk_box_pack_start(GTK_BOX(right), app.current, FALSE, FALSE, 0);

    app.stack = gtk_stack_new();
    GtkWidget *switcher = gtk_stack_switcher_new();
    gtk_stack_switcher_set_stack(GTK_STACK_SWITCHER(switcher), GTK_STACK(app.stack));
    gtk_widget_set_halign(switcher, GTK_ALIGN_START);
    gtk_box_pack_start(GTK_BOX(root), switcher, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(root), app.stack, TRUE, TRUE, 0);

    app.results = gtk_list_box_new();
    gtk_list_box_set_selection_mode(GTK_LIST_BOX(app.results), GTK_SELECTION_NONE);
    GtkWidget *hint = gtk_label_new("Nothing to show yet. Start a scan.");
    gtk_style_context_add_class(gtk_widget_get_style_context(hint), "muted");
    gtk_widget_set_margin_top(hint, 24);
    gtk_widget_set_margin_bottom(hint, 24);
    gtk_list_box_set_placeholder(GTK_LIST_BOX(app.results), hint);
    gtk_widget_show(hint);
    gtk_stack_add_titled(GTK_STACK(app.stack), scrolled(app.results), "scan", "Findings");

    app.quarantine = gtk_list_box_new();
    gtk_list_box_set_selection_mode(GTK_LIST_BOX(app.quarantine), GTK_SELECTION_NONE);
    gtk_stack_add_titled(GTK_STACK(app.stack), scrolled(app.quarantine), "quarantine", "Quarantine");
    g_signal_connect(app.stack, "notify::visible-child-name", G_CALLBACK(on_stack_changed), NULL);

    app.guard = find_guard();
    refresh_engine();
    g_timeout_add(80, on_tick, NULL);
    gtk_widget_show_all(app.win);
}

int main(int argc, char **argv) {
    GtkApplication *a = gtk_application_new("org.snapos.SnapGuard", G_APPLICATION_DEFAULT_FLAGS);
    g_signal_connect(a, "activate", G_CALLBACK(activate), NULL);
    int r = g_application_run(G_APPLICATION(a), argc, argv);
    g_object_unref(a);
    return r;
}
