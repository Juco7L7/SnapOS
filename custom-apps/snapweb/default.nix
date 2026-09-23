{ lib, wrapFirefox, firefox-unwrapped, runCommand }:

let
  start = runCommand "snapweb-start" { } ''
    mkdir -p $out
    cp ${./start/index.html} $out/index.html
    cp ${./start/logo.png} $out/logo.png
  '';

  chromeCss = runCommand "snapweb-userchrome.css" { } ''
    cp ${./chrome/userChrome.css} $out
  '';

  autoconfig = runCommand "snapweb.cfg" { } ''
    substitute ${./autoconfig.js} $out \
      --subst-var-by css ${chromeCss} \
      --subst-var-by start ${start}
  '';

  icons = ../../branding/icons;
in
(wrapFirefox firefox-unwrapped {
  pname = "snapweb";
  icon = "snapweb";
  wmClass = "snapweb";

  extraPrefsFiles = [ autoconfig ];

  extraPolicies = {
    DisableTelemetry = true;
    DisableFirefoxStudies = true;
    DisablePocket = true;
    DontCheckDefaultBrowser = true;
    OverrideFirstRunPage = "";
    OverridePostUpdatePage = "";
    DisplayBookmarksToolbar = "always";
    NoDefaultBookmarks = true;
    SearchEngines.Default = "Google";
    Homepage = {
      URL = "file://${start}/index.html";
      StartPage = "homepage";
    };
    FirefoxHome = {
      Search = true;
      TopSites = false;
      SponsoredTopSites = false;
      Highlights = false;
      Pocket = false;
      SponsoredPocket = false;
    };
    FirefoxSuggest = {
      WebSuggestions = false;
      SponsoredSuggestions = false;
      ImproveSuggest = false;
    };
    UserMessaging = {
      ExtensionRecommendations = false;
      FeatureRecommendations = false;
      UrlbarInterventions = false;
      SkipOnboarding = true;
      MoreFromMozilla = false;
      WhatsNew = false;
    };
    EnableTrackingProtection = {
      Value = true;
      Cryptomining = true;
      Fingerprinting = true;
      EmailTracking = true;
    };
  };
}).overrideAttrs (old: {
  buildCommand = old.buildCommand + ''
    if [ -L "$out/share/icons" ]; then
      target=$(readlink "$out/share/icons")
      rm "$out/share/icons"
      mkdir -p "$out/share/icons"
      cp -r "$target"/. "$out/share/icons/"
      chmod -R u+w "$out/share/icons"
    fi
    for s in 16 32 48 64 128 256; do
      d="$out/share/icons/hicolor/''${s}x''${s}/apps"
      mkdir -p "$d"
      rm -f "$d/snapweb.png"
      cp ${icons}/snapfox-$s.png "$d/snapweb.png"
    done
    ln -s "$out/bin/firefox" "$out/bin/snapweb"

    # The menu entry is SnapWeb, not Firefox.
    sed -i 's/^Name=.*/Name=SnapWeb/' "$out"/share/applications/*.desktop
    grep -q '^Name=SnapWeb$' "$out"/share/applications/*.desktop

    # Firefox runs the autoconfig file in a sandbox without XPCOM unless this
    # is off. The red look and the new tab page both need XPCOM.
    for f in "$out"/lib/*/defaults/pref/autoconfig.js; do
      echo 'pref("general.config.sandbox_enabled", false);' >> "$f"
    done
    grep -q sandbox_enabled "$out"/lib/*/defaults/pref/autoconfig.js
  '';
})
