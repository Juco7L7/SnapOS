# Custom apps

Each folder here is one app that SnapOS ships in its own version. The flake
picks up every folder automatically: `custom-apps/<name>/default.nix` becomes
`pkgs.<name>`, and you enable it by adding `<name>` to `configuration.nix`.

`default.nix` is an ordinary Nix package. It may fetch the upstream source with
`fetchFromGitHub` and apply patches, or wrap a package that already exists in
nixpkgs and change its settings. The GitHub workflow builds everything the
system uses, so a new app is built the next time the ISO is built.

## snapweb

The web browser. It is the official Firefox build with SnapOS changes applied
on top:

| File | What it changes |
| --- | --- |
| `default.nix` | name, icon and desktop entry, policies (no telemetry, no sponsored content, tracking protection, bookmarks bar always visible, Google as the search engine), the start page |
| `chrome/userChrome.css` | the browser's look: dark surfaces with the red accent |
| `autoconfig.js` | loads the stylesheet above into every window and makes every new tab open the start page |
| `start/` | the start page (fox logo and a Google search box) |

Building Firefox itself from source takes hours and far more memory than a
GitHub runner has, so SnapWeb reuses the official build and changes its
configuration.
