{ stdenv, pkg-config, wrapGAppsHook3, gtk3, src }:

stdenv.mkDerivation {
  pname = "snapguard";
  version = "0.1";
  inherit src;

  nativeBuildInputs = [ pkg-config wrapGAppsHook3 ];
  buildInputs = [ gtk3 ];

  buildPhase = ''
    runHook preBuild
    make gui
    runHook postBuild
  '';

  installPhase = ''
    runHook preInstall
    install -Dm755 bin/snapguard-gui $out/bin/snapguard-gui
    install -Dm644 branding/snapguard.desktop $out/share/applications/snapguard.desktop
    for s in 16 24 32 48 64 128 256 512; do
      install -Dm644 branding/icons/snapguard-dark-$s.png $out/share/icons/hicolor/''${s}x''${s}/apps/snapguard.png
      install -Dm644 branding/icons/snapguard-light-$s.png $out/share/icons/hicolor/''${s}x''${s}/apps/snapguard-light.png
    done
    runHook postInstall
  '';

  meta = {
    description = "SnapGuard, the SnapOS antivirus window";
    mainProgram = "snapguard-gui";
    platforms = [ "x86_64-linux" "aarch64-linux" ];
  };
}
