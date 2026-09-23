#!/usr/bin/env bash
set -u

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BIN="$ROOT/bin"
TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT
PASS=0
FAIL=0

check() {
    local name="$1"
    shift
    if "$@" >/dev/null 2>&1; then
        PASS=$((PASS+1))
        printf '  ok    %s\n' "$name"
    else
        FAIL=$((FAIL+1))
        printf '  FAIL  %s\n' "$name"
    fi
}

check_eq() {
    local name="$1" want="$2" got="$3"
    if [ "$want" = "$got" ]; then
        PASS=$((PASS+1))
        printf '  ok    %s\n' "$name"
    else
        FAIL=$((FAIL+1))
        printf '  FAIL  %s (want "%s", got "%s")\n' "$name" "$want" "$got"
    fi
}

EICAR='X5O!P%@AP[4\PZX54(P^)7CC)7}$EICAR-STANDARD-ANTIVIRUS-TEST-FILE!$H+H*'

section() { printf '\n%s\n' "$1"; }

[ -x "$BIN/snapctl" ] || { echo "run make first"; exit 2; }

section "snappy"
export HOME="$TMP/home"
mkdir -p "$HOME"
unset XDG_DATA_HOME
out="$("$BIN/snappy" 2>&1)"
check "prints the original sprite" bash -c "diff <(printf '%s\n' \"\$1\" | sed -n 2,9p | sed 's/[[:space:]]*\$//') <(sed 's/[[:space:]]*\$//' '$ROOT/branding/snappy-mascot.txt')" _ "$out"
check "no color codes when piped" bash -c "! printf '%s' \"\$1\" | grep -q \$'\\033'" _ "$out"
"$BIN/snappy" pet >/dev/null 2>&1
check "pet saves xp" grep -q '^xp 2' "$HOME/.local/share/snapos/snappy.state"

section "Snappy status"
out="$("$BIN/snappy" 2>&1)"
check "shows the name and the XP" bash -c "printf '%s' \"\$1\" | grep -q 'Snappy' && printf '%s' \"\$1\" | grep -q 'XP'" _ "$out"
check "has no level" bash -c "! printf '%s' \"\$1\" | grep -qi 'level'" _ "$out"

section "snapctl"
mkdir -p "$TMP/nix"
cp "$ROOT/configuration.nix" "$TMP/nix/"
export SNAPOS_NIX_DIR="$TMP/nix"
declared="$("$BIN/snapctl" status | grep -c '✓')"
check_eq "reads the declared package list" "9" "$declared"
printf 'htop\n' > "$TMP/nix/pending"
"$BIN/snapctl" save >/dev/null 2>&1
check "save writes the package" grep -q '^    htop$' "$TMP/nix/configuration.nix"
check "save keeps the markers" bash -c "grep -q 'snapos:packages:begin' '$TMP/nix/configuration.nix' && grep -q 'snapos:packages:end' '$TMP/nix/configuration.nix'"
printf 'btop\n' > "$TMP/nix/pending"
"$BIN/snapctl" discard btop >/dev/null 2>&1
check "discard drops a draft" bash -c "! grep -q btop '$TMP/nix/pending'"
unset SNAPOS_NIX_DIR

section "snapguard"
G="$BIN/snapguard"
export SNAP_GUARD_SIGS="$ROOT/security/signatures.txt"
printf '%s' "$EICAR" > "$TMP/virus.bin"
echo "plain text" > "$TMP/clean.txt"
"$G" scan "$TMP/virus.bin" >/dev/null 2>&1
check_eq "flags the EICAR test file" "1" "$?"
"$G" scan "$TMP/clean.txt" >/dev/null 2>&1
rc=$?
check "does not flag a clean file" bash -c "[ $rc -eq 0 ] || [ $rc -eq 2 ]"
"$G" quarantine "$TMP/virus.bin" >/dev/null 2>&1
check "quarantine removes the file" bash -c "[ ! -e '$TMP/virus.bin' ]"
check "quarantine is per user" bash -c "ls '$HOME/.local/share/snapos/quarantine' | grep -q virus.bin"
check "list-quarantine shows it" bash -c "'$G' list-quarantine | grep -q virus.bin"
name="$(ls "$HOME/.local/share/snapos/quarantine" | grep virus.bin | grep -v index | head -1)"
"$G" restore "$name" --trust >/dev/null 2>&1
check "restore puts the file back" bash -c "[ -e '$TMP/virus.bin' ]"
check "restore empties the quarantine list" bash -c "'$G' list-quarantine | grep -q empty"
"$G" scan "$TMP/virus.bin" >/dev/null 2>&1
check_eq "a trusted file scans clean" "0" "$?"
"$G" quarantine "$TMP/virus.bin" >/dev/null 2>&1
name="$(ls "$HOME/.local/share/snapos/quarantine" | grep virus.bin | grep -v index | head -1)"
"$G" delete "$name" >/dev/null 2>&1
check "delete removes it for good" bash -c "! ls '$HOME/.local/share/snapos/quarantine' | grep -q virus.bin"
check "the help lists every command" bash -c "out=\$('$G' help); for w in scan status quarantine list restore delete trust; do printf '%s' \"\$out\" | grep -q \"snapguard \$w\" || exit 1; done"
printf '%s' "$EICAR" > "$TMP/virus2.bin"
"$G" quarentine "$TMP/virus2.bin" >/dev/null 2>&1
check "quarentine works as an alias of quarantine" bash -c "[ ! -e '$TMP/virus2.bin' ]"
check "list works as an alias of list-quarantine" bash -c "'$G' list | grep -q virus2.bin"
mkdir -p "$TMP/folder/sub"
printf '%s' "$EICAR" > "$TMP/folder/sub/bad.bin"
echo "fine" > "$TMP/folder/ok.txt"
: > "$TMP/noallow"
out="$(SNAP_GUARD_ALLOW="$TMP/noallow" "$G" scan "$TMP/folder" 2>&1)"; rc=$?
check_eq "scanning a folder finds the threat inside" "1" "$rc"
check "scanning a folder ends with a summary" bash -c "printf '%s' \"\$1\" | grep -q '2 files checked'" _ "$out"
mkdir -p "$TMP/gui"
printf '#!/bin/sh\necho GUI-OPENED\n' > "$TMP/gui/snapguard-gui"
chmod +x "$TMP/gui/snapguard-gui"
out="$(env DISPLAY=:9 PATH="$TMP/gui:$PATH" "$G" 2>&1)"
check_eq "snapguard alone opens the window" "GUI-OPENED" "$out"
out="$(env -u DISPLAY -u WAYLAND_DISPLAY PATH="$TMP/gui:$PATH" "$G" 2>&1)"
check "without a graphical session it prints the help" bash -c "printf '%s' \"\$1\" | grep -q 'snapguard scan'" _ "$out"
unset SNAP_GUARD_SIGS

section "snapguard and ClamAV (stand-in scanners)"
CL="$TMP/clam"
mkdir -p "$CL/bin" "$CL/db"
: > "$CL/empty"
touch "$CL/db/test.hdb"
printf '#!/bin/sh\necho "ERROR: Could not connect to clamd on LocalSocket /x: No such file or directory" >&2\nexit 2\n' > "$CL/bin/clamdscan"
cat > "$CL/bin/clamscan" <<STUB
#!/bin/sh
echo run >> "$CL/calls"
rc=0
for f in "\$@"; do
  case "\$f" in -*) continue;; esac
  case "\$(cat "\$f")" in *bad*) echo "\$f: Test.Sig FOUND"; rc=1;; *) echo "\$f: OK";; esac
done
exit \$rc
STUB
chmod +x "$CL/bin/clamdscan" "$CL/bin/clamscan"
echo "this is bad" > "$CL/bad.bin"
echo "this is fine" > "$CL/good.txt"
gs() {
    env PATH="$CL/bin:$PATH" SNAP_GUARD_SIGS="$CL/empty" SNAP_GUARD_ALLOW="$CL/empty" \
        SNAP_GUARD_QUARANTINE="$CL/q" SNAP_GUARD_CLAMDB="$CL/db" SNAP_GUARD_CLAMD_SOCKET="$CL/none.sock" \
        "$G" "$@"
}
out="$(gs scan "$CL/bad.bin" 2>&1)"; rc=$?
check_eq "falls back to clamscan when clamd is down: infected" "1" "$rc"
check "names the signature that ClamAV reported" bash -c "printf '%s' \"\$1\" | grep -q 'ClamAV: Test.Sig'" _ "$out"
gs scan "$CL/good.txt" >/dev/null 2>&1
check_eq "falls back to clamscan when clamd is down: clean" "0" "$?"
echo "this is bad too" > "$CL/bad2.bin"
: > "$CL/calls"
out="$(gs scan "$CL/bad.bin" "$CL/good.txt" "$CL/bad2.bin" 2>&1)"; rc=$?
check_eq "many files: a threat among them gives 1" "1" "$rc"
check_eq "many files: one ClamAV run for all of them" "1" "$(wc -l < "$CL/calls" | tr -d ' ')"
check "many files: each result is reported" bash -c "[ \$(printf '%s\n' \"\$1\" | grep -c INFECTED) -eq 2 ] && printf '%s' \"\$1\" | grep -q 'good.txt'" _ "$out"
printf '#!/bin/sh\necho "$@" > "%s/args"\nexit 0\n' "$CL" > "$CL/bin/clamdscan"
gs scan "$CL/good.txt" >/dev/null 2>&1
check "clamdscan is called with --fdpass" grep -q -- '--fdpass' "$CL/args"
printf '#!/bin/sh\necho "ERROR: nope" >&2\nexit 2\n' > "$CL/bin/clamdscan"
printf '#!/bin/sh\necho "ERROR: nope" >&2\nexit 2\n' > "$CL/bin/clamscan"
out="$(gs scan "$CL/good.txt" 2>&1)"; rc=$?
check_eq "an engine error is UNKNOWN, never clean" "2" "$rc"
check "the error is explained" bash -c "printf '%s' \"\$1\" | grep -q 'could not scan: ERROR: nope'" _ "$out"
out="$(gs status 2>&1)"
check "status is ACTIVE with an engine and a database" bash -c "printf '%s' \"\$1\" | grep -q 'protection    : .*ACTIVE'" _ "$out"
out="$(env PATH="$CL/bin:$PATH" SNAP_GUARD_CLAMDB="$CL/nodb" SNAP_GUARD_CLAMD_SOCKET="$CL/none.sock" "$G" status 2>&1)"
check "status is LIMITED without a database" bash -c "printf '%s' \"\$1\" | grep -q 'protection    : .*LIMITED'" _ "$out"

section "snap-deb"
if command -v dpkg-deb >/dev/null 2>&1; then
    D="$BIN/snap-deb"
    mkdev() {
        local ver="$1" arch="$2" dir="$TMP/deb/$1-$2"
        mkdir -p "$dir/DEBIAN" "$dir/opt/hello"
        printf 'Package: hello-snap\nVersion: %s\nArchitecture: %s\nMaintainer: test\nDescription: a test package\nDepends: libc6\n' "$ver" "$arch" > "$dir/DEBIAN/control"
        printf '#!/bin/sh\necho hi\n' > "$dir/opt/hello/hello"
        chmod +x "$dir/opt/hello/hello"
        dpkg-deb --build "$dir" "$TMP/deb/hello-snap_${ver}_${arch}.deb" >/dev/null 2>&1
    }
    mkdir -p "$TMP/deb" "$TMP/fakeguard"
    mkdev 1.0 amd64; mkdev 2.0 amd64; mkdev 1.0 arm64
    cat > "$TMP/fakeguard/snapguard" <<GUARD
#!/bin/sh
case "\$1" in
  scan) echo "  scanned \$2"; exit \${FAKE_RC:-0} ;;
  quarantine) echo "\$2" >> "$TMP/quarantined"; exit 0 ;;
esac
GUARD
    chmod +x "$TMP/fakeguard/snapguard"
    export SNAPOS_NIX_DIR="$TMP/nixdeb"
    mkdir -p "$SNAPOS_NIX_DIR"
    sd() { env PATH="$TMP/fakeguard:$PATH" "$D" "$@"; }

    out="$(sd info "$TMP/deb/hello-snap_1.0_amd64.deb" 2>&1)"
    check "info shows the package" bash -c "printf '%s' \"\$1\" | grep -q 'hello-snap'" _ "$out"
    sd info "$ROOT/README.md" >/dev/null 2>&1
    check_eq "a file that is not a .deb is refused" "2" "$?"
    sd add "$TMP/deb/hello-snap_1.0_arm64.deb" >/dev/null 2>&1
    check_eq "a package for another architecture is refused" "2" "$?"

    sd add "$TMP/deb/hello-snap_1.0_amd64.deb" >/dev/null 2>&1
    check_eq "add succeeds when the scan is clean" "0" "$?"
    check "add declares the file in the debs folder" test -f "$SNAPOS_NIX_DIR/debs/hello-snap_1.0_amd64.deb"
    check "list shows it" bash -c "'$D' list | grep -q hello-snap_1.0"
    sd add "$TMP/deb/hello-snap_2.0_amd64.deb" >/dev/null 2>&1
    check "a newer version replaces the older one" bash -c "[ -f '$SNAPOS_NIX_DIR/debs/hello-snap_2.0_amd64.deb' ] && [ ! -f '$SNAPOS_NIX_DIR/debs/hello-snap_1.0_amd64.deb' ]"
    "$D" remove hello-snap >/dev/null 2>&1
    check "remove takes it out again" bash -c "[ -z \"\$(ls '$SNAPOS_NIX_DIR/debs')\" ]"
    "$D" remove nothing-here >/dev/null 2>&1
    check_eq "removing something unknown is an error" "2" "$?"
    sd add "$TMP/deb/hello-snap_2.0_amd64.deb" >/dev/null 2>&1
    "$D" remove hello-snap_2.0_amd64.deb >/dev/null 2>&1
    check "remove also accepts the file name" bash -c "[ -z \"\$(ls '$SNAPOS_NIX_DIR/debs')\" ]"

    FAKE_RC=1 sd add "$TMP/deb/hello-snap_1.0_amd64.deb" >/dev/null 2>&1
    check_eq "a threat is refused" "1" "$?"
    check "a threat is contained" grep -q hello-snap_1.0_amd64.deb "$TMP/quarantined"
    check "a threat is not declared" bash -c "[ -z \"\$(ls '$SNAPOS_NIX_DIR/debs')\" ]"
    FAKE_RC=2 sd add "$TMP/deb/hello-snap_1.0_amd64.deb" >/dev/null 2>&1
    check_eq "a file that could not be scanned is refused" "2" "$?"
    FAKE_RC=2 sd add "$TMP/deb/hello-snap_1.0_amd64.deb" --force >/dev/null 2>&1
    check "--force adds it anyway" test -f "$SNAPOS_NIX_DIR/debs/hello-snap_1.0_amd64.deb"
    mkdir -p "$TMP/deb/svc/DEBIAN" "$TMP/deb/svc/usr/bin" "$TMP/deb/svc/lib/systemd/system"
    printf 'Package: vpn-thing\nVersion: 1.0\nArchitecture: amd64\nMaintainer: test\nDescription: a program with a daemon\n' > "$TMP/deb/svc/DEBIAN/control"
    printf '[Unit]\nDescription=daemon\n[Service]\nExecStart=/usr/bin/vpn-thing\n' > "$TMP/deb/svc/lib/systemd/system/vpn-thing.service"
    printf '#!/bin/sh\n' > "$TMP/deb/svc/usr/bin/vpn-thing"
    dpkg-deb --build "$TMP/deb/svc" "$TMP/deb/vpn-thing_1.0_amd64.deb" >/dev/null 2>&1
    out="$(sd add "$TMP/deb/vpn-thing_1.0_amd64.deb" 2>&1)"; rc=$?
    check_eq "a package with a system service is refused" "2" "$rc"
    check "and the refusal names the service and the way out" bash -c "printf '%s' \"\$1\" | grep -q 'vpn-thing.service' && printf '%s' \"\$1\" | grep -q 'search.nixos.org'" _ "$out"
    check "and it is not declared" bash -c "! ls '$SNAPOS_NIX_DIR/debs' | grep -q vpn-thing"
    unset SNAPOS_NIX_DIR
fi
check "the .deb opener is the default for .deb files" grep -q 'application/vnd.debian.binary-package' "$ROOT/branding/snap-deb.desktop"

section "snap-deb: the Debian layer"
D="$BIN/snap-deb"
L="$TMP/layer"; R="$L/rootfs"
mkdir -p "$R/etc" "$R/var/lib/dpkg/info" "$R/usr/bin" "$R/usr/share/applications" "$R/usr/share/icons/hicolor/48x48/apps" "$R/usr/share/doc/hello-snap"
echo "13.1" > "$R/etc/debian_version"
printf '#!/bin/sh\necho hi\n' > "$R/usr/bin/hello-snap"
chmod +x "$R/usr/bin/hello-snap"
printf '[Desktop Entry]\nType=Application\nName=Hello\nExec=/usr/bin/hello-snap %%U\nTryExec=/usr/bin/hello-snap\nIcon=hello-snap\nDBusActivatable=true\n' > "$R/usr/share/applications/hello-snap.desktop"
: > "$R/usr/share/icons/hicolor/48x48/apps/hello-snap.png"
printf '/usr/bin/hello-snap\n/usr/share/applications/hello-snap.desktop\n/usr/share/icons/hicolor/48x48/apps/hello-snap.png\n/usr/share/doc/hello-snap\n' > "$R/var/lib/dpkg/info/hello-snap.list"
echo "hello-snap hello-snap_1.0_amd64.deb" > "$L/installed"
export SNAPDEB_DIR="$L" SNAPOS_NIX_DIR="$TMP/nixlayer"
mkdir -p "$SNAPOS_NIX_DIR/debs"
: > "$SNAPOS_NIX_DIR/debs/hello-snap_1.0_amd64.deb"
"$D" export >/dev/null 2>&1
check_eq "export succeeds on a layer" "0" "$?"
E="$L/exports"
check "export writes a command wrapper" bash -c "[ -x '$E/bin/hello-snap' ] && grep -q 'exec snap-deb run hello-snap' '$E/bin/hello-snap'"
check "the exported menu entry runs through the layer" grep -q '^Exec=snap-deb run /usr/bin/hello-snap %U' "$E/share/applications/hello-snap.desktop"
check "the exported menu entry drops TryExec and DBusActivatable" bash -c "! grep -qE '^(TryExec|DBusActivatable)=' '$E/share/applications/hello-snap.desktop'"
check "the exported menu entry names its package" grep -q '^X-SnapOS-Package=hello-snap$' "$E/share/applications/hello-snap.desktop"
check "the icon is exported" test -L "$E/share/icons/hicolor/48x48/apps/hello-snap.png"
check "documentation is not exported" bash -c "! find '$E' -name '*doc*' | grep -q ."
out="$("$D" status 2>&1)"
check "status shows the Debian version" bash -c "printf '%s' \"\$1\" | grep -q '13.1'" _ "$out"
check "status counts the installed packages" bash -c "printf '%s' \"\$1\" | grep -q '1 package'" _ "$out"
check "list shows the package as installed" bash -c "'$D' list | grep -q 'hello-snap_1.0_amd64.deb.*installed'"
printf '#!/bin/sh\nprintf "%%s\\n" "$@" > "$FAKE_OUT"\n' > "$TMP/fakebwrap"
chmod +x "$TMP/fakebwrap"
mkdir -p "$TMP/home"
FAKE_OUT="$TMP/bwrap-args" SNAPDEB_BWRAP="$TMP/fakebwrap" HOME="$TMP/home" "$D" run hello-snap --flag
check "run mounts the layer as the root" bash -c "grep -A2 -x -- '--ro-bind' '$TMP/bwrap-args' | grep -A1 -x '$R' | grep -qx '/'"
check "run keeps the home folder" bash -c "grep -A2 -x -- '--bind' '$TMP/bwrap-args' | grep -qx '$TMP/home'"
check "run starts from a clean environment" grep -qx -- '--clearenv' "$TMP/bwrap-args"
check_eq "run passes the command and its arguments" "hello-snap --flag" "$(tail -2 "$TMP/bwrap-args" | tr '\n' ' ' | sed 's/ $//')"
SNAPDEB_DIR="$TMP/nolayer" "$D" run hello-snap >/dev/null 2>&1
check_eq "run without a layer is refused" "2" "$?"
rm -f "$SNAPOS_NIX_DIR/debs/hello-snap_1.0_amd64.deb"
check "list marks a removed file as gone" bash -c "! '$D' list | grep -q hello-snap"
unset SNAPDEB_DIR SNAPOS_NIX_DIR
check "snapos rebuild syncs the Debian layer" grep -q '"snap-deb", "sync"' "$ROOT/src/snapos.c"
check "the module exports the layer to the menu" grep -q 'debLayer}/exports/share' "$ROOT/nix/modules/snapos.nix"
check "the module ships bubblewrap and debootstrap" bash -c "grep -q 'pkgs.bubblewrap' '$ROOT/nix/modules/snapos.nix' && grep -q 'pkgs.debootstrap' '$ROOT/nix/modules/snapos.nix'"
check "the Debian archive keyring is shipped" bash -c "[ -s '$ROOT/security/debian-archive-keyring.gpg' ] && grep -q 'debian-archive-keyring.gpg' '$ROOT/nix/modules/snapos.nix'"
check "the layer is created only from a verified archive" grep -q -- '--keyring=' "$ROOT/src/snap-deb.c"
check "install scripts that call systemctl do not fail" bash -c "grep -q '\"systemctl\", \"service\"' '$ROOT/src/snap-deb.c' && grep -q 'policy-rc.d' '$ROOT/src/snap-deb.c'"
check "debootstrap gets mount on its PATH" grep -q 'util-linux}/bin' "$ROOT/flake.nix"
mkdir -p "$TMP/nolayer2" "$TMP/nixkey/debs"
: > "$TMP/nixkey/debs/hello-snap_1.0_amd64.deb"
out="$(SNAPDEB_DIR="$TMP/nolayer2" SNAPOS_NIX_DIR="$TMP/nixkey" SNAPDEB_KEYRING="$TMP/no-such-keyring" "$D" sync 2>&1)"; rc=$?
check_eq "sync refuses to create a layer without the keyring" "1" "$rc"
check "and says which file is missing" bash -c "printf '%s' \"\$1\" | grep -q 'no-such-keyring is missing'" _ "$out"
check "and leaves nothing behind" bash -c "[ ! -e '$TMP/nolayer2/rootfs.new' ]"

section "snapos"
mkdir -p "$TMP/fake"
printf '#!/bin/sh\necho "SUDO $*"\n' > "$TMP/fake/sudo"
chmod +x "$TMP/fake/sudo"
check "help works" "$BIN/snapos" help
"$BIN/snapos" bogus >/dev/null 2>&1
check_eq "unknown command is an error" "2" "$?"
if [ "$(id -u)" -ne 0 ]; then
    out="$(PATH="$TMP/fake:$PATH" "$BIN/snapos" rebuild 2>&1)"
    check "asks sudo to rebuild" bash -c "printf '%s' \"\$1\" | grep -q 'SUDO .*/snapos rebuild'" _ "$out"
fi

section "apt"
out="$("$BIN/apt" install opsec 2>&1)"
check_eq "install opsec prints nothing without a terminal" "" "$out"
"$BIN/apt" install firefox >/dev/null 2>&1
check_eq "any other package is refused" "1" "$?"
check "the refusal says how to install" bash -c "'$BIN/apt' install firefox 2>&1 | grep -q 'snapos rebuild'"

section "branding paths"
python3 - "$ROOT" <<'CHK'
import os, re, sys
root = sys.argv[1]
missing = []
for base, _, files in os.walk(os.path.join(root, "nix")):
    for f in files:
        if f.endswith(".nix"):
            for ref in re.findall(r"(?:\.\./)+branding/[A-Za-z0-9_./-]+", open(os.path.join(base, f)).read()):
                if not os.path.exists(os.path.normpath(os.path.join(base, ref))):
                    missing.append(ref)
for s in (16, 24, 32, 48, 64, 72, 96, 128, 256, 512):
    if not os.path.exists(os.path.join(root, "branding", "icons", "snapos-%d.png" % s)):
        missing.append("icons/snapos-%d.png" % s)
sys.exit(1 if missing else 0)
CHK
check "every branding file used by Nix exists" test $? -eq 0
check "no OS-logo file is named after the mascot" bash -c "! git -C '$ROOT' ls-files branding | grep -E 'snappy-logo|icons/snappy'"

section "custom apps"
check "snapweb start page exists" test -f "$ROOT/custom-apps/snapweb/start/index.html"
check "snapweb autoconfig has the stylesheet placeholder" grep -q '@css@' "$ROOT/custom-apps/snapweb/autoconfig.js"
check "the app list uses snapweb" grep -q '^    snapweb$' "$ROOT/configuration.nix"
check "snapweb autoconfig has the start page placeholder" grep -q '@start@' "$ROOT/custom-apps/snapweb/autoconfig.js"
check "snapweb turns the autoconfig sandbox off" grep -q 'general.config.sandbox_enabled' "$ROOT/custom-apps/snapweb/default.nix"
check "snapweb shows the bookmarks bar" grep -q 'DisplayBookmarksToolbar = "always"' "$ROOT/custom-apps/snapweb/default.nix"
check "snapweb searches with Google" grep -q 'SearchEngines.Default = "Google"' "$ROOT/custom-apps/snapweb/default.nix"
check "the start page searches with Google" grep -q 'google.com/search' "$ROOT/custom-apps/snapweb/start/index.html"
check "SnapGuard ships the shield icons" grep -q 'snapguard-dark' "$ROOT/nix/pkgs/snapguard.nix"

section "appearance (dark and light)"
INST="$ROOT/nix/installer/snap-install-nixos.sh"
check "installer asks for the appearance" grep -q '^choose_appearance$' "$INST"
check "installer saves it in local.nix" grep -q 'snapos.appearance = "${LOOK}"' "$INST"
steps="$(grep -oE '^ *step [0-9]+ ' "$INST" | grep -oE '[0-9]+' | sort -n | uniq | tr '\n' ' ')"
check_eq "installer steps run from 1 to TOTAL" "1 2 3 4 5 6 7 8 9 10 " "$steps"
check_eq "TOTAL matches the last step" "10" "$(sed -n 's/^TOTAL=//p' "$INST")"
check "the module has the appearance option" grep -q 'appearance = mkOption' "$ROOT/nix/modules/snapos.nix"
check "both wallpapers exist" bash -c "[ -f '$ROOT/branding/wallpapers/snapos-default.jpeg' ] && [ -f '$ROOT/branding/wallpapers/snapos-light.jpeg' ]"
check "both shield icon sets exist" bash -c "[ -f '$ROOT/branding/icons/snapguard-dark-512.png' ] && [ -f '$ROOT/branding/icons/snapguard-light-512.png' ]"
check "CI builds the light system too" grep -q 'toplevel-light' "$ROOT/.github/workflows/build-nixos-iso.yml"
check "SnapWeb has a light look" grep -q 'prefers-color-scheme: light' "$ROOT/custom-apps/snapweb/chrome/userChrome.css"
check "the start page has a light look" grep -q 'prefers-color-scheme: light' "$ROOT/custom-apps/snapweb/start/index.html"

section "SnapHelper and the animations"
for g in snappy-declares snappy-deb snappy-defends snappy-appearance snappy-install; do
    check "$g.gif exists" test -s "$ROOT/branding/$g.gif"
done
check "SnapHelper shows exactly the three animations it installs" bash -c "for g in snappy-declares snappy-deb snappy-defends; do grep -q \"\$g.gif\" '$ROOT/src/snaphelper.c' && grep -q \"\$g\" '$ROOT/nix/pkgs/snaphelper.nix' || exit 1; done; [ \$(grep -o 'snappy-[a-z]*\\.gif' '$ROOT/src/snaphelper.c' | sort -u | wc -l) -eq 3 ]"
check "SnapHelper opens once at first login" grep -q 'Exec=snaphelper --first-run' "$ROOT/nix/modules/snapos.nix"
check "SnapHelper is in the app menu" grep -q 'Exec=snaphelper$' "$ROOT/branding/snaphelper.desktop"
check "SnapHelper has Next, Back and End" bash -c "grep -q '\"Next\"' '$ROOT/src/snaphelper.c' && grep -q '\"Back\"' '$ROOT/src/snaphelper.c' && grep -q '\"End\"' '$ROOT/src/snaphelper.c'"
check "SnapHelper answers the arrow keys" bash -c "grep -q GDK_KEY_Left '$ROOT/src/snaphelper.c' && grep -q GDK_KEY_Right '$ROOT/src/snaphelper.c'"
check "the installer plays the intro animation" grep -q '^    intro_text$\|^    intro_text' "$ROOT/nix/installer/snap-install-nixos.sh"

section "dock, light theme and bookmarks"
check "the dock starts at 45" grep -q 'size=45' "$ROOT/nix/modules/snapos.nix"
check "the dock pins the defender, browser, programs and terminal" bash -c "grep -q 'pinned-launchers=\\[\"snapguard.desktop\", \"firefox.desktop\", \"org.gnome.Software.desktop\", \"org.gnome.Terminal.desktop\"\\]' '$ROOT/nix/modules/snapos.nix'"
check "no media player is pinned" bash -c "! grep -qi 'vlc\\|rhythmbox' '$ROOT/nix/modules/snapos.nix'"
check "the light theme also has a light dark-variant" grep -q 'gtk-dark.css' "$ROOT/nix/modules/snapos.nix"
check "SnapWeb starts with no bookmarks" grep -q 'NoDefaultBookmarks = true' "$ROOT/custom-apps/snapweb/default.nix"
check "SnapGuard has a light palette" grep -q 'light_css' "$ROOT/src/snapguard-gui.c"
check "SnapHelper has a light palette" grep -q 'light_css' "$ROOT/src/snaphelper.c"

section "repository"
check "installer syntax" bash -n "$ROOT/nix/installer/snap-install-nixos.sh"
if command -v shellcheck >/dev/null 2>&1; then
    check "installer shellcheck" shellcheck -x "$ROOT/nix/installer/snap-install-nixos.sh"
fi
python3 -c "import yaml" 2>/dev/null && check "workflow is valid yaml" python3 -c "import yaml,sys; yaml.safe_load(open('$ROOT/.github/workflows/build-nixos-iso.yml')); yaml.safe_load(open('$ROOT/.github/workflows/snapguard-av.yml')); yaml.safe_load(open('$ROOT/.github/workflows/snap-deb.yml')); yaml.safe_load(open('$ROOT/.github/workflows/desktop-test.yml'))"
for f in flake.nix configuration.nix nix/iso.nix nix/modules/snapos.nix nix/pkgs/snapos-tools.nix nix/pkgs/snapguard.nix nix/pkgs/snapos-backgrounds.nix nix/pkgs/snapos-icons.nix custom-apps/snapweb/default.nix nix/tests/desktop.nix nix/modules/integrated-graphics.nix; do
    o=$(grep -o '{' "$ROOT/$f" | wc -l)
    c=$(grep -o '}' "$ROOT/$f" | wc -l)
    check_eq "balanced braces in $f" "$o" "$c"
done

python3 - "$ROOT" <<'PY'
import glob, os, struct, sys
root = sys.argv[1]
bad = []
for p in glob.glob(root + "/branding/**/*", recursive=True):
    if p.endswith(".png"):
        d = open(p, "rb").read()
        i = 8
        while i < len(d):
            n, = struct.unpack(">I", d[i:i+4])
            t = d[i+4:i+8].decode()
            if t not in ("IHDR", "IDAT", "IEND", "PLTE", "tRNS"):
                bad.append((p, t))
            i += 12 + n
    if p.endswith((".jpg", ".jpeg")):
        d = open(p, "rb").read()
        i = 2
        while d[i] == 0xFF and d[i+1] != 0xDA:
            n, = struct.unpack(">H", d[i+2:i+4])
            if 0xE1 <= d[i+1] <= 0xEF or d[i+1] == 0xFE:
                bad.append((p, hex(d[i+1])))
            i += 2 + n
sys.exit(1 if bad else 0)
PY
check "images carry no metadata" test $? -eq 0

if git -C "$ROOT" rev-parse --git-dir >/dev/null 2>&1; then
    check "repository text is English (except the installer's Portuguese table)" bash -c "! git -C '$ROOT' grep -qIE '[ãõçáéíóúâêô]' -- . ':!nix/installer/snap-install-nixos.sh' ':!tests/run.sh'"
    check "fastfetch logo uses the color placeholder" grep -q '^\$1' "$ROOT/branding/fastfetch/snapos-logo.txt"
    check "touchpad typing lock is off by default" grep -q 'disable-while-typing = false' "$ROOT/nix/modules/snapos.nix"
    check "no personal data in tracked files" bash -c "! git -C '$ROOT' grep -qIiE '[A-Za-z0-9._%+-]+@[A-Za-z0-9-]+\.[A-Za-z]{2,}|/home/[a-z]' -- . ':!tests/run.sh'"
fi

printf '\n%d passed, %d failed\n' "$PASS" "$FAIL"
[ "$FAIL" -eq 0 ]
