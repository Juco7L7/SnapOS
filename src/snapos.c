#define _POSIX_C_SOURCE 200809L
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define DEFAULT_DIR "/etc/nixos"
#define FLAKE_ATTR  "snapos"

static const char *nixdir(void) {
    const char *d = getenv("SNAPOS_NIX_DIR");
    return (d && *d) ? d : DEFAULT_DIR;
}

static void usage(FILE *out) {
    fprintf(out,
        "snapos — manage this SnapOS system\n"
        "\n"
        "  snapos config            open configuration.nix in $EDITOR (nano if unset)\n"
        "  snapos rebuild [MODE]    apply the config; MODE = switch (default), test,\n"
        "                           boot or dry-build. Extra args go to nixos-rebuild.\n"
        "  snapos doctor            show graphics, boot and antivirus problems (GPU, failed units,\n"
        "                           display manager and X errors)\n"
        "  snapos help              this text\n"
        "\n"
        "Runs through sudo automatically when you are not root.\n");
}

static void reexec_with_sudo(int argc, char **argv) {
    char self[PATH_MAX];
    ssize_t n = readlink("/proc/self/exe", self, sizeof self - 1);
    if (n <= 0) { perror("snapos: cannot locate myself"); return; }
    self[n] = '\0';

    char **nargv = calloc((size_t)argc + 2, sizeof *nargv);
    if (!nargv) { perror("snapos"); return; }
    nargv[0] = "sudo";
    nargv[1] = self;
    for (int i = 1; i < argc; i++) nargv[i + 1] = argv[i];
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

static int cmd_rebuild(int argc, char **argv) {
    char flake[PATH_MAX + 16];
    snprintf(flake, sizeof flake, "path:%s#%s", nixdir(), FLAKE_ATTR);

    const char *mode = "switch";
    int extra = 2;
    if (argc > 2 && argv[2][0] != '-') { mode = argv[2]; extra = 3; }
    if (strcmp(mode, "switch") && strcmp(mode, "test") &&
        strcmp(mode, "boot")   && strcmp(mode, "dry-build")) {
        fprintf(stderr, "snapos: unknown rebuild mode '%s' "
                        "(use switch, test, boot or dry-build)\n", mode);
        return 2;
    }

    char **nargv = calloc((size_t)argc + 6, sizeof *nargv);
    if (!nargv) { perror("snapos"); return 1; }
    int k = 0;
    nargv[k++] = "nixos-rebuild";
    nargv[k++] = (char *)mode;
    nargv[k++] = "--flake";
    nargv[k++] = flake;
    for (int i = extra; i < argc; i++) nargv[k++] = argv[i];

    fprintf(stderr, "snapos: nixos-rebuild %s --flake %s\n\n", mode, flake);
    execvp("nixos-rebuild", nargv);
    perror("snapos: could not run nixos-rebuild (is this a NixOS/SnapOS system?)");
    free(nargv);
    return 1;
}

static int cmd_doctor(void) {
    const char *script =
        "echo '== GPU'; lspci 2>/dev/null | grep -iE 'vga|3d|display'\n"
        "echo; echo '== kernel'; uname -r\n"
        "echo; echo '== failed units'; systemctl --failed --no-legend\n"
        "echo; echo '== display manager'; journalctl -u display-manager -b --no-pager -n 25\n"
        "echo; echo '== X errors'; grep -E '\\(EE\\)' /var/log/X.0.log 2>/dev/null | head -20\n"
        "echo; echo '== graphics kernel messages'; dmesg 2>/dev/null | grep -iE 'drm|radeon|amdgpu|nouveau|i915|firmware' | tail -15\n"
        "echo; echo '== antivirus'; snapguard status 2>&1\n"
        "echo; systemctl is-active clamav-freshclam clamav-daemon 2>&1\n"
        "echo; journalctl -u clamav-freshclam -b --no-pager -n 6 2>&1\n";
    execl("/bin/sh", "sh", "-c", script, (char *)NULL);
    perror("snapos: could not run the checks");
    return 1;
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
    if (!is_config && !is_rebuild && !is_doctor) {
        fprintf(stderr, "snapos: unknown command '%s'\n\n", argv[1]);
        usage(stderr);
        return 2;
    }

    if (geteuid() != 0) {
        reexec_with_sudo(argc, argv);
        return 1;
    }
    if (is_doctor) return cmd_doctor();
    return is_config ? cmd_config() : cmd_rebuild(argc, argv);
}
