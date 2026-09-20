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
nix build .#checks.x86_64-linux.deb   # the .deb packaging test
nix develop                 # a shell with gcc and make for src/
```

GitHub Actions does the same: `build-nixos-iso.yml` builds the system, the light
system and the ISO; `desktop-test.yml` boots the desktop in a VM;
`snapguard-av.yml` checks SnapGuard against the official ClamAV.

## On the installed system

`/etc/nixos` holds a copy of this repository. The installer writes three files
next to `configuration.nix`: `hardware-configuration.nix`, `local.nix` (your
choices, including `snapos.appearance`) and `graphics.nix`.

- `snapos config` opens `configuration.nix`.
- `snapos rebuild` runs `nixos-rebuild switch --flake path:/etc/nixos#snapos`.

Both ask for root through `sudo` by themselves (`src/snapos.c`).

## Extra packages

- **`custom-apps/<name>/default.nix`** becomes `pkgs.<name>` automatically.
- **`/etc/nixos/debs/*.deb`**, added with `snap-deb`, are built into packages by
  [`pkgs/snapos-deb.nix`](pkgs/snapos-deb.nix) on the next rebuild.
