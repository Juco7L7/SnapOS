{ pkgs }:

# Builds a small .deb, packages it the way `snap-deb` does, and checks that the
# program runs and that its paths were rewritten.
let
  fixture = pkgs.runCommand "hello-snap-deb" { nativeBuildInputs = [ pkgs.dpkg pkgs.gcc ]; } ''
    mkdir -p pkg/DEBIAN pkg/opt/hello-snap pkg/usr/bin pkg/usr/share/applications $out
    printf '#include <stdio.h>\nint main(void) { puts("hello from a deb"); return 0; }\n' > hello.c
    gcc -o pkg/opt/hello-snap/hello hello.c
    ln -s /opt/hello-snap/hello pkg/usr/bin/hello-snap
    printf '[Desktop Entry]\nType=Application\nName=Hello\nExec=/opt/hello-snap/hello\nIcon=/opt/hello-snap/icon.png\n' > pkg/usr/share/applications/hello-snap.desktop
    printf 'Package: hello-snap\nVersion: 1.0\nArchitecture: amd64\nMaintainer: test\nDescription: a test package\n' > pkg/DEBIAN/control
    dpkg-deb --build pkg $out/hello-snap_1.0_amd64.deb
  '';

  package = pkgs.callPackage ../pkgs/snapos-deb.nix {
    deb = "${fixture}/hello-snap_1.0_amd64.deb";
  };
in
{
  deb = pkgs.runCommand "snapos-deb-test" { } ''
    test "$(${package}/bin/hello-snap)" = "hello from a deb"
    ${pkgs.patchelf}/bin/patchelf --print-interpreter ${package}/opt/hello-snap/hello | grep -q /nix/store
    grep -q "Exec=${package}/opt/hello-snap/hello" ${package}/share/applications/hello-snap.desktop
    grep -q "Icon=${package}/opt/hello-snap/icon.png" ${package}/share/applications/hello-snap.desktop
    touch $out
  '';
}
