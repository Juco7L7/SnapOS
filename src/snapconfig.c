#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#define SNAPOS_CONFIG "/etc/nixos/configuration.nix"

int main(void) {
    if (access(SNAPOS_CONFIG, F_OK) != 0) {
        fprintf(stderr,
            "snapconfig: %s not found.\n"
            "  This system may not be a SnapOS install (or has no config to edit yet).\n",
            SNAPOS_CONFIG);
        return 1;
    }
    const char *editor = getenv("EDITOR");
    if (!editor || !*editor) editor = "nano";
    fprintf(stderr, "snapconfig: opening %s with %s ...\n", SNAPOS_CONFIG, editor);
    fprintf(stderr, "snapconfig: after saving, apply with: snapos rebuild\n\n");
    execlp(editor, editor, SNAPOS_CONFIG, (char *)NULL);
    perror("snapconfig: failed to launch editor");
    return 1;
}
