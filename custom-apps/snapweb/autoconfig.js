try {
  const Cc = Components.classes;
  const Ci = Components.interfaces;
  const obs = Cc["@mozilla.org/observer-service;1"].getService(Ci.nsIObserverService);
  const ios = Cc["@mozilla.org/network/io-service;1"].getService(Ci.nsIIOService);
  const sss = Cc["@mozilla.org/content/style-sheet-service;1"].getService(Ci.nsIStyleSheetService);

  const register = function () {
    const uri = ios.newURI("file://@css@");
    if (!sss.sheetRegistered(uri, sss.USER_SHEET)) {
      sss.loadAndRegisterSheet(uri, sss.USER_SHEET);
    }
  };

  // Every new tab opens the SnapWeb start page instead of the Firefox one.
  const setNewTab = function () {
    const mod = ChromeUtils.importESModule("resource:///modules/AboutNewTab.sys.mjs");
    mod.AboutNewTab.newTabURL = "file://@start@/index.html";
  };

  obs.addObserver({
    observe: function () {
      try {
        register();
      } catch (e) {}
      try {
        setNewTab();
      } catch (e) {}
    }
  }, "final-ui-startup", false);
} catch (e) {}
