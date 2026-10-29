# brWoWser

**World of Warcraft: Wrath of the Lich King in your browser.**

brWoWser is an experimental browser client for WotLK **3.3.5a, build 12340**. It reconstructs client behavior in C/WebAssembly, runs the original local GlueXML/Lua interface, and presents the game through a WebGL canvas. It does not stream a running desktop client.

The current milestone covers startup, login, realm selection, and character selection/creation previews. **Entering and playing the world is not implemented.** Visual fidelity and server compatibility are still works in progress.

## Run locally

Requirements:

- Node.js 22.13 or newer and npm.
- Python 3.11 or newer.
- `ffmpeg` and `ffprobe` on `PATH` for cinematic conversion.
- Your own compatible WotLK client, with `Wow.exe` and `Data/` in the project root, including a locale folder and loose cinematics.
- A local compatible auth/world server for login. The bridge currently accepts server routes on the same machine.

```sh
npm ci
npm run setup
npm run build
npm run dev
```

Open **http://127.0.0.1:8787/?startup&live**. The web server and WebSocket bridge start together. The first build downloads pinned public compiler/source dependencies; subsequent builds reuse verified local outputs.

```sh
npm run dev:restart  # Restart this project's web server and bridge
npm run dev:fresh    # Start with a fresh browser configuration for comparison
```

Client settings and accepted notices normally persist in browser storage. Original game files, local caches, credentials, server checkouts, and generated assets are excluded from Git. The browser receives resources from your local client; the repository contains no game executable, asset pack, font, Lua/XML script dump, or captured payload.

## What works today

- Original login interface, notices, options/credits screens, keyboard and pointer input, software cursor, and configuration persistence.
- Canvas cinematic presentation, music, ambience, and selected sound effects.
- Animated login and character background scenes, player models, customization, starting clothes, and held-item attachment rendering.
- Authentication, realm listing/selection, character roster handling, and the character creation flow under development.
- A local WebSocket-to-TCP bridge and client-side server-protocol handling.

These are implementation milestones, not a claim of complete native equivalence. See [current status](docs/STATUS.md), [known issues](docs/KNOWN_ISSUES.md), and the [TODO list](docs/TODO.md).

## Project layout

| Path          | Purpose                                                                    |
| ------------- | -------------------------------------------------------------------------- |
| `app/client/` | Browser entry point, workers, local resource preparation, and web server   |
| `runtime/`    | C/WASM client implementation and browser platform boundaries               |
| `engine/`     | Required module adapters, decoders, build entry points, and worker helpers |
| `config/`     | Runtime defaults, supported interface resources, and audio/UI catalogues   |
| `tools/`      | Reproducible build and public dependency setup                             |
| `scripts/`    | Development launcher, bridge, networking build, and style commands         |
| `docs/`       | Architecture, deployment, status, TODOs, and known issues                  |

Research, captures, previous experiments, and historical tests remain local and are not part of the source distribution. See [architecture](docs/ARCHITECTURE.md) and [deployment](docs/DEPLOYMENT.md) for the current runtime boundaries.

## Development

```sh
python3 -m venv .venv
.venv/bin/python -m pip install -r requirements-dev.txt
npm run format:check
npm run lint
```

See [contributing](CONTRIBUTING.md). Runtime and visual review is currently manual; this repository does not ship automated client tests or CI workflows.

## Distribution

This is an unofficial, incomplete project, not affiliated with or endorsed by Blizzard Entertainment. Supply your own client files; do not commit or distribute them with this source. No project-wide open-source license has been selected. Public build dependencies retain their own licenses; see [third-party dependencies](docs/THIRD_PARTY.md).
