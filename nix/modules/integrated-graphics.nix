{ ... }:

{
  specialisation.integrated-graphics.configuration = {
    system.nixos.tags = [ "integrated-graphics" ];
    boot.blacklistedKernelModules = [ "nouveau" "nvidia" "radeon" "amdgpu" ];
  };
}
