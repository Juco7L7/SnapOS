{ config, pkgs, lib, modulesPath, self, ... }:

{
  imports = [
    (modulesPath + "/installer/cd-dvd/installation-cd-minimal.nix")
    ./modules/snapos.nix
  ];

  services.xserver.enable = lib.mkForce false;
  services.desktopManager.budgie.enable = lib.mkForce false;
  services.xserver.displayManager.lightdm.enable = lib.mkForce false;

  environment.systemPackages = with pkgs; [
    parted
    whois
    git
    pciutils
  ];

  # NetworkManager brings its own wpa_supplicant (controlled over D-Bus);
  # forcing networking.wireless off here would take it away and leave every
  # Wi-Fi card "unavailable" in the installer.
  networking.networkmanager.enable = true;

  i18n.supportedLocales = [ "all" ];

  environment.etc."snapos-build".text =
    self.shortRev or self.dirtyShortRev or "unknown";

  environment.etc."snapos-installer/snap-install-nixos.sh" = {
    source = ./installer/snap-install-nixos.sh;
    mode = "0755";
  };
  environment.etc."snapos-src" = {
    source = ../.;
  };
  environment.interactiveShellInit = ''
    if [ -z "''${SNAPOS_NO_AUTOINSTALL:-}" ] && [ -t 0 ]; then
      export SNAPOS_NO_AUTOINSTALL=1
      sudo /etc/snapos-installer/snap-install-nixos.sh || true
    fi
  '';

  isoImage.splashImage = ../branding/splash/boot-800x600.png;
  isoImage.efiSplashImage = ../branding/splash/efi-1920x1080.png;
  isoImage.grubTheme = null;
  isoImage.syslinuxTheme = ''
    MENU TITLE SnapOS
    MENU RESOLUTION 800 600
    MENU CLEAR
    MENU ROWS 6
    MENU CMDLINEROW -4
    MENU TIMEOUTROW -3
    MENU TABMSGROW  -2
    MENU HELPMSGROW -1
    MENU HELPMSGENDROW -1
    MENU MARGIN 0

    MENU COLOR BORDER       30;44      #00000000    #00000000   none
    MENU COLOR SCREEN       37;40      #FFE8E4DF    #00000000   none
    MENU COLOR TABMSG       31;40      #80E8E4DF    #00000000   none
    MENU COLOR TIMEOUT      1;37;40    #FFE8E4DF    #00000000   none
    MENU COLOR TIMEOUT_MSG  37;40      #FFA09A94    #00000000   none
    MENU COLOR CMDMARK      1;36;40    #FFE22A1C    #00000000   none
    MENU COLOR CMDLINE      37;40      #FFE8E4DF    #00000000   none
    MENU COLOR TITLE        1;36;44    #FFE22A1C    #00000000   none
    MENU COLOR UNSEL        37;44      #FFE8E4DF    #00000000   none
    MENU COLOR SEL          7;37;40    #FFFFFFFF    #FFE12A1C   std
  '';

  # The red Papirus icons and the red Colloid theme are SnapOS variants, so no
  # public cache has them and the installer would build them on the laptop,
  # which is the slowest part of the install. They ship inside the image.
  isoImage.storeContents = [
    (pkgs.papirus-icon-theme.override { color = "red"; })
    (pkgs.colloid-gtk-theme.override { themeVariants = [ "red" ]; colorVariants = [ "light" "dark" ]; })
  ];

  # ARM64 computers and VMs often have only a serial console: the boot and
  # the installer show up there too (tty0 stays the main console).
  boot.kernelParams = lib.optionals pkgs.stdenv.hostPlatform.isAarch64 [ "console=ttyAMA0,115200" "console=tty0" ];

  isoImage.isoName = lib.mkForce (if pkgs.stdenv.hostPlatform.isAarch64 then "snapos-installer-aarch64.iso" else "snapos-installer.iso");
  isoImage.volumeID = lib.mkForce "SNAPOS_INSTALL";
  isoImage.appendToMenuLabel = " SnapOS Installer";

  system.stateVersion = "26.05";
}
