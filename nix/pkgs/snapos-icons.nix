{ runCommand }:

runCommand "nixos-icons-snapos" { } ''
  for s in 16 24 32 48 64 72 96 128 256 512; do
    d=$out/share/icons/hicolor/''${s}x''${s}/apps
    mkdir -p "$d"
    cp ${../../branding/icons}/snapos-$s.png "$d/nix-snowflake.png"
    cp ${../../branding/icons}/snapos-$s.png "$d/nix-snowflake-white.png"
  done
  d=$out/share/icons/hicolor/scalable/apps
  mkdir -p "$d"
  cp ${../../branding/icons}/snapos.svg "$d/nix-snowflake.svg"
  cp ${../../branding/icons}/snapos.svg "$d/nix-snowflake-white.svg"
''
