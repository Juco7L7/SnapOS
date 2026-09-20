{ stdenv, src }:

stdenv.mkDerivation {
  pname = "snapos-tools";
  version = "0.1";
  inherit src;

  installPhase = ''
    runHook preInstall
    make install PREFIX=$out
    runHook postInstall
  '';

  meta = {
    description = "SnapOS system core (declarative controller, security, pet), in C";
    platforms = [ "x86_64-linux" ];
  };
}
