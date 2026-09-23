{ stdenv, pkg-config, wrapGAppsHook3, gtk3, src }:

# SnapHelper, the tour with three animations, and snapupdate, the login check
# for a newer release. Both open by themselves (autostart entries in
# modules/snapos.nix) and are in the menu.
stdenv.mkDerivation {
  pname = "snaphelper";
  version = "0.1";
  inherit src;

  nativeBuildInputs = [ pkg-config wrapGAppsHook3 ];
  buildInputs = [ gtk3 ];

  buildPhase = ''
    runHook preBuild
    make bin/snaphelper bin/snapupdate
    runHook postBuild
  '';

  installPhase = ''
    runHook preInstall
    install -Dm755 bin/snaphelper $out/bin/snaphelper
    install -Dm755 bin/snapupdate $out/bin/snapupdate
    install -Dm644 branding/snapupdate.desktop $out/share/applications/snapupdate.desktop
    for g in snappy-declares snappy-deb snappy-defends; do
      install -Dm644 branding/$g.gif $out/share/snapos/helper/$g.gif
    done
    install -Dm644 branding/snaphelper.desktop $out/share/applications/snaphelper.desktop
    for s in 16 24 32 48 64 128 256 512; do
      install -Dm644 branding/icons/snapos-$s.png $out/share/icons/hicolor/''${s}x''${s}/apps/snapos.png
    done
    runHook postInstall
  '';

  meta = {
    description = "SnapHelper, the first-boot tour of SnapOS";
    mainProgram = "snaphelper";
    platforms = [ "x86_64-linux" ];
  };
}
