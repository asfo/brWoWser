# Project status

Updated: 2026-10-29.

brWoWser targets the WotLK 3.3.5a build 12340 login-to-character-creation flow. It is not yet a playable in-world client.

## Implemented

- C/WASM interface object lifecycle, original local GlueXML/Lua loading, text and texture quads, clipping, scrollbars, button states, edit boxes, and pointer/keyboard input.
- Full-viewport canvas presentation, original software cursor, notices, and persisted browser configuration.
- Local cinematic preparation and canvas playback, including Escape handling.
- Music, ambience, UI sounds, and selected scene cues.
- Login scene rendering with animated models, textures, material passes, and particles.
- Authentication, realm selection, and character roster protocol handling through a local WebSocket bridge.
- Character creation interface, supported race/class combinations from `CharBaseInfo`, customization, source-based backgrounds, player skin composition, and held weapons/shields.
- Animated hand anchors and `HandsClosed` finger tracks for occupied hands.
- Uncovered ears select geoset 702 across character models; models with ears integrated into the body retain their existing geometry. Final visual review remains manual.
- Character selection applies clothing textures and garment geosets from server-reported equipment, using the same composition path as creation previews.
- Bald hair variants retain their scalp mesh; facial customization maps the source table's 1/3/2 geoset order for features such as piercings and tusks.
- Hunters preview their source-provided ranged weapon. Bows use the left palm; shields use the forearm mount while retaining the left-hand grip pose.

## Current review

- Lighting and camera framing were recently restored after regressions. Final appearance still needs manual comparison.
- Weapon placement/grip and tall-race framing need further manual review.
- Night Elf eye glow is admitted by the renderer but its reported absence remains unresolved.
- Reconnecting, retrying failed actions, character creation results, and session lifetime need further review.

## Compatibility limits

The project contains client-side auth/world protocol and module-processing components. Their presence is not a promise that every server or server configuration works. The local bridge currently restricts auth/world routes to the same machine. Remote server support and public hosting are not complete.

The runtime does not launch `Wow.exe`: it reads required owner-local data and reconstructs supported behavior. Browser APIs provide WebGL, audio/media decoding, storage, workers, and network transport. Native rendering, timing, complete UI behavior, and in-world gameplay are not fully reproduced.

## Source distribution

The repository contains the application and its build inputs. Historical research, captures, tests, game files, downloaded public source archives, and generated caches remain outside Git. Builds and resource preparation do not depend on retained native captures.
