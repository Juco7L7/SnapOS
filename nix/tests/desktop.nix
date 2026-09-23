{ pkgs }:

let
  mk = name:
    pkgs.testers.runNixOSTest {
      name = "snapos-${name}";

      nodes.machine = { lib, ... }: {
        imports = [ ../../configuration.nix ];

        networking.hostName = lib.mkForce "machine";

        virtualisation.memorySize = 3072;
        virtualisation.cores = 2;
        virtualisation.resolution = { x = 1280; y = 800; };

        users.users.tester = {
          isNormalUser = true;
          password = "tester";
          extraGroups = [ "wheel" ];
        };
        services.displayManager.autoLogin = {
          enable = true;
          user = "tester";
        };
      };

      testScript = ''
        machine.start()
        machine.wait_for_unit("multi-user.target")
        machine.sleep(30)
        machine.execute("cat /proc/cmdline > /tmp/cmdline.txt; uname -r > /tmp/kernel.txt; ps -eo user,comm,args > /tmp/ps.txt")
        machine.execute("systemctl status display-manager --no-pager > /tmp/dm-status.txt 2>&1")
        machine.execute("journalctl -b --no-pager > /tmp/journal.txt")
        machine.execute("tar czf /tmp/xlogs.tgz /var/log/lightdm /var/log/X.0.log 2>/dev/null; true")
        for f in ["cmdline.txt", "kernel.txt", "ps.txt", "dm-status.txt", "journal.txt", "xlogs.tgz"]:
            machine.copy_from_vm("/tmp/" + f)
        machine.screenshot("01-state")
        machine.wait_for_unit("display-manager.service")
        machine.wait_until_succeeds("pgrep -u tester -f 'labwc|budgie-wm'", timeout=300)
        machine.wait_until_succeeds("pgrep -u tester -f 'budgie-panel|budgie-desktop'", timeout=300)
        machine.sleep(30)
        machine.screenshot("02-desktop")
        machine.execute("(echo 'variant: ${name}'; free -m; echo; echo running services: $(systemctl list-units --type=service --state=running --no-legend | wc -l); echo processes: $(ps -e | wc -l)) > /tmp/metrics.txt")
        machine.copy_from_vm("/tmp/metrics.txt")
        machine.succeed("sudo -u tester env DBUS_SESSION_BUS_ADDRESS=unix:path=/run/user/1000/bus gsettings get org.gnome.desktop.peripherals.touchpad disable-while-typing | grep -q false")
        machine.succeed("sudo -u tester env DBUS_SESSION_BUS_ADDRESS=unix:path=/run/user/1000/bus gsettings get org.gnome.desktop.interface gtk-theme | grep -q Colloid-Red-Dark")
        machine.succeed("sudo -u tester env DBUS_SESSION_BUS_ADDRESS=unix:path=/run/user/1000/bus gsettings get org.gnome.desktop.interface color-scheme | grep -q prefer-dark")
        machine.succeed("grep -q dark /etc/snapos/appearance")
        machine.succeed("test -f /etc/xdg/autostart/snaphelper.desktop")
        machine.succeed("test -s /run/current-system/sw/share/snapos/helper/snappy-declares.gif")
        machine.succeed("snapos help")
        machine.succeed("snapguard status")

        # The update guard: a new system is approved once a user has logged in
        # (tester is logged in by autologin), and a system that never got
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
        # (a test VM has no system profile generations, so the switch itself is
        # covered by tests/run.sh with a stand-in profile)
      '';
    };
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
{
  desktop = mk "desktop";
  inherit antivirus;
}
