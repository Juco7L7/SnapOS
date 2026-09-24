#define _GNU_SOURCE
#include <gtk/gtk.h>
#include <glib/gstdio.h>
#include <stdlib.h>
#include <string.h>

/* snapupdate: runs at login, asks `snapos update check` whether a newer SnapOS
 * release exists, and if so shows a window with the release notes and an
 * Install button. Installing runs `snapos update` in a terminal, where the
 * installer-style screen shows the steps. */

static struct {
    GtkWidget *win;
    char tag[128], name[256], notes[4096], latest[64], undone[256];
    int autostart;
    int built;      /* the update is built already; a restart finishes it */
    int unknown;    /* the check could not reach GitHub */
} app;

static const char *CSS =
    "window, .root { background-color: #121212; }"
    ".card { background-color: #0d0909; border-radius: 12px; border: 1px solid #6e140e; padding: 18px; }"
    "label { color: #e8e8e8; }"
    "label.title { font-size: 20px; font-weight: 700; color: #e22a1c; }"
    "label.notes { color: #c8c8c8; }"
    "button { background-image: none; background-color: #232323; color: #e8e8e8; border: 1px solid #333333; border-radius: 8px; padding: 10px 22px; box-shadow: none; text-shadow: none; }"
    "button label { color: #e8e8e8; }"
    "button:hover { background-color: #2c2c2c; }"
    "button.primary { background-color: #e22a1c; border-color: #e22a1c; }"
    "button.primary label { color: #ffffff; font-weight: 600; }"
    "button.primary:hover { background-color: #f0392b; }";

static const char *CSS_LIGHT =
    "window, .root { background-color: #ece9e5; }"
    ".card { background-color: #ffffff; border-radius: 12px; border: 1px solid #e22a1c; padding: 18px; }"
    "label { color: #000000; }"
    "label.title { font-size: 20px; font-weight: 700; color: #e22a1c; }"
    "label.notes { color: #000000; }"
    "button { background-image: none; background-color: #ffffff; color: #000000; border: 1px solid #c9c4be; border-radius: 8px; padding: 10px 22px; box-shadow: none; text-shadow: none; }"
    "button label { color: #000000; }"
    "button:hover { background-color: #f3f0ec; }"
    "button.primary { background-color: #e22a1c; border-color: #e22a1c; }"
    "button.primary label { color: #ffffff; font-weight: 600; }"
    "button.primary:hover { background-color: #f0392b; }";

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

/* Runs `snapos update check` and keeps what it prints. 1 = an update exists. */
static int check_once(char **out, int *code) {
    const char *cmd = g_getenv("SNAPUPDATE_CHECK");
    if (!cmd || !*cmd) cmd = "snapos update check";
    int status = 0;
    *out = NULL;
    if (!g_spawn_command_line_sync(cmd, out, NULL, &status, NULL) || !*out) { *code = -1; return 0; }
    *code = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
    return 1;
}

static int check(void) {
    char *out = NULL;
    int code = -1;
    /* at login the network may need a minute; a failed check is tried again */
    int tries = app.autostart ? 6 : 1;
    for (int i = 0; i < tries; i++) {
        if (check_once(&out, &code) && (code == 0 || code == 10 || code == 11)) break;
        g_free(out);
        out = NULL;
        if (i + 1 < tries) g_usleep(20 * G_USEC_PER_SEC);
    }
    if (!out || (code != 0 && code != 10 && code != 11)) { app.unknown = 1; g_free(out); return 0; }
    char **lines = g_strsplit(out, "\n", -1);
    g_free(out);
    int in_notes = 0;
    app.notes[0] = 0;
    for (char **l = lines; *l; l++) {
        if (in_notes) {
            if (strlen(app.notes) + strlen(*l) + 2 < sizeof app.notes) {
                strcat(app.notes, *l);
                strcat(app.notes, "\n");
            }
        } else if (g_str_has_prefix(*l, "tag ")) g_strlcpy(app.tag, *l + 4, sizeof app.tag);
        else if (g_str_has_prefix(*l, "name ")) g_strlcpy(app.name, *l + 5, sizeof app.name);
        else if (g_str_has_prefix(*l, "latest ")) g_strlcpy(app.latest, *l + 7, sizeof app.latest);
        else if (!strcmp(*l, "notes")) in_notes = 1;
    }
    g_strfreev(lines);
    g_strstrip(app.notes);
    if (code == 11) app.built = 1;
    return code == 10 || code == 11;
}

/* The guard leaves a marker when it went back to the previous system. Each
 * user is told once (the marker's time is remembered in the user's config). */
static int rollback_notice(char *name, size_t n) {
    const char *marker = g_getenv("SNAPUPDATE_ROLLBACK_MARKER");
    if (!marker || !*marker) marker = "/var/lib/snapos/update/update-rolled-back";
    GStatBuf st;
    if (g_stat(marker, &st) != 0) return 0;
    char *seen = g_build_filename(g_get_user_config_dir(), "snapos", "rollback-seen", NULL);
    char *prev = NULL;
    char now[64];
    g_snprintf(now, sizeof now, "%ld", (long)st.st_mtime);
    int fresh = 1;
    if (g_file_get_contents(seen, &prev, NULL, NULL)) { fresh = strcmp(g_strstrip(prev), now) != 0; g_free(prev); }
    if (fresh) {
        char *dir = g_path_get_dirname(seen);
        g_mkdir_with_parents(dir, 0755);
        g_file_set_contents(seen, now, -1, NULL);
        g_free(dir);
        char *text = NULL;
        if (g_file_get_contents(marker, &text, NULL, NULL)) { g_strlcpy(name, g_strstrip(text), n); g_free(text); }
        else name[0] = 0;
    }
    g_free(seen);
    return fresh;
}

static void on_later(GtkButton *b, gpointer d) { (void)b; (void)d; gtk_widget_destroy(app.win); }

static void on_restart(GtkButton *b, gpointer d) {
    (void)b; (void)d;
    g_spawn_command_line_async("systemctl reboot", NULL);
    gtk_widget_destroy(app.win);
}

static void on_install(GtkButton *b, gpointer d) {
    (void)b; (void)d;
    /* The terminal stays open until Enter, so an error can be read, and
     * everything the updater prints is kept in the user's update.log. */
    const char *term = g_getenv("SNAPUPDATE_TERMINAL");
    if (!term || !*term) term = "gnome-terminal --title=SnapOS -- bash -c '"
        "mkdir -p \"$HOME/.local/share/snapos\"; "
        "snapos update 2>&1 | tee -a \"$HOME/.local/share/snapos/update.log\"; "
        "echo; read -r -p \"Press Enter to close.\"'";
    GError *err = NULL;
    if (!g_spawn_command_line_async(term, &err)) {
        GtkWidget *dlg = gtk_message_dialog_new(GTK_WINDOW(app.win), GTK_DIALOG_MODAL, GTK_MESSAGE_ERROR,
            GTK_BUTTONS_CLOSE, "Could not open a terminal. Run this in one:\n\n  snapos update");
        gtk_dialog_run(GTK_DIALOG(dlg));
        gtk_widget_destroy(dlg);
        if (err) g_error_free(err);
        return;
    }
    gtk_widget_destroy(app.win);
}

static void build_window(int available) {
    app.win = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(app.win), "SnapOS update");
    gtk_window_set_default_size(GTK_WINDOW(app.win), 520, 360);
    gtk_window_set_position(GTK_WINDOW(app.win), GTK_WIN_POS_CENTER);
    gtk_window_set_icon_name(GTK_WINDOW(app.win), "snapos");
    g_signal_connect(app.win, "destroy", G_CALLBACK(gtk_main_quit), NULL);

    GtkCssProvider *css = gtk_css_provider_new();
    gtk_css_provider_load_from_data(css, is_light() ? CSS_LIGHT : CSS, -1, NULL);
    gtk_style_context_add_provider_for_screen(gdk_screen_get_default(), GTK_STYLE_PROVIDER(css),
                                              GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);

    GtkWidget *root = gtk_box_new(GTK_ORIENTATION_VERTICAL, 14);
    gtk_style_context_add_class(gtk_widget_get_style_context(root), "root");
    gtk_container_set_border_width(GTK_CONTAINER(root), 22);
    gtk_container_add(GTK_CONTAINER(app.win), root);

    GtkWidget *card = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_style_context_add_class(gtk_widget_get_style_context(card), "card");
    gtk_box_pack_start(GTK_BOX(root), card, TRUE, TRUE, 0);

    char title[512];
    if (app.undone[0]) snprintf(title, sizeof title, "The update to %s was undone", app.undone);
    else if (available && app.built) snprintf(title, sizeof title, "%s is installed", app.name[0] ? app.name : app.tag);
    else if (available) snprintf(title, sizeof title, "%s is ready", app.name[0] ? app.name : app.tag);
    else if (app.unknown) snprintf(title, sizeof title, "Could not check for updates");
    else snprintf(title, sizeof title, "SnapOS is up to date");
    GtkWidget *t = gtk_label_new(title);
    gtk_style_context_add_class(gtk_widget_get_style_context(t), "title");
    gtk_label_set_xalign(GTK_LABEL(t), 0);
    gtk_box_pack_start(GTK_BOX(card), t, FALSE, FALSE, 0);

    GtkWidget *sub = gtk_label_new(app.undone[0]
        ? "The new system did not reach the login screen, so SnapOS went back to the previous one. Everything is as it was before the update. You can try again later with SnapOS Update."
        : available && app.built
        ? "The new release is built and starts at the next boot. Restart to use it. If it does not come up, SnapOS goes back to this one by itself."
        : available
        ? "A new SnapOS release is available. Installing keeps your files, your programs and your settings, and the new system is used from the next restart. If it does not come up, SnapOS goes back to this one by itself."
        : app.unknown
        ? "GitHub did not answer. Check the network and try again in a moment; in a terminal, `snapos update` says more."
        : "This system runs the latest release.");
    gtk_label_set_line_wrap(GTK_LABEL(sub), TRUE);
    gtk_label_set_xalign(GTK_LABEL(sub), 0);
    gtk_box_pack_start(GTK_BOX(card), sub, FALSE, FALSE, 0);

    if (available && !app.undone[0] && app.notes[0]) {
        GtkWidget *scroll = gtk_scrolled_window_new(NULL, NULL);
        gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
        GtkWidget *notes = gtk_label_new(app.notes);
        gtk_style_context_add_class(gtk_widget_get_style_context(notes), "notes");
        gtk_label_set_line_wrap(GTK_LABEL(notes), TRUE);
        gtk_label_set_xalign(GTK_LABEL(notes), 0);
        gtk_label_set_yalign(GTK_LABEL(notes), 0);
        gtk_container_add(GTK_CONTAINER(scroll), notes);
        gtk_box_pack_start(GTK_BOX(card), scroll, TRUE, TRUE, 0);
    }

    GtkWidget *buttons = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    gtk_widget_set_halign(buttons, GTK_ALIGN_END);
    gtk_box_pack_end(GTK_BOX(root), buttons, FALSE, FALSE, 0);
    GtkWidget *later = gtk_button_new_with_label(available ? "Later" : "Close");
    g_signal_connect(later, "clicked", G_CALLBACK(on_later), NULL);
    gtk_box_pack_start(GTK_BOX(buttons), later, FALSE, FALSE, 0);
    if (available && !app.undone[0]) {
        GtkWidget *install = gtk_button_new_with_label(app.built ? "Restart now" : "Install now");
        gtk_style_context_add_class(gtk_widget_get_style_context(install), "primary");
        g_signal_connect(install, "clicked", G_CALLBACK(app.built ? on_restart : on_install), NULL);
        gtk_box_pack_start(GTK_BOX(buttons), install, FALSE, FALSE, 0);
        gtk_widget_grab_focus(install);
    }
    gtk_widget_show_all(app.win);
}

int main(int argc, char **argv) {
    for (int i = 1; i < argc; i++)
        if (!strcmp(argv[i], "--autostart")) app.autostart = 1;
    int undone = rollback_notice(app.undone, sizeof app.undone);
    int available = undone ? 0 : check();
    /* at login only news is worth a window; from the menu the answer always is */
    if (app.autostart && !available && !undone) return 0;
    gtk_init(&argc, &argv);
    build_window(available);
    gtk_main();
    return 0;
}
