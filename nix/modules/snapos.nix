{ config, lib, pkgs, ... }:

with lib;
let
  cfg = config.snapos;

  light = cfg.appearance == "light";
  mode = if light then "light" else "dark";

  redTheme = pkgs.colloid-gtk-theme.override {
    themeVariants = [ "red" ];
    colorVariants = [ "light" "dark" ];
  };
  redIcons = pkgs.papirus-icon-theme.override { color = "red"; };

  # Colloid's light theme ships a dark gtk-dark.css. Any program that asks for
  # the dark variant would draw white text on the light desktop, so in this copy
  # the dark variant is the light one too.
  lightTheme = pkgs.runCommand "snapos-light-gtk-theme" { } ''
    d=$out/share/themes/SnapOS-Light
    mkdir -p $out/share/themes
    cp -rL ${redTheme}/share/themes/Colloid-Red-Light $d
    chmod -R u+w $d
    cp $d/gtk-3.0/gtk.css $d/gtk-3.0/gtk-dark.css
    if [ -e $d/gtk-4.0/gtk.css ]; then cp $d/gtk-4.0/gtk.css $d/gtk-4.0/gtk-dark.css; fi
    sed -i 's/^Name=.*/Name=SnapOS-Light/' $d/index.theme
  '';
  gtkTheme  = if light then "SnapOS-Light" else "Colloid-Red-Dark";
  gtkPackage = if light then lightTheme else redTheme;
  iconName  = if light then "Papirus-Light" else "Papirus-Dark";
  scheme    = if light then "prefer-light" else "prefer-dark";

  wallpaper = "file://${pkgs.snapos-backgrounds}/share/backgrounds/snapos/snapos-${mode}.jpeg";
  loginBackground =
    if light then ../../branding/wallpapers/snapos-light.jpeg
    else ../../branding/wallpapers/snapos-default.jpeg;
  primaryColor   = if light then "#ECE9E5" else "#141110";
  secondaryColor = if light then "#FFFFFF" else "#000000";

  # The shield is white on a dark desktop and black on a light one.
  shieldIcons = pkgs.runCommand "snapguard-${mode}-icons" { } ''
    for s in 16 24 32 48 64 128 256 512; do
      install -Dm644 ${../../branding/icons}/snapguard-${mode}-$s.png \
        $out/share/icons/hicolor/''${s}x''${s}/apps/snapguard.png
    done
  '';

  # Every .deb that `snap-deb` declared sits in /etc/snapos/debs. `snap-deb sync`
  # installs them in a Debian layer under /var/lib/snapdeb and exports their
  # menu entries and commands from there.
  debLayer = "/var/lib/snapdeb";

  # The SnapOS release this tree is (the VERSION file at the repository root).
  snaposVersion = lib.strings.trim (builtins.readFile ../../VERSION);
in {
  options.snapos = {
    desktop = mkOption {
      type = types.enum [ "budgie" ];
      default = "budgie";
    };
    theme = mkOption { type = types.str; default = "snappy-red"; };
    appearance = mkOption {
      type = types.enum [ "dark" "light" ];
      default = "dark";
      description = "Dark or light desktop. The installer asks for it; change it any time and run snapos rebuild.";
    };
    security = {
      antivirus  = mkOption { type = types.enum [ "snapguard" "none" ]; default = "snapguard"; };
      onInfected = mkOption { type = types.enum [ "contain" "warn" ]; default = "contain"; };
    };
  };

  config = {
    system.nixos.distroName = "SnapOS";
    networking.hostName = mkDefault "snapos";

    # os-release names the SnapOS release, not the NixOS one: fastfetch and
    # `snapos version` show "SnapOS 2.0", and updates compare this number.
    environment.etc."os-release".text = mkForce ''
      NAME="SnapOS"
      ID=snapos
      ID_LIKE=nixos
      VERSION="${snaposVersion}"
      VERSION_ID="${snaposVersion}"
      PRETTY_NAME="SnapOS ${snaposVersion}"
      BUILD_ID="${config.system.nixos.version}"
      HOME_URL="https://github.com/Juco7L7/SnapOS"
      SUPPORT_URL="https://github.com/Juco7L7/SnapOS/issues"
      LOGO=snapos
    '';
    environment.etc."snapos/version".text = snaposVersion;

    services.xserver.enable = true;
    services.desktopManager.budgie.enable = true;
    services.xserver.displayManager.lightdm.enable = true;
    services.desktopManager.gnome.enable = mkForce false;
    services.desktopManager.plasma6.enable = mkForce false;
    # Budgie turns on Rygel, a DLNA media server that shares the user's music,
    # videos and photos with other devices on the network. Not on SnapOS.
    services.gnome.rygel.enable = false;

    environment.etc."snapos/theme".text = cfg.theme;
    environment.etc."snapos/appearance".text = cfg.appearance;

    # SnapGuard in real time: at login, snapguard-watch starts scanning every
    # new file in Downloads and every USB drive that is plugged in.
    environment.etc."xdg/autostart/snapguard-watch.desktop".text = mkIf (cfg.security.antivirus == "snapguard") ''
      [Desktop Entry]
      Type=Application
      Name=SnapGuard real-time protection
      Exec=snapguard-watch
      Terminal=false
      X-GNOME-Autostart-enabled=true
    '';

    # An update is only made the default for the next boot. At boot the guard
    # lets a new system start once; it is approved when a normal user logs in,
    # and undone (back to the previous generation) if that never happens.
    systemd.services.snapos-update-guard = {
      description = "SnapOS update guard: undo an update that did not come up";
      wantedBy = [ "multi-user.target" ];
      before = [ "display-manager.service" ];
      after = [ "local-fs.target" ];
      path = [ config.nix.package pkgs.systemd pkgs.coreutils ];
      serviceConfig = { Type = "oneshot"; ExecStart = "${pkgs.snapos-tools}/bin/snapos guard boot"; };
    };
    systemd.services.snapos-update-approve = {
      description = "SnapOS update guard: approve the new system once someone logs in";
      wantedBy = [ "graphical.target" ];
      after = [ "display-manager.service" ];
      path = [ config.nix.package pkgs.systemd pkgs.coreutils ];
      serviceConfig = { Type = "simple"; ExecStart = "${pkgs.snapos-tools}/bin/snapos guard approve"; };
    };

    # At every login, snapupdate asks GitHub for a newer release and offers it.
    environment.etc."xdg/autostart/snapupdate.desktop".text = ''
      [Desktop Entry]
      Type=Application
      Name=SnapOS Update
      Exec=snapupdate --autostart
      Terminal=false
      X-GNOME-Autostart-enabled=true
    '';

    # SnapHelper opens once, the first time each user logs in.
    environment.etc."xdg/autostart/snaphelper.desktop".text = ''
      [Desktop Entry]
      Type=Application
      Name=SnapHelper
      Exec=snaphelper --first-run
      Terminal=false
      X-GNOME-Autostart-enabled=true
    '';

    services.flatpak.enable = config.services.xserver.enable;
    xdg.portal.enable = mkIf config.services.flatpak.enable true;
    xdg.portal.extraPortals = mkIf config.services.flatpak.enable [ pkgs.xdg-desktop-portal-gtk ];
    xdg.portal.config.common.default = mkIf config.services.flatpak.enable (mkDefault "*");

    systemd.services.flatpak-flathub = mkIf config.services.flatpak.enable {
      wantedBy = [ "multi-user.target" ];
      wants = [ "network-online.target" ];
      after = [ "network-online.target" ];
      path = [ pkgs.flatpak ];
      script = ''
        until flatpak remote-add --system --if-not-exists flathub https://dl.flathub.org/repo/flathub.flatpakrepo; do
          sleep 30
        done
        until flatpak update --system --appstream; do
          sleep 30
        done
      '';
    };

    environment.systemPackages =
      optionals config.services.xserver.enable ([ redTheme redIcons (hiPrio shieldIcons) ] ++ optional light lightTheme)
      ++ optional config.services.xserver.enable pkgs.snaphelper
      ++ [ pkgs.bubblewrap pkgs.debootstrap pkgs.dpkg pkgs.libnotify ];

    # Programs from the Debian layer show up in the menu and on the PATH.
    environment.sessionVariables.XDG_DATA_DIRS = [ "${debLayer}/exports/share" ];
    environment.extraInit = ''
      export PATH="$PATH:${debLayer}/exports/bin"
    '';

    # Lets programs built for other distributions find their libraries.
    programs.nix-ld.enable = true;

    services.clamav.daemon.enable  = mkIf (cfg.security.antivirus == "snapguard") true;
    services.clamav.updater.enable = mkIf (cfg.security.antivirus == "snapguard") true;
    # SnapGuard runs as the user, so every user must be able to reach clamd.
    services.clamav.daemon.settings.LocalSocketMode = mkIf (cfg.security.antivirus == "snapguard") "666";
    # Tell clamd to reload when freshclam has new signatures.
    services.clamav.updater.settings.NotifyClamd = mkIf (cfg.security.antivirus == "snapguard") "/etc/clamav/clamd.conf";
    # The first signature download needs the network. If it is not there yet,
    # try again instead of leaving the defender off until the next reboot.
    systemd.services.clamav-daemon = mkIf (cfg.security.antivirus == "snapguard") {
      # If the daemon fails (no database yet), systemd would remove /run/clamav
      # and with it the socket file that clamd is reached through.
      serviceConfig = { Restart = "on-failure"; RestartSec = "60s"; RuntimeDirectoryPreserve = "yes"; };
      unitConfig.StartLimitIntervalSec = 0;
      # NixOS makes the daemon wait for freshclam, so the boot would wait for the
      # signature download. The daemon starts on its own and retries until the
      # timer below has fetched the database.
      after = mkForce [ "clamav-daemon.socket" ];
      wants = mkForce [ ];
    };
    systemd.timers.clamav-freshclam.timerConfig = mkIf (cfg.security.antivirus == "snapguard") {
      OnBootSec = "2min";
    };
    environment.etc."snapos/onInfected".text = cfg.security.onInfected;
    environment.etc."snapos/signatures.txt".source = ../../security/signatures.txt;
    # Debian's archive keys: the Debian layer is only created from a verified archive.
    environment.etc."snapos/debian-archive-keyring.gpg".source = ../../security/debian-archive-keyring.gpg;
    systemd.tmpfiles.rules = [
      "f /etc/snapos/allow.txt 0644 root root -"
      "d /var/lib/snapos 0750 root root -"
      "d ${debLayer} 0755 root root -"
      "d /var/lib/snapos/update 0755 root root -"
      # The system lives in /etc/snapos; the NixOS path keeps working through a link.
      "L /etc/nixos - - - - /etc/snapos"
    ];

    environment.etc."snapos/fastfetch.jsonc".source = ../../branding/fastfetch/config.jsonc;
    environment.etc."snapos/logo.txt".source        = ../../branding/fastfetch/snapos-logo.txt;
    environment.etc."snapos/mascot.txt".source      = ../../branding/snappy-mascot.txt;
    # The same Snappy drawn with the characters the Linux console font has
    # (the installer runs on the console, which cannot draw the terminal one).
    environment.etc."snapos/mascot-console.txt".source = ../../branding/snappy-console.txt;

    boot.plymouth.enable = false;

    boot.loader.grub.splashImage = ../../branding/splash/boot-800x600.png;
    boot.loader.grub.backgroundColor = "#141110";
    boot.loader.grub.configurationLimit = 10;

    services.xserver.displayManager.lightdm.background = loginBackground;
    services.xserver.displayManager.lightdm.greeters.slick = {
      theme = { name = gtkTheme; package = gtkPackage; };
      iconTheme = { name = iconName; package = redIcons; };
    };

    xdg.mime.defaultApplications = {
      "application/vnd.debian.binary-package" = "snap-deb.desktop";
      "application/x-deb" = "snap-deb.desktop";
      "application/x-debian-package" = "snap-deb.desktop";
      "text/html" = "firefox.desktop";
      "x-scheme-handler/http" = "firefox.desktop";
      "x-scheme-handler/https" = "firefox.desktop";
      "x-scheme-handler/about" = "firefox.desktop";
      "x-scheme-handler/unknown" = "firefox.desktop";
    };

    # The dock: 45 pixels high, with the defender, the browser, the programs
    # store and the terminal pinned. (NixOS pins a media player by default.)
    services.desktopManager.budgie.extraGSettingsOverrides = ''
      [com.solus-project.icon-tasklist:Budgie]
      pinned-launchers=["snapguard.desktop", "firefox.desktop", "org.gnome.Software.desktop", "org.gnome.Terminal.desktop"]

      [com.solus-project.budgie-panel.panel:Budgie]
      size=45
    '';

    programs.dconf.enable = true;
    environment.budgie.excludePackages = [
      (pkgs.runCommand "nixos-background-info" { } "mkdir -p $out")
    ];
    programs.dconf.profiles.user.databases = [
      {
        settings = {
          "org/gnome/desktop/background" = {
            picture-uri = wallpaper;
            picture-uri-dark = wallpaper;
            picture-options = "zoom";
            primary-color = primaryColor;
            secondary-color = secondaryColor;
          };
          "org/gnome/desktop/screensaver" = {
            picture-uri = wallpaper;
            picture-options = "zoom";
            primary-color = primaryColor;
          };
          "org/gnome/desktop/interface" = {
            gtk-theme = gtkTheme;
            icon-theme = iconName;
            color-scheme = scheme;
          };
          "org/gnome/desktop/peripherals/touchpad" = {
            disable-while-typing = false;
          };
          "com/solus-project/budgie-panel" = {
            dark-theme = !light;
          };
        };
      }
    ];

    documentation.nixos.enable = false;
    documentation.doc.enable = false;
    documentation.info.enable = false;

    nix.settings.experimental-features = [ "nix-command" "flakes" ];
    nix.settings.auto-optimise-store = true;
    nix.gc = {
      automatic = true;
      dates = "weekly";
      options = "--delete-older-than 14d";
    };
    zramSwap.enable = true;

    hardware.enableRedistributableFirmware = true;
    hardware.graphics.enable = mkIf config.services.xserver.enable true;
    services.pulseaudio.enable = false;
    security.rtkit.enable = mkIf config.services.xserver.enable true;
    services.pipewire = mkIf config.services.xserver.enable {
      enable = true;
      alsa.enable = true;
      pulse.enable = true;
      wireplumber.enable = true;
    };
    services.fwupd.enable = config.services.xserver.enable;
    services.udisks2.enable = config.services.xserver.enable;
    services.gvfs.enable = mkIf config.services.xserver.enable true;
    services.libinput.touchpad.disableWhileTyping = mkIf config.services.xserver.enable false;
    services.journald.extraConfig = "SystemMaxUse=200M";

    time.timeZone       = mkDefault "UTC";
    i18n.defaultLocale  = mkDefault "en_US.UTF-8";
    console.keyMap      = mkDefault "us";
  };
}
