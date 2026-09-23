# SnapOS security model

SnapOS's defender is **SnapGuard**: ClamAV (`services.clamav.daemon` and
`services.clamav.updater`, enabled by the SnapOS module) wrapped by
`snapguard` (`src/snapguard.c`) with SnapOS policy.

## Contain and ask, never delete

An infected file is never deleted automatically. `snapguard` moves it to
quarantine and strips its exec bits, then the user decides. Quarantine lives in
`~/.local/share/snapos/quarantine` for a normal user and in
`/var/lib/snapos/quarantine` for root:

```
snapguard scan <path>...              exit 0 clean, 1 infected, 2 could not scan
snapguard status                      engine, daemon, database, ACTIVE or LIMITED
snapguard quarantine <path>           contain a file
snapguard restore <name> [--trust]    put it back, optionally trust it
snapguard delete <name>               delete it for good
snapguard list                       show the quarantine
snapguard                            open the window
snapguard trust <path>                add its hash to the allowlist
```

## Scan order

1. Allowlist: your own `~/.local/share/snapos/allow.txt` plus the system-wide
   `/etc/snapos/allow.txt` (SHA-256 of files you trust).
2. Deny-list: `/etc/snapos/signatures.txt` (SHA-256 of known-bad files, shipped
   with the system).
3. ClamAV. If the daemon is running, `clamdscan --fdpass` (the daemon reads the
   file through a descriptor, so it needs no access to the user's folders).
   Otherwise one `clamscan` run for all the files of a scan, so the signature
   database is loaded once.

A file that could not be scanned is reported as such, never as clean.
`snapguard status` says whether the engine, the daemon and the virus database
are in place. The database downloads on the first boot with internet, and the
daemon retries by itself until it succeeds.

`snapctl run <program>` scans any undeclared program through `snapguard`
before it is drafted or run. `snap-deb` does the same for `.deb` files.

## The Debian layer

Programs from `.deb` files run inside a Debian layer (`/var/lib/snapdeb`,
`src/snap-deb.c`) through bubblewrap: the layer's root is mounted read-only,
`/tmp` and `/run` are private, the environment starts empty, and the program
gets the user's home, display, sound and D-Bus sockets. This is a compatibility
layer, not a sandbox: a program in the layer can read and write the user's
files exactly like a native one. Installing a `.deb` runs its maintainer
scripts as root inside the layer, as on Debian, which is why SnapGuard scans
every `.deb` first; that root runs with only the capabilities dpkg needs (no
`SYS_ADMIN`, no `MKNOD`) and in its own pid, ipc and uts namespaces, so an
install script cannot mount, create device nodes or enter the host's
namespaces. The layer itself is created with Debian's
`debootstrap` against Debian's archive keys, which SnapOS ships in
`/etc/snapos/debian-archive-keyring.gpg` (from the `debian-archive-keyring`
package; without it no layer is created), and packages are
installed by Debian's `apt` from the official archive with security updates
enabled. Only root (through `snapos rebuild` or `snap-deb sync`) changes the
layer.

## The SnapGuard window

`snapguard` with no arguments opens the window (`src/snapguard-gui.c`, GTK3, installed
as `snapguard-gui`), a skin over the same commands: quick
scan of Downloads, scan a folder or a file, and a Quarantine tab with
**Keep & trust** and **Delete**. It calls `snapguard scan`, `quarantine` and the rest, so the rules are the
same. While it scans it shows each file as it is checked; when it finishes it
reports the files checked, the threats, the files that could not be checked and
the time taken, and sends a desktop notification.

## Not done yet

- Real-time watching of Downloads and USB drives (planned for the next release).
- Talking to `clamd` over its socket protocol instead of running `clamdscan`.
- Sandboxing for programs that are not trusted yet.

## Updates

`snapos update` (`src/snapos.c`) never replaces the running system. It checks
the machine and the release first, verifies the SHA-256 of the source package
against the checksum attached to the release (both come from the release
page over HTTPS, so this catches a corrupted download, not a compromised
release), builds the new NixOS generation with `nixos-rebuild boot`, and
leaves a marker in `/var/lib/snapos/update`. At boot `snapos guard boot` lets
a new generation start once; `snapos guard approve` removes the marker when a
user with uid ≥ 1000 has logged in, and after ten minutes without a login, or
on the next boot with the marker still there, switches the profile back to the
recorded generation, restores the previous `/etc/snapos`, and restarts. Only
root writes the markers; users only read them. Downloads go to
`/var/lib/snapos/update/work`, a folder only root can enter, never to `/tmp`.

## Advisories

- **V1 and V1.1 (NixOS 24.11, Linux 6.6.94).** The base stopped receiving
  security fixes on 30 June 2025; kernel vulnerabilities found since then,
  including local privilege escalations, are unpatched in those releases.
  Fixed by V2.0, which moves to NixOS 26.05 (Linux 6.18) and updates itself.
- **V1.1 Debian layer.** `snap-deb sync` ran a package's install scripts as
  root with all capabilities and without a pid namespace, so a malicious
  `.deb` could mount devices or enter the host's namespaces and leave the
  layer. Fixed in V2.0: only the capabilities dpkg needs, own pid, ipc and uts
  namespaces.
- Found and fixed during the V2.0 review, never released: the updater wrote
  its downloads to `/tmp` under predictable names as root (a local user could
  plant a symbolic link there); file names reached desktop notifications as
  markup.

## Threat model

SnapOS aims to stop a user from running malware they downloaded or that came
on removable media: an undeclared binary is scanned before it runs. It does not
defend against a fully compromised root or against supply-chain attacks on the
declared package set.
