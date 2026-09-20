#define _GNU_SOURCE
#include <gtk/gtk.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* SnapHelper: a short tour with three animations. Left and right arrows (or the
 * buttons) move between them, and the dots show where you are. */

static const char *SLIDES[] = { "snappy-declares.gif", "snappy-deb.gif", "snappy-defends.gif" };
#define NSLIDES (sizeof SLIDES / sizeof SLIDES[0])

static struct {
    GtkWidget *win, *image, *dots, *back, *next, *missing;
    guint page;
    char *dir;
    int first_run;
} app;

static const char *label_next(void) { return "Next"; }
static const char *label_back(void) { return "Back"; }
static const char *label_end(void)  { return "End"; }

static char *done_file(void) {
    return g_build_filename(g_get_user_config_dir(), "snapos", "helper-done", NULL);
}

static void mark_done(void) {
    char *f = done_file();
    char *dir = g_path_get_dirname(f);
    g_mkdir_with_parents(dir, 0755);
    g_file_set_contents(f, "1\n", -1, NULL);
    g_free(dir);
    g_free(f);
}

static char *find_dir(void) {
    const char *env = g_getenv("SNAPHELPER_DIR");
    if (env && g_file_test(env, G_FILE_TEST_IS_DIR)) return g_strdup(env);
    char self[PATH_MAX];
    ssize_t n = readlink("/proc/self/exe", self, sizeof self - 1);
    if (n > 0) {
        self[n] = 0;
        char *bin = g_path_get_dirname(self);
        char *a = g_build_filename(bin, "..", "share", "snapos", "helper", NULL);
        char *b = g_build_filename(bin, "..", "branding", NULL);
        g_free(bin);
        if (g_file_test(a, G_FILE_TEST_IS_DIR)) { g_free(b); return a; }
        g_free(a);
        if (g_file_test(b, G_FILE_TEST_IS_DIR)) return b;
        g_free(b);
    }
    return g_strdup("/run/current-system/sw/share/snapos/helper");
}

static void show_page(void) {
    char *path = g_build_filename(app.dir, SLIDES[app.page], NULL);
    if (g_file_test(path, G_FILE_TEST_EXISTS)) {
        gtk_image_set_from_file(GTK_IMAGE(app.image), path);
        gtk_widget_hide(app.missing);
        gtk_widget_show(app.image);
    } else {
        gtk_widget_hide(app.image);
        gtk_widget_show(app.missing);
    }
    g_free(path);
    /* the first page has no Back; the last page ends the tour instead of going on */
    gtk_widget_set_opacity(app.back, app.page > 0 ? 1.0 : 0.0);
    gtk_widget_set_sensitive(app.back, app.page > 0);
    gtk_button_set_label(GTK_BUTTON(app.next), app.page + 1 == NSLIDES ? label_end() : label_next());
    gtk_widget_queue_draw(app.dots);
}

static void finish(void) {
    if (app.first_run) mark_done();
    gtk_widget_destroy(app.win);
}

static void go_next(void) {
    if (app.page + 1 >= NSLIDES) { finish(); return; }
    app.page++;
    show_page();
}

static void go_back(void) {
    if (app.page > 0) { app.page--; show_page(); }
}

static void on_next(GtkButton *b, gpointer d) { (void)b; (void)d; go_next(); }
static void on_back(GtkButton *b, gpointer d) { (void)b; (void)d; go_back(); }

static gboolean on_key(GtkWidget *w, GdkEventKey *e, gpointer d) {
    (void)w; (void)d;
    switch (e->keyval) {
    case GDK_KEY_Right: if (app.page + 1 < NSLIDES) go_next(); return TRUE;
    case GDK_KEY_Left:  go_back(); return TRUE;
    case GDK_KEY_Return:
    case GDK_KEY_KP_Enter: go_next(); return TRUE;
    case GDK_KEY_Escape: finish(); return TRUE;
    default: return FALSE;
    }
}

static gboolean on_close(GtkWidget *w, GdkEvent *e, gpointer d) {
    (void)w; (void)e; (void)d;
    if (app.first_run) mark_done();
    return FALSE;
}

static gboolean on_dots(GtkWidget *w, cairo_t *cr, gpointer d) {
    (void)d;
    int width = gtk_widget_get_allocated_width(w), height = gtk_widget_get_allocated_height(w);
    double gap = 24, x0 = (width - gap * (NSLIDES - 1)) / 2;
    for (guint i = 0; i < NSLIDES; i++) {
        if (i == app.page) cairo_set_source_rgb(cr, 0.886, 0.165, 0.11);
        else cairo_set_source_rgb(cr, 0.29, 0.16, 0.14);
        cairo_arc(cr, x0 + gap * i, height / 2.0, i == app.page ? 6.5 : 5, 0, 2 * G_PI);
        cairo_fill(cr);
    }
    return FALSE;
}

static const char *CSS =
    "window, .root { background-color: #121212; }"
    ".gifcard { background-color: #0d0909; border-radius: 12px; border: 1px solid #6e140e; }"
    "label { color: #e8e8e8; }"
    "button { background-image: none; background-color: #232323; color: #e8e8e8; border: 1px solid #333333; border-radius: 8px; padding: 10px 22px; box-shadow: none; text-shadow: none; }"
    "button label { color: #e8e8e8; }"
    "button:hover { background-color: #2c2c2c; }"
    "button.primary { background-color: #e22a1c; border-color: #e22a1c; }"
    "button.primary label { color: #ffffff; font-weight: 600; }"
    "button.primary:hover { background-color: #f0392b; }";

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

static void activate(GtkApplication *a, gpointer d) {
    (void)d;
    GtkCssProvider *css = gtk_css_provider_new();
    char *css_text = is_light() ? light_css(CSS) : g_strdup(CSS);
    gtk_css_provider_load_from_data(css, css_text, -1, NULL);
    g_free(css_text);
    gtk_style_context_add_provider_for_screen(gdk_screen_get_default(), GTK_STYLE_PROVIDER(css),
                                              GTK_STYLE_PROVIDER_PRIORITY_USER);
    app.dir = find_dir();
    app.win = gtk_application_window_new(a);
    gtk_window_set_title(GTK_WINDOW(app.win), "SnapHelper");
    gtk_window_set_icon_name(GTK_WINDOW(app.win), "snapos");
    gtk_window_set_default_size(GTK_WINDOW(app.win), 1000, 560);
    gtk_window_set_position(GTK_WINDOW(app.win), GTK_WIN_POS_CENTER);
    gtk_style_context_add_class(gtk_widget_get_style_context(app.win), "root");
    g_signal_connect(app.win, "delete-event", G_CALLBACK(on_close), NULL);
    g_signal_connect(app.win, "key-press-event", G_CALLBACK(on_key), NULL);

    GtkWidget *root = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 24);
    gtk_container_set_border_width(GTK_CONTAINER(root), 24);
    gtk_container_add(GTK_CONTAINER(app.win), root);

    /* left: the animation and the dots under it */
    GtkWidget *left = gtk_box_new(GTK_ORIENTATION_VERTICAL, 14);
    gtk_widget_set_hexpand(left, TRUE);
    gtk_widget_set_valign(left, GTK_ALIGN_CENTER);
    GtkWidget *card = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_style_context_add_class(gtk_widget_get_style_context(card), "gifcard");
    gtk_widget_set_halign(card, GTK_ALIGN_CENTER);
    app.image = gtk_image_new();
    gtk_widget_set_size_request(app.image, 760, 400);
    app.missing = gtk_label_new("The animation was not found.");
    gtk_widget_set_no_show_all(app.missing, TRUE);
    gtk_box_pack_start(GTK_BOX(card), app.image, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(card), app.missing, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(left), card, FALSE, FALSE, 0);
    app.dots = gtk_drawing_area_new();
    gtk_widget_set_size_request(app.dots, 120, 22);
    gtk_widget_set_halign(app.dots, GTK_ALIGN_CENTER);
    g_signal_connect(app.dots, "draw", G_CALLBACK(on_dots), NULL);
    gtk_box_pack_start(GTK_BOX(left), app.dots, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(root), left, TRUE, TRUE, 0);

    /* right: Next on top, Back under it */
    GtkWidget *right = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
    gtk_widget_set_valign(right, GTK_ALIGN_CENTER);
    app.next = gtk_button_new_with_label(label_next());
    app.back = gtk_button_new_with_label(label_back());
    gtk_style_context_add_class(gtk_widget_get_style_context(app.next), "primary");
    gtk_widget_set_size_request(app.next, 110, -1);
    g_signal_connect(app.next, "clicked", G_CALLBACK(on_next), NULL);
    g_signal_connect(app.back, "clicked", G_CALLBACK(on_back), NULL);
    gtk_box_pack_start(GTK_BOX(right), app.next, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(right), app.back, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(root), right, FALSE, FALSE, 0);

    gtk_widget_show_all(app.win);
    show_page();
}

int main(int argc, char **argv) {
    /* --first-run: open only once per user, then never again by itself */
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--first-run")) {
            app.first_run = 1;
            for (int j = i; j + 1 < argc; j++) argv[j] = argv[j + 1];
            argc--;
            char *f = done_file();
            int seen = g_file_test(f, G_FILE_TEST_EXISTS);
            g_free(f);
            if (seen) return 0;
            break;
        }
    }
    GtkApplication *a = gtk_application_new("org.snapos.Helper", G_APPLICATION_NON_UNIQUE);
    g_signal_connect(a, "activate", G_CALLBACK(activate), NULL);
    int r = g_application_run(G_APPLICATION(a), argc, argv);
    g_object_unref(a);
    return r;
}
