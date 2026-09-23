{
  description = "SnapOS — a NixOS-based, declarative, security-first Linux distro";

  inputs.nixpkgs.url = "github:NixOS/nixpkgs/nixos-26.05";

  outputs = { self, nixpkgs }:
    let
      system = "x86_64-linux";

      customApps = final: prev:
        let
          dir = ./custom-apps;
          names = builtins.attrNames (prev.lib.filterAttrs (_: t: t == "directory") (builtins.readDir dir));
        in
        prev.lib.genAttrs names (n: final.callPackage (dir + "/${n}") { });

      snaposOverlay = final: prev: customApps final prev // {
        snapos-tools = final.callPackage ./nix/pkgs/snapos-tools.nix { src = self; };
        snapguard = final.callPackage ./nix/pkgs/snapguard.nix { src = self; };
        snaphelper = final.callPackage ./nix/pkgs/snaphelper.nix { src = self; };
        nixos-icons = final.callPackage ./nix/pkgs/snapos-icons.nix { };
        fastfetch = final.symlinkJoin {
          name = "fastfetch";
          paths = [ prev.fastfetch ];
          nativeBuildInputs = [ final.makeWrapper ];
          postBuild = ''
            wrapProgram $out/bin/fastfetch --add-flags "--config /etc/snapos/fastfetch.jsonc"
          '';
        };
        snapos-backgrounds = final.callPackage ./nix/pkgs/snapos-backgrounds.nix { };
        budgie-backgrounds = final.snapos-backgrounds;
        # nixpkgs wraps debootstrap with a fixed PATH that has no `mount`, and
        # debootstrap needs it while it builds the Debian layer.
        debootstrap = prev.debootstrap.overrideAttrs (old: {
          postInstall = (old.postInstall or "") + ''
            sed -i "s|^export PATH='|export PATH='${final.util-linux}/bin:|" $out/bin/debootstrap
            grep -q "${final.util-linux}/bin" $out/bin/debootstrap
          '';
        });
      };

      pkgs = import nixpkgs { inherit system; overlays = [ snaposOverlay ]; };
    in
    {
      nixosConfigurations = {
        snapos = nixpkgs.lib.nixosSystem {
          inherit system;
          modules = [
            { nixpkgs.overlays = [ snaposOverlay ]; }
            ./configuration.nix
          ];
        };

        # The same system with the light appearance, so CI builds both.
        snapos-light = self.nixosConfigurations.snapos.extendModules {
          modules = [ { snapos.appearance = "light"; } ];
        };

        snapos-installer = nixpkgs.lib.nixosSystem {
          inherit system;
          specialArgs = { inherit self; };
          modules = [
            { nixpkgs.overlays = [ snaposOverlay ]; }
            ./nix/iso.nix
          ];
        };
      };

      packages.${system} = {
        snapos-tools = pkgs.snapos-tools;
        debootstrap = pkgs.debootstrap;
        iso = self.nixosConfigurations.snapos-installer.config.system.build.isoImage;
        toplevel = self.nixosConfigurations.snapos.config.system.build.toplevel;
        toplevel-light = self.nixosConfigurations.snapos-light.config.system.build.toplevel;
        default = pkgs.snapos-tools;
      };

      checks.${system} = import ./nix/tests/desktop.nix { inherit pkgs; };

      devShells.${system}.default = pkgs.mkShell { packages = [ pkgs.gcc pkgs.gnumake ]; };
    };
}
