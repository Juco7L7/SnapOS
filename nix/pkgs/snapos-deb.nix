{ lib, stdenv, dpkg, autoPatchelfHook
, zlib, glib, gtk3, gdk-pixbuf, pango, cairo, atk, at-spi2-atk, at-spi2-core
, nss, nspr, alsa-lib, cups, dbus, expat, libdrm, libxkbcommon, mesa
, libpulseaudio, libsecret, libnotify, openssl, curl, fontconfig, freetype
, libGL, libuuid, systemd, xorg
, deb
}:

# Builds a Debian package that the user added with `snap-deb`. The file is
# unpacked, its programs are pointed at Nix's libraries, and the paths that
# assume /usr and /opt are rewritten. This works for many programs but not for
# all of them: one that needs a Debian service or an unusual library may fail.

let
  file = builtins.baseNameOf deb;
  parts = lib.splitString "_" (lib.removeSuffix ".deb" file);
  name = lib.elemAt parts 0;
  version = if builtins.length parts > 1 then lib.elemAt parts 1 else "0";
in
stdenv.mkDerivation {
  pname = "deb-${name}";
  inherit version;
  src = deb;

  nativeBuildInputs = [ dpkg autoPatchelfHook ];
  buildInputs = [
    stdenv.cc.cc.lib zlib glib gtk3 gdk-pixbuf pango cairo atk at-spi2-atk
    at-spi2-core nss nspr alsa-lib cups dbus expat libdrm libxkbcommon mesa
    libpulseaudio libsecret libnotify openssl curl fontconfig freetype libGL
    libuuid systemd
    xorg.libX11 xorg.libXcomposite xorg.libXdamage xorg.libXext xorg.libXfixes
    xorg.libXrandr xorg.libxcb xorg.libXtst xorg.libXi xorg.libXcursor
    xorg.libXrender xorg.libXScrnSaver
  ];

  # Libraries that are not found are reported by the program itself.
  autoPatchelfIgnoreMissingDeps = true;

  dontConfigure = true;
  dontBuild = true;

  unpackPhase = ''
    runHook preUnpack
    dpkg-deb -x "$src" unpacked
    sourceRoot=unpacked
    runHook postUnpack
  '';

  installPhase = ''
    runHook preInstall
    mkdir -p $out

    if [ -d usr ]; then cp -a usr/. $out/; fi
    if [ -d opt ]; then mkdir -p $out/opt; cp -a opt/. $out/opt/; fi
    for d in bin sbin; do
      if [ -d $d ]; then mkdir -p $out/bin; cp -a $d/. $out/bin/; fi
    done
    chmod -R u+w $out

    # Links that point at /usr or /opt must point inside this package instead.
    find $out -type l | while read -r link; do
      target=$(readlink "$link")
      case "$target" in
        /usr/*) ln -snf "$out''${target#/usr}" "$link" ;;
        /opt/*) ln -snf "$out$target" "$link" ;;
      esac
    done

    # Menu entries: the program and its icon live inside this package.
    if [ -d $out/share/applications ]; then
      for f in $out/share/applications/*.desktop; do
        sed -i -E \
          -e "s#^(Exec|TryExec|Icon)=(\"?)/opt/#\1=\2$out/opt/#" \
          -e "s#^(Exec|TryExec|Icon)=(\"?)/usr/#\1=\2$out/#" "$f"
      done
    fi
    runHook postInstall
  '';

  meta = {
    description = "Debian package ${name} added with snap-deb";
    platforms = [ "x86_64-linux" ];
  };
}
