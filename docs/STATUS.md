# Project status

Updated: 2026-10-30.

brWoWser targets the WotLK 3.3.5a build 12340 login, character-management and initial outdoor-world flow. An initial outdoor world-entry implementation is now available for manual validation; general in-world gameplay is not implemented.

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

- Character deletion preserves the original typed confirmation, captures the confirmed GUID, waits for the server response and refreshes the roster on success.

## First outdoor world entry

- Enter World sends the selected character GUID and uses the server-provided map and position.
- World time synchronization is answered while the existing session services remain active.
- A C/WASM worker decodes ADT terrain, texture alpha maps, static WMO groups, static M2 decoration and instance transforms.
- C/WASM evaluates the source introduction camera spline, terrain heights, ground movement, player placement and view matrices. JavaScript owns browser I/O, input forwarding, media playback and WebGL submission.
- Server-triggered introductions resolve their source camera and narration. Escape opens a canvas C/Lua confirmation; completion restores the follow camera.
- Initial outdoor controls support forward/backward movement, turning, strafing, right-button camera orbit and wheel zoom, with server movement packets and source run/left/right/backward poses. Diagonal visual facing is computed in WASM.
- Archive sector reads use a bounded block cache; decoded textures transfer to the browser without another buffer copy. Frame samples take priority over background reads, and a bounded queue loads visible scenery instead of every asset in each tile.
- ADT object records persist across camera movement and cinematic skipping; C/WASM recomputes their visibility for the current view. Terrain bounds are prepared once during decoding.
- WMO decoration sets and exterior-group references admit the five Valley of Heroes statues and source-authored props. Torches and chimneys use original particle records with instance-local camera transforms.
- Sky color bands and their spatial/time selection run in C/WASM from Light tables; server game time anchors the clock.
- World drawing is presented at an animation-frame boundary. Input/audio acknowledgements no longer present UI-only frames over the world.
- Builds and bounded owner-resource decoding have been inspected. The latest visual changes, load times, deletion controls and frame stability require manual validation.

## Initial in-game FrameXML shell

- A dedicated world-UI worker owns a separate C/Lua VM from the login session. It reads the owner-local `FrameXML.toc` and a bounded dependency closure; original sources stay outside Git.
- The first profile loads 32 original Lua/XML resources, fonts and textures for `WorldFrame`, `UIParent`, the player-frame artwork and the lower main-menu artwork. A bounded local source import produced 253 objects and 26 draw commands without geometry/text issues; browser appearance remains unverified.
- C/WASM owns frame composition, font rasterization, StatusBar geometry and the initial unit identity APIs. Name, level, race, class, sex and GUID come from the authenticated roster and client metadata.
- The shell appears after the introduction ends or is skipped, inside the existing canvas. It remains hidden during the introduction. Loading failures are isolated from the world connection and exposed through `window.worldUIStatus`.
- General component `OnLoad`/`OnUpdate` lifecycles are explicitly deferred. Original callback definitions are retained, not replaced with successful no-ops. Full FrameXML loading, unit state, portrait generation, action buttons, chat and menu interaction remain unfinished.

## Static collision and initial world audio

- Legacy ADT/WMO model references ending in `.mdl` or `.mdx` normalize to `.m2` before lookup. This includes the reported `TORCH.MDL` reference.
- The WASM world module admits source M2 bounding triangles and WMO collision/render faces, transforms instance geometry, performs capsule/triangle checks, samples walkable model surfaces and integrates short movement steps with sliding. Nearby collision loading is independent of the render frustum; nearby WMO interior geometry can also be drawn.
- A bounded owner-resource preparation around Northshire admitted 57 collision meshes / 165 instances containing 9,643 triangles from the selected nearby records, including nine WMO groups. This is asset admission evidence; movement behavior remains manually validated.
- WASM resolves source area inheritance and SoundEntries file selection for music/ambience/intro channels. The browser supplies media decoding/output without blocking world frame delivery. Northshire's source data resolved music 2523 and ambience 4183 during local preparation.
- World locomotion reuses one movement pose VM across the admitted source clips rather than allocating another full VM for every direction.
- The model-light argument regression introduced with the FrameXML type extension is corrected: the native bridge reads exactly the 13 supplied light values using the array's length.

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
