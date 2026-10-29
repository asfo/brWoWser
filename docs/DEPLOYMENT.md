# Running and hosting

## Local development

Place your compatible client at the project root (`Wow.exe`, `Data/`, and a locale with cinematics), then run:

```sh
npm ci
npm run setup
npm run build
npm run dev
```

The browser entry is `http://127.0.0.1:8787/?startup&live`. The bridge listens on port 8788 and defaults to a local auth server at `127.0.0.1:3724`. Neither command launches the native game.

`npm run dev:restart` checks that the current listeners belong to this checkout before restarting them. `npm run dev:fresh` starts a comparison session with fresh browser configuration. Existing native client data is not deleted.

## Build only

`npm run build` compiles the required WASM modules and networking module. It does not run browser/native tests or authenticate an account. `npm run build -- --force` bypasses the build cache. Public Lua and FreeType sources are prepared automatically if absent.

Build outputs stay under ignored `build/` folders; private preparation state stays in ignored application build folders and `.local/`. Recreate them from the checked-in source plus your client files.

## Hosting limits

This currently runs as a Node-assisted application, not a standalone static-site bundle. `npm run serve` starts only the web/resource service after a build; `npm run bridge` starts the transport separately. The combined launcher is the supported local entry point.

The web/resource service and bridge bind to loopback. The current bridge only admits auth/world targets on the same machine. To experiment on a remote host, the compatible game server must also satisfy that restriction; arbitrary remote realms are not supported by this deployment profile. Browser transport URLs/origin handling still need a production configuration pass.

WebAssembly workers use `SharedArrayBuffer`; responses must retain the application's cross-origin isolation headers. HTTPS/WSS, reverse proxy routing, static export, remote asset delivery, and a supported public deployment configuration remain TODOs. Do not treat copying the HTML to a static host as a complete deployment.

Client assets are required at runtime but are not part of the source distribution. Decide how you are authorized to supply them before exposing an installation beyond your local environment.
