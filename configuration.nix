# SnapOS system configuration.
#
# This file describes your whole system. Open it with `snapos config` and
# apply your changes with `snapos rebuild`. Every rebuild keeps the previous
# version as a boot entry, so a change can always be rolled back.
#
# Lines that start with "#" are comments. To turn one of the examples below
# on, remove the leading "#" and rebuild.

{ config, pkgs, lib, ... }:

{
  # The installer writes three extra files next to this one: your hardware
  # (hardware-configuration.nix), your choices (local.nix) and the graphics
  # mode (graphics.nix). They are picked up automatically when they exist.
  imports = [ ./nix/modules/snapos.nix ./nix/modules/integrated-graphics.nix ]
    ++ lib.optional (builtins.pathExists ./hardware-configuration.nix) ./hardware-configuration.nix
    ++ lib.optional (builtins.pathExists ./local.nix) ./local.nix
    ++ lib.optional (builtins.pathExists ./graphics.nix) ./graphics.nix;

  snapos.desktop             = "budgie";
  snapos.theme               = "snappy-red";
  snapos.security.antivirus  = "snapguard";
  snapos.security.onInfected = "contain";

  # Dark or light desktop. The installer saved your choice in local.nix
  # (snapos.appearance = "dark" or "light"). To switch, edit that line there
  # and run `snapos rebuild`.

  # Bootloader. GRUB handles both BIOS and UEFI machines.
  boot.loader.grub.enable = lib.mkDefault true;
  boot.loader.grub.device = lib.mkDefault "nodev";
  boot.loader.grub.efiSupport = lib.mkDefault true;
  boot.loader.grub.efiInstallAsRemovable = lib.mkDefault true;
  boot.loader.efi.canTouchEfiVariables = lib.mkDefault false;

  fileSystems."/" = lib.mkDefault { device = "/dev/disk/by-label/nixos"; fsType = "ext4"; };

  # Networking. Wi-Fi and Ethernet are handled by NetworkManager, which is
  # already enabled. The computer name is set by the installer.
  # networking.hostName = "snapos";

  # Configure a network proxy if necessary.
  # networking.proxy.default = "http://user:password@proxy:port/";
  # networking.proxy.noProxy = "127.0.0.1,localhost,internal.domain";

  # Time zone and language. The installer already set these for you.
  # time.timeZone = "America/New_York";
  # i18n.defaultLocale = "en_US.UTF-8";
  # console.keyMap = "us";

  # Printing is enabled by the desktop. Uncomment to turn it off.
  # services.printing.enable = false;

  # Audio comes from PipeWire and is already enabled.

  # Users. Your account was created by the installer. To add another one:
  # users.users.alice = {
  #   isNormalUser = true;
  #   extraGroups = [ "wheel" "networkmanager" ];
  # };

  # Programs and services you may want to enable.
  # programs.steam.enable = true;
  # services.openssh.enable = true;

  # Firewall. Open ports here, or turn it off.
  # networking.firewall.allowedTCPPorts = [ 22 80 ];
  # networking.firewall.enable = false;

  # This value describes the release this system was first installed with.
  # Do not change it when upgrading. See `man configuration.nix` for details.
  system.stateVersion = "26.05";

  # Installed programs. Add a name to the list and run `snapos rebuild`, or
  # let `snapctl save` manage this list for you.

  # snapos:packages:begin
  environment.systemPackages = with pkgs; [
    curl
    dpkg
    fastfetch
    git
    gnome-software
    pciutils
    snapguard
    snapos-tools
    snapweb
  ];
  # snapos:packages:end
}
