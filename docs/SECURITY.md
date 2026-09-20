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

## The SnapGuard window

`snapguard` with no arguments opens the window (`src/snapguard-gui.c`, GTK3, installed
as `snapguard-gui`), a skin over the same commands: quick
scan of Downloads, scan a folder or a file, and a Quarantine tab with
**Keep & trust** and **Delete**. It calls `snapguard scan`, `quarantine` and the rest, so the rules are the
same. While it scans it shows each file as it is checked; when it finishes it
reports the files checked, the threats, the files that could not be checked and
the time taken, and sends a desktop notification.

## Not done yet

- Real-time watching of Downloads and USB drives.
- Talking to `clamd` over its socket protocol instead of running `clamdscan`.
- Sandboxing for programs that are not trusted yet.

## Threat model

SnapOS aims to stop a user from running malware they downloaded or that came
on removable media: an undeclared binary is scanned before it runs. It does not
defend against a fully compromised root or against supply-chain attacks on the
declared package set.
