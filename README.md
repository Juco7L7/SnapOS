<p align="center">
  <img src="docs/desktop.png" alt="The SnapOS desktop: red Snappy wallpaper, a terminal with fastfetch, and the SnapGuard window" width="720">
</p>

<h1 align="center">SnapOS</h1>

<p align="center">A declarative, security-focused Linux distribution built on NixOS.</p>

> [!CAUTION]
> **Flashing the ISO: use [balenaEtcher](https://etcher.balena.io).** Select `snapos-installer.iso`, select your USB drive, click *Flash!* and wait for the validation to finish. Do not copy the file onto the drive by hand, and do not use "ISO mode" tools (in Rufus, choose *DD Image mode*). If the boot stops with `unable to read id index table`, the download or the USB drive is damaged: compare the SHA-256 of the file with the one on the release page, then download and flash again.

> [!WARNING]
> **Versions before V2.0 (V1 and V1.1) should not be used any more.** They run
> NixOS 24.11 with Linux 6.6.94, a base that stopped receiving security fixes
> on 30 June 2025: every kernel vulnerability found since then, including the
> ones that let a normal user become root, is unpatched there. Their Debian
> layer (V1.1) also ran a package's install script as root with every Linux
> capability and no pid namespace, so a malicious `.deb` could leave the layer
> and take over the machine. **V2.0 moves to NixOS 26.05 with Linux 6.18**, gives
> that root only the powers dpkg needs, and updates itself from now on. Install
> V2.0 from the Releases page (boot the image and choose "Update SnapOS (keeps
> your files)" on an existing system).

---

SnapOS is a Budgie desktop with a red theme, a built-in defender (SnapGuard),
its own web browser (SnapWeb) and a guided terminal installer. The whole system
is described in one file, `configuration.nix`, and every change can be rolled
back from the boot menu.

## Declaring

<p align="center">
  <img src="branding/snappy-declares.gif" alt="Snappy writes apps into configuration.nix with a pencil, then rebuilds the system" width="640">
</p>

On most systems you install things one by one and the computer slowly turns
into a pile of changes nobody remembers. SnapOS is **declarative**: you do not
install, you *declare*. One file, `configuration.nix`, lists what the system
is (its programs, services, look and settings). `snapos config` opens that
file, `snapos rebuild` makes the computer match it. Add a name to the list and
rebuild: the program is there. Remove the name and rebuild: it is gone, with
nothing left behind. Every rebuild is kept, so the boot menu can start any
earlier version of the system if something goes wrong.

`.deb` files follow the same idea: `snap-deb` declares them in a folder next
to `configuration.nix`, and `snapos rebuild` applies that folder too (see
[Debian packages](#debian-packages-deb)).

## What you get

- **One file describes the system.** `snapos config` opens it, `snapos rebuild`
  applies it.
- **SnapGuard**, the defender, based on ClamAV. Threats go to quarantine and
  nothing is deleted without your approval. One command, `snapguard`, opens the
  window and does everything else.
- **SnapWeb**, the browser: official Firefox with a dark and red look, a start
  page, an empty bookmarks bar and Google search.
- **Software**: GNOME Software with Flatpak and Flathub. Open a `.deb` file and
  SnapOS scans it, then installs it in a real Debian layer with everything it
  needs, so programs made for Debian and Ubuntu just work (`snap-deb`).
- **A dock** 45 pixels high with the defender, the browser, the programs store
  and the terminal pinned.
- **Dark or light desktop**, chosen in the installer: theme, icons, wallpaper,
  login screen, the SnapGuard shield and the SnapWeb browser all follow it. On the
  light desktop every text is black or red.
- **SnapHelper**, a short tour that opens the first time you log in (also in the
  menu): declaring apps, opening a `.deb`, SnapGuard. Use the arrow keys or the
  Next and Back buttons.
- **Terminal installer** in ten steps, in English or Portuguese, for BIOS and
  UEFI computers.
- **Graphics that adapt.** On laptops with two graphics chips the installer can
  use the Intel chip only.
- **Small C tools** for everything else, in [`src/`](src/).

## Install

<p align="center">
  <img src="branding/snappy-install.gif" alt="Snappy walks through the ten installer steps" width="640">
</p>

1. Download `snapos-installer.iso` from the Releases page.
2. Write it to a USB drive with balenaEtcher, or with `dd`.
3. Boot from the USB drive. The installer starts by itself.

The installer asks for: network, keyboard, language, time zone, disk, account,
appearance (dark or light) and graphics, then shows a review before it
installs. English is the default;
choose Portuguese on the first screen if you prefer it.

If SnapOS is already installed, run the installer again and pick **Update SnapOS
(keeps your files)**.

## Dark or light

The installer asks whether you want a dark or a light desktop. The theme,
icons, wallpaper, login screen, the SnapGuard shield and SnapWeb all follow it.
To switch later, change `snapos.appearance` in `/etc/snapos/local.nix` and run
`snapos rebuild`.

<p align="center">
  <img src="branding/snappy-appearance.gif" alt="Snappy next to a SnapOS desktop that switches between dark and light" width="640">
</p>

## Commands

| Command | Description |
| --- | --- |
| `snapos config` | open `configuration.nix` in your editor |
| `snapos rebuild` | apply your changes to the system |
| `snapos update` | install the latest SnapOS release, keeping your files (checked at every login) |
| `snapos version` | the SnapOS version this system runs (also in `fastfetch` and `/etc/os-release`) |
| `snapos doctor` | check graphics, boot and antivirus problems |
| `snap-deb FILE.deb` | scan a `.deb`, then add it to SnapOS (also what double-clicking one does) |
| `snap-deb list` / `remove NAME` | packages added this way |
| `snap-deb run CMD` / `shell` / `status` | run a command in the Debian layer, open a shell there, see its state |
| `snapctl run <program>` | scan, draft and run a program that is not installed |
| `snapctl save` / `discard` / `list` / `diff` / `status` | keep or drop drafted programs |
| `snapguard` | open the SnapGuard window |
| `snapguard scan <file or folder>` | scan from the command line |
| `snapguard status` | is the antivirus working? |
| `snapguard quarantine <path>` | contain a file |
| `snapguard list` | show what is in quarantine |
| `snapguard restore <name>` / `delete <name>` / `trust <path>` | decide what happens to a contained file |
| `snapweb` | the web browser |
| `snaphelper` | the SnapHelper tour (opens by itself the first time you log in) |
| `snappy` / `snappy feed` / `snappy pet` | the mascot |
| `fastfetch` | system information |

`snapos` asks for root through `sudo` on its own. The system lives in
`/etc/snapos` (`/etc/nixos` points there, so NixOS tools work too).

## Updates

At every login SnapOS asks GitHub whether a newer release exists. If one does,
a window shows its notes and an **Install now** button. Installing runs
`snapos update` in a terminal, in four steps:

1. **Checking**: the computer is x86_64, `/nix` has room, the release is not
   older than the installed one (by date, and by the `VERSION` number once the
   package is downloaded; `--force` overrides), and the release ships its
   source package with a checksum.
2. **Downloading**: the source package is fetched and its SHA-256 compared with
   the checksum the release carries; a mismatch stops everything.
3. **Keeping your files**: `configuration.nix`, `local.nix`, the hardware
   file, the graphics mode and your `.deb` files are carried over.
4. **Building the next system**: the new system is built and made the default
   for the *next* boot only; what is running is not touched. The Debian layer
   gets Debian's security updates.

Then you restart. The new system gets one try: it is approved the moment a
normal user logs in. If it never reaches the login screen, or nobody manages
to log in within ten minutes, SnapOS goes back to the previous generation by
itself on the next start and tells you at login. You can run `snapos update`
yourself at any time, and `snapos update check` only asks.
The Wi-Fi network you chose in the installer is kept, so the installed system
connects by itself and updates can download right away.

## Installing programs

Programs are listed in `configuration.nix`:

```nix
environment.systemPackages = with pkgs; [
  curl
  dpkg
  fastfetch
  git
  gnome-software
  pciutils
  snapguard
  snapos-tools
  snapweb
];
```

Add a name and run `snapos rebuild`. `snapctl run <program>` tries a program
first and adds it to the list only when you run `snapctl save`. There is no
`apt`: SnapOS is declarative.

## Debian packages (.deb)

<p align="center">
  <img src="branding/snappy-deb.gif" alt="Snappy opens a .deb, SnapGuard scans it, and it becomes part of SnapOS" width="640">
</p>

A `.deb` is not written to `configuration.nix`. It is declared by `snap-deb`,
which keeps the file in `/etc/snapos/debs/`, and `snapos rebuild` applies that
folder the same way it applies the configuration. Double-click a `.deb` (or run
`snap-deb FILE.deb`) and this happens:

1. **SnapGuard scans the file.** A threat goes to quarantine and nothing is
   installed. A file that cannot be scanned is refused unless you insist.
2. **The file is declared**: copied to `/etc/snapos/debs/`. A newer version of
   the same package replaces the older one.
3. **`snapos rebuild` installs it in the Debian layer**, a small real Debian
   (Debian 13) that lives in `/var/lib/snapdeb`. It is created the first time you
   add a `.deb`, from the official Debian archive with its signatures checked,
   and downloads about 300 MB once. Inside it, Debian's own `apt` installs the
   package and fetches the libraries it needs, so the program gets exactly what
   it was built for.
4. **The program is exported**: its menu entry, icon and command appear on the
   desktop like any other. When you open it, it runs inside the layer with your
   home folder, screen, sound, D-Bus and settings.

To see what is declared and whether it is installed, run `snap-deb list`. To
take a package out, run `snap-deb remove NAME` (the package name, or the file
name shown by `list`) and then `snapos rebuild`: the package and the libraries
only it used are removed from the layer, and its menu entry disappears.

```bash
snap-deb list                   # declared .deb files and their state
snap-deb remove discord         # take one out (then: snapos rebuild)
snap-deb run discord            # start a program from the layer by hand
snap-deb shell                  # a shell inside the Debian layer
snap-deb status                 # the layer: Debian version, packages, tools
```

What the layer does not do: it runs programs, not services. A `.deb` that
installs a system service (a VPN, Docker, a driver) is refused with a message,
because it would install but never work; look for that software on
search.nixos.org and declare it in `configuration.nix` instead. The layer is a
compatibility layer, not a sandbox: a program in it reads and writes your files
like a native one, which is why SnapGuard scans every `.deb` first.

## SnapGuard

<p align="center">
  <img src="branding/snappy-defends.gif" alt="Snappy contains a threat and moves it to quarantine" width="640">
</p>

SnapGuard combines ClamAV, a SnapOS hash list and a trust list. When a scan
finds a threat, the file is moved to quarantine and its execute bits are
removed. You then choose to **keep and trust** it or **delete** it.


The window offers a quick scan of Downloads, folder and file scans and a
quarantine view. While it scans it shows each file as it is checked, and when
it finishes it says how many files were checked, what was found, and what could
not be checked. `snapguard status` shows whether the engine,
the daemon and the virus database are in place. The database downloads on the
first boot with internet. See [docs/SECURITY.md](docs/SECURITY.md) for the
design.

## Build it yourself

You need Nix with flakes enabled.

```bash
nix build .#iso             # the installer image, in result/iso/
nix build .#toplevel        # the installed system
make all gui && make test   # the C tools and their tests
```

Apps that SnapOS ships in its own version live in [`custom-apps/`](custom-apps/).
Each folder there becomes a package automatically; SnapWeb is one of them.

## Repository layout

| Path | Contents |
| --- | --- |
| `configuration.nix` | the system declaration and the program list |
| `flake.nix` | overlay, `.#iso`, `.#toplevel`, `.#snapos-tools`, checks |
| `nix/modules/` | desktop, theme, defender, hardware and defaults |
| `nix/iso.nix` | the installer image |
| `nix/installer/` | the installer |
| `nix/pkgs/` | SnapOS packages: tools, SnapGuard window, icons, wallpapers |
| `nix/tests/` | virtual machine tests |
| `custom-apps/` | apps SnapOS ships in its own version (SnapWeb) |
| `src/` | the C tools |
| `security/` | SnapGuard signature list and allowlist |
| `docs/` | the security design |
| `branding/` | logo, icons, wallpapers, fastfetch configuration |
| `tests/` | tests for the C tools and the repository |
| `.github/workflows/` | ISO build, desktop test, SnapGuard check and the Debian layer test |

## License

MIT, see [LICENSE](LICENSE).

---

SnapOS was created as the final project (TCC) of a 15-year-old student. The goal
is to give you total control of your system while shipping an OS with strong
built-in security, good performance, and a friendlier learning curve than Nix.

AI assisted (Sonnet 5, Fable 5.1)
