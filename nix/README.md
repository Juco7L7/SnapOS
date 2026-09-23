# SnapOS on NixOS

SnapOS is built on NixOS. It imports `nixpkgs` and adds its own module
([`modules/snapos.nix`](modules/snapos.nix)); it does not fork nixpkgs.

## Two systems, one flake

- **`snapos`**, built from [`configuration.nix`](../configuration.nix): the
  installed system. Budgie desktop, SnapGuard, SnapWeb, the C tools, GNOME
  Software with Flatpak.
- **`snapos-installer`**, built from [`iso.nix`](iso.nix): a text-only live image
  that starts [`installer/snap-install-nixos.sh`](installer/snap-install-nixos.sh).
  The installer asks for keyboard, language, time zone, disk, account,
  appearance and graphics, then runs `nixos-install --flake` for the `snapos`
  system.

`snapos-light` is the same system with `snapos.appearance = "light"`; CI builds
both so a light install cannot break unnoticed.

## Build

```bash
nix build .#iso             # result/iso/snapos-installer.iso
nix build .#toplevel        # the installed system
nix build .#toplevel-light  # the installed system, light appearance
nix build .#snapos-tools    # only the C tools
nix develop                 # a shell with gcc and make for src/
```

GitHub Actions does the same: `build-nixos-iso.yml` builds the system, the light
system and the ISO; `desktop-test.yml` boots the desktop in a VM;
`snapguard-av.yml` checks SnapGuard against the official ClamAV; `snap-deb.yml`
creates a real Debian layer and installs, runs and removes a real `.deb` in it.

## On the installed system

`/etc/snapos` holds a copy of this repository (`/etc/nixos` is a link to it).
The installer writes four files next to `configuration.nix`:
`hardware-configuration.nix`, `local.nix` (your choices, including
`snapos.appearance`), `graphics.nix` and `release` (the commit the system was
installed from, which `snapos update` compares with the latest release).

- `snapos config` opens `configuration.nix`.
- `snapos rebuild` runs `nixos-rebuild switch --flake path:/etc/snapos#snapos`.
- `snapos update` downloads the latest release into `/etc/snapos`, keeps the
  user's files, rebuilds, and runs `snap-deb sync` and `snap-deb upgrade`.

Both ask for root through `sudo` by themselves (`src/snapos.c`).

## Extra packages

- **`custom-apps/<name>/default.nix`** becomes `pkgs.<name>` automatically.
- **`/etc/snapos/debs/*.deb`**, added with `snap-deb`, are installed by
  `snap-deb sync` (which `snapos rebuild` runs) into a Debian layer at
  `/var/lib/snapdeb`, with the libraries they need from the Debian archive.
  The module puts the layer's exported menu entries and commands on
  `XDG_DATA_DIRS` and `PATH`; `src/snap-deb.c` does the rest.
