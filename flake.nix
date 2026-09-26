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

      # SnapOS is built for x86_64 (PCs) and aarch64 (ARM64 computers with UEFI).
      systems = [ "x86_64-linux" "aarch64-linux" ];
      forAll = f: nixpkgs.lib.genAttrs systems f;
      mkPkgs = sys: import nixpkgs { system = sys; overlays = [ snaposOverlay ]; };
      mkSystem = sys: modules: nixpkgs.lib.nixosSystem {
        system = sys;
        specialArgs = { inherit self; };
        modules = [ { nixpkgs.overlays = [ snaposOverlay ]; } ] ++ modules;
      };
      pkgs = mkPkgs system;
    in
    {
      nixosConfigurations = {
        snapos = mkSystem "x86_64-linux" [ ./configuration.nix ];
        # The same system with the light appearance, so CI builds both.
        snapos-light = self.nixosConfigurations.snapos.extendModules {
          modules = [ { snapos.appearance = "light"; } ];
        };
        snapos-installer = mkSystem "x86_64-linux" [ ./nix/iso.nix ];

        # ARM64: the installer and `snapos rebuild` pick these on an aarch64 computer.
        snapos-aarch64 = mkSystem "aarch64-linux" [ ./configuration.nix ];
        snapos-aarch64-light = self.nixosConfigurations.snapos-aarch64.extendModules {
          modules = [ { snapos.appearance = "light"; } ];
        };
        snapos-installer-aarch64 = mkSystem "aarch64-linux" [ ./nix/iso.nix ];
      };

      # `nix build .#iso` builds the image of the machine it runs on.
      packages = forAll (sys:
        let p = mkPkgs sys; a = if sys == "aarch64-linux" then "-aarch64" else ""; in {
          snapos-tools = p.snapos-tools;
          debootstrap = p.debootstrap;
          iso = self.nixosConfigurations."snapos-installer${a}".config.system.build.isoImage;
          toplevel = self.nixosConfigurations."snapos${a}".config.system.build.toplevel;
          toplevel-light = self.nixosConfigurations."snapos${a}-light".config.system.build.toplevel;
          default = p.snapos-tools;
        });

      checks.${system} = import ./nix/tests/desktop.nix { inherit pkgs; };

      devShells = forAll (sys: { default = (mkPkgs sys).mkShell { packages = with (mkPkgs sys); [ gcc gnumake ]; }; });
    };
}
