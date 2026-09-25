{ pkgs }:

# One VM per desktop and appearance: the system boots to the login screen,
# the desktop's own processes are up, the SnapOS look and programs are in
# place, and the update guard approves a system once someone has logged in.
let
  # The login screen is photographed and the password typed, as a person
  # would do; then the desktop is photographed.
  loginScreen = ''
        machine.wait_until_succeeds("pgrep -f '[s]lick-greeter'", timeout=300)
        machine.sleep(20)
        machine.screenshot("01-login")
        machine.send_chars("tester\n")
  '';
  mk = { name, desktop, appearance ? "dark", processes, extra ? "", vm ? { }, autoLogin ? false, login ? loginScreen }:
    pkgs.testers.runNixOSTest {
      name = "snapos-${name}";

      nodes.machine = { lib, ... }: {
        imports = [ ../../configuration.nix ];

        networking.hostName = lib.mkForce "machine";
        # no antivirus in the desktop VMs: loading the virus database takes
        # the VM's memory and time, and SnapGuard has its own test
        services.clamav.daemon.enable = lib.mkForce false;
        services.clamav.updater.enable = lib.mkForce false;
        snapos.desktop = desktop;
        snapos.appearance = appearance;

        virtualisation.memorySize = 3072;
        virtualisation.cores = 2;
        virtualisation.resolution = { x = 1280; y = 800; };

        users.users.tester = {
          isNormalUser = true;
          password = "tester";
          extraGroups = [ "wheel" ];
        };
        services.displayManager.autoLogin = {
          enable = autoLogin;
          user = "tester";
        };
      } // vm;

      testScript = ''
        machine.start()
        machine.wait_for_unit("multi-user.target")
        machine.wait_for_unit("NetworkManager.service")
        machine.sleep(30)
        machine.execute("cat /proc/cmdline > /tmp/cmdline.txt; uname -r > /tmp/kernel.txt; ps -eo user,comm,args > /tmp/ps.txt")
        machine.execute("systemctl status display-manager --no-pager > /tmp/dm-status.txt 2>&1")
        machine.execute("journalctl -b --no-pager > /tmp/journal.txt")
        machine.execute("tar czf /tmp/xlogs.tgz /var/log/lightdm /var/log/X.0.log /home/*/.xsession-errors /home/*/.config/snapos/*.log 2>/dev/null; true")
        for f in ["cmdline.txt", "kernel.txt", "ps.txt", "dm-status.txt", "journal.txt", "xlogs.tgz"]:
            machine.copy_from_vm("/tmp/" + f)
        machine.screenshot("01-state")
        machine.wait_for_unit("display-manager.service")
        ${login}
        try:
            for p in ${builtins.toJSON processes}:
                machine.wait_until_succeeds("pgrep -u tester -f " + p, timeout=300)
            machine.sleep(30)
            machine.screenshot("02-desktop")
            machine.execute("(echo 'variant: ${name}'; free -m; echo; echo running services: $(systemctl list-units --type=service --state=running --no-legend | wc -l); echo processes: $(ps -e | wc -l)) > /tmp/metrics.txt")
            machine.copy_from_vm("/tmp/metrics.txt")
            machine.succeed("grep -q ${appearance} /etc/snapos/appearance")
            machine.succeed("test -f /etc/xdg/autostart/snaphelper.desktop")
            machine.succeed("test -s /run/current-system/sw/share/snapos/helper/snappy-declares.gif")
            machine.succeed("snapos help")
            machine.succeed("snapguard status")
            machine.succeed("grep -q 'PRETTY_NAME=\"SnapOS ' /etc/os-release")
            ${let ls = pkgs.lib.splitString "\n" extra; in pkgs.lib.concatStringsSep "\n" ([ (builtins.head ls) ] ++ map (l: "    " + l) (builtins.tail ls))}
            machine.screenshot("09-final")
        finally:
            machine.screenshot("08-last")
            machine.execute("journalctl -b --no-pager > /tmp/journal.txt")
            machine.execute("tar czf /tmp/xlogs.tgz /var/log/lightdm /var/log/X.0.log /home/*/.xsession-errors /home/*/.config/snapos/*.log /tmp/*.log /tmp/hypr-*.txt 2>/dev/null; true")
            machine.copy_from_vm("/tmp/journal.txt")
            machine.copy_from_vm("/tmp/xlogs.tgz")

        # The update guard: a new system is approved once a user has logged in
        # (tester logged in above), and a system that never got
        # approved is undone at the next boot.
        machine.succeed("systemctl is-enabled snapos-update-guard snapos-update-approve")
        machine.succeed("printf 'previous=1\\nattempts=0\\nname=test-release\\n' > /var/lib/snapos/update/update-pending")
        machine.succeed("systemctl restart snapos-update-guard")
        machine.succeed("grep -q '^attempts=1' /var/lib/snapos/update/update-pending")
        machine.succeed("systemctl restart snapos-update-approve")
        machine.wait_until_succeeds("test ! -e /var/lib/snapos/update/update-pending", timeout=120)
        machine.succeed("printf 'previous=1\\nattempts=1\\nname=test-release\\n' > /var/lib/snapos/update/update-pending")
        machine.succeed("SNAPOS_GUARD_NO_REBOOT=1 snapos guard boot")
        machine.succeed("test ! -e /var/lib/snapos/update/update-pending")
        machine.succeed("grep -q test-release /var/lib/snapos/update/update-rolled-back")
      '';
    };

  gsettings = "sudo -u tester env DBUS_SESSION_BUS_ADDRESS=unix:path=/run/user/1000/bus gsettings get";

  # The VM has no internet, so ClamAV gets a tiny signature database written by
  # hand. The point is to prove the wiring: clamd, its socket, and SnapGuard
  # reading a private file of a normal user.
  antivirus = pkgs.testers.runNixOSTest {
    name = "snapos-antivirus";

    nodes.machine = { lib, ... }: {
      imports = [ ../../configuration.nix ];

      networking.hostName = lib.mkForce "machine";

      virtualisation.memorySize = 3072;
      virtualisation.cores = 2;

      users.users.tester = {
        isNormalUser = true;
        password = "tester";
      };
    };

    testScript = ''
      machine.start()
      machine.wait_for_unit("multi-user.target")

      machine.succeed("echo harmless > /tmp/fake.bin && echo clean > /tmp/clean.txt && chmod 644 /tmp/fake.bin /tmp/clean.txt")
      machine.succeed("mkdir -p /var/lib/clamav")
      machine.succeed("echo \"$(md5sum /tmp/fake.bin | cut -d' ' -f1):$(stat -c %s /tmp/fake.bin):SnapOS.Test.Fake-1\" > /var/lib/clamav/test.hdb")
      machine.succeed("chown -R clamav:clamav /var/lib/clamav")
      home = machine.succeed("su tester -c 'echo $HOME'").strip()
      private = home + "/private"
      machine.succeed("install -d -m 700 -o tester " + private + " && cp /tmp/fake.bin " + private + "/ && chown tester " + private + "/fake.bin")

      machine.succeed("systemctl restart clamav-daemon")
      machine.wait_until_succeeds("test -S /run/clamav/clamd.ctl", timeout=180)
      machine.execute("systemctl status clamav-daemon clamav-freshclam --no-pager > /tmp/clam-units.txt 2>&1; journalctl -u clamav-daemon --no-pager > /tmp/clam-journal.txt 2>&1")

      # 1. through the daemon, as a normal user, on a file only that user can read
      status, out = machine.execute("su tester -c 'snapguard status'")
      print(out)
      assert "ACTIVE" in out, out
      assert "running" in out, out
      status, out = machine.execute("su tester -c 'snapguard scan " + private + "/fake.bin'")
      print(out)
      assert status == 1, (status, out)
      assert "SnapOS.Test.Fake-1" in out, out
      status, out = machine.execute("su tester -c 'snapguard scan /tmp/clean.txt'")
      print(out)
      assert status == 0, (status, out)

      # 2. daemon down: SnapGuard must fall back to the stand-alone scanner
      machine.succeed("systemctl stop clamav-daemon")
      status, out = machine.execute("su tester -c 'snapguard scan " + private + "/fake.bin'")
      print(out)
      assert status == 1, (status, out)
      assert "SnapOS.Test.Fake-1" in out, out
    '';
  };
in
let
  # every desktop in both appearances
  budgie = appearance: mk {
    name = "desktop" + (if appearance == "light" then "-light" else ""); desktop = "budgie"; inherit appearance;
    processes = [ "'labwc|budgie-wm'" "'budgie-panel|budgie-desktop'" ];
    extra = ''
      machine.succeed("${gsettings} org.gnome.desktop.peripherals.touchpad disable-while-typing | grep -q false")
      machine.succeed("${gsettings} org.gnome.desktop.interface gtk-theme | grep -q ${if appearance == "light" then "SnapOS-Light" else "Colloid-Red-Dark"}")
      machine.succeed("${gsettings} org.gnome.desktop.interface color-scheme | grep -q ${if appearance == "light" then "prefer-light" else "prefer-dark"}")
    '';
  };
  plasma = appearance: mk {
    name = "plasma" + (if appearance == "light" then "-light" else ""); desktop = "plasma"; inherit appearance;
    processes = [ "plasmashell" "kwin_x11" ];
    extra = ''
      machine.succeed("grep -q 'ColorScheme=${if appearance == "light" then "BreezeLight" else "BreezeDark"}' /etc/xdg/kdeglobals")
      machine.succeed("grep -q 'Theme=${if appearance == "light" then "Papirus-Light" else "Papirus-Dark"}' /etc/xdg/kdeglobals")
      # the first-login script applied the SnapOS wallpaper, look and pins
      machine.wait_until_succeeds("su tester -c 'test -e ~/.config/snapos/plasma-look-done'", timeout=240)
      machine.succeed("su tester -c 'grep -rq snapos-${appearance}.jpeg ~/.config/plasma-org.kde.plasma.desktop-appletsrc'")
      machine.succeed("su tester -c 'grep -q applications:snapguard.desktop ~/.config/plasma-org.kde.plasma.desktop-appletsrc'")
    '';
  };
  xfce = appearance: let
    xq = "su tester -c 'DBUS_SESSION_BUS_ADDRESS=unix:path=/run/user/$(id -u)/bus DISPLAY=:0 xfconf-query";
    theme = if appearance == "light" then "SnapOS-Light" else "Colloid-Red-Dark";
  in mk {
    name = "xfce" + (if appearance == "light" then "-light" else ""); desktop = "xfce"; inherit appearance;
    processes = [ "xfce4-panel" "xfwm4" "xfdesktop" ];
    extra = ''
      machine.succeed("grep -q '${theme}' /etc/xdg/xfce4/xfconf/xfce-perchannel-xml/xsettings.xml")
      machine.wait_until_succeeds("su tester -c 'test -e ~/.config/snapos/xfce-look-done'", timeout=240)
      machine.succeed("${xq} -c xfce4-desktop -l -v | grep -q snapos-${appearance}.jpeg'")
      machine.succeed("${xq} -c xsettings -p /Net/ThemeName | grep -q ${theme}'")
      machine.succeed("${xq} -c xfce4-panel -p /panels/panel-1/position | grep -q p=12'")
      machine.succeed("${xq} -c xfce4-panel -p /plugins/plugin-1 | grep -q whiskermenu'")
      machine.wait_until_succeeds("pgrep -u tester -f nm-applet", timeout=120)
    '';
  };
  hyprland = appearance: mk {
    name = "hyprland" + (if appearance == "light" then "-light" else ""); desktop = "hyprland"; inherit appearance;
    processes = [ "Hyprland" "waybar" "mako" "swaybg" ];
    vm = {
      # no GPU in the VM: a virtio display with software rendering
      virtualisation.qemu.options = [ "-vga none -device virtio-gpu-pci" ];
      environment.variables.WLR_RENDERER_ALLOW_SOFTWARE = "1";
      environment.variables.AQ_NO_ATOMIC = "1";
    };
    extra = ''
      machine.succeed("test -f /etc/snapos/hypr/hyprland.conf")
      machine.succeed("grep -q 'snapguard-watch' /etc/snapos/hypr/hyprland.conf")
      machine.wait_until_succeeds("su tester -c 'test -f ~/.config/hypr/hyprland.conf'", timeout=120)
      # the bar is a layer of Hyprland, the wallpaper program runs
      hy = "su tester -c 'export XDG_RUNTIME_DIR=/run/user/$(id -u); export HYPRLAND_INSTANCE_SIGNATURE=$(ls $XDG_RUNTIME_DIR/hypr | head -1); "
      machine.wait_until_succeeds(hy + "hyprctl layers | grep -q waybar'", timeout=120)
      machine.execute(hy + "hyprctl layers > /tmp/hypr-layers.txt; hyprctl clients > /tmp/hypr-clients.txt'")
      machine.succeed("grep -q waybar /tmp/hypr-layers.txt")
      # the title bars come from the hyprbars plugin
      machine.wait_until_succeeds(hy + "hyprctl plugins list | grep -q hyprbars'", timeout=60)
    '';
  };
in
{
  desktop = budgie "dark";
  desktop-light = budgie "light";
  plasma = plasma "dark";
  plasma-light = plasma "light";
  xfce = xfce "dark";
  xfce-light = xfce "light";
  hyprland = hyprland "dark";
  hyprland-light = hyprland "light";
  inherit antivirus;
}
