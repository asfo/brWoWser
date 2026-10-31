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
- Initial outdoor controls support forward/backward movement, turning, strafing, Space jumping, right-button camera orbit and wheel zoom, with server movement packets and source running/backward/jump poses. Diagonal visual facing is computed in WASM.
- Archive sector reads use a bounded block cache; decoded textures transfer to the browser without another buffer copy. Frame samples take priority over background reads, and a bounded queue loads visible scenery instead of every asset in each tile.
- ADT object records persist across camera movement and cinematic skipping; C/WASM recomputes their visibility for the current view. Terrain bounds are prepared once during decoding.
- WMO decoration sets and exterior-group references admit the five Valley of Heroes statues and source-authored props. Torches and chimneys use original particle records with instance-local camera transforms.
- Sky color bands and their spatial/time selection run in C/WASM from Light tables; server game time anchors the clock.
- World drawing is presented at an animation-frame boundary. Input/audio acknowledgements no longer present UI-only frames over the world.
- Builds and bounded owner-resource decoding have been inspected. The latest visual changes, load times, deletion controls and frame stability require manual validation.

## Initial in-game FrameXML shell

- A dedicated world-UI worker owns a separate C/Lua VM from the login session. It reads the owner-local `FrameXML.toc` and a bounded dependency closure; original sources stay outside Git.
- The selected profile now includes original Minimap.xml/Lua alongside WorldFrame, UIParent, PlayerFrame and MainMenuBar. Component lifecycle activation remains deferred; source import and browser appearance are separate validation steps.
- C/WASM owns frame composition, font rasterization, StatusBar geometry and the initial unit identity APIs. Name, level, race, class, sex and GUID come from the authenticated roster and client metadata.
- The shell appears after the introduction ends or is skipped, inside the existing canvas. It remains hidden during the introduction. Loading failures are isolated from the world connection and exposed through `window.worldUIStatus`.
- General component `OnLoad`/`OnUpdate` lifecycles are explicitly deferred. Original callback definitions are retained, not replaced with successful no-ops. Full FrameXML loading, action buttons, chat and menu interaction remain unfinished.

## Player portrait, resource bars and minimap

- A bounded C decoder reads raw/compressed build-12340 object updates and commits the local player's fields only after the complete packet parses. Creation initializes omitted fields to their protocol-defined zero values. Unsupported packets preserve the previous snapshot instead of closing the connection; `window.worldUnitStatus` exposes the latest decoder status.
- C/Lua APIs expose known health/max health and the active power pool/type. The original health/resource StatusBars receive these server values, including rage/runic-power scaling. This is selected-player state, not general object/NPC reconstruction.
- The owner character model supplies a cached 3D head portrait rendered to a small GPU surface. Camera/pose preparation stays in WASM and no pixels are read back to JavaScript. Native portrait framing/lighting still needs manual comparison.
- WASM resolves the owner md5translate.trs map index, nine nearby minimap textures, north-up coordinates and the initial direction marker. Original FrameXML supplies minimap placement/artwork. Zoom buttons, tracking, POIs, indoor WMO minimaps and world-map interaction are pending.
- World previews use Item.dbc sheathe types and the model's matching storage attachments. Combat-driven unsheathing/equipment changes remain pending.
- World rendering may present at up to 60 FPS. Scene decoding/simulation uses a worker separate from character/portrait/minimap texture reads; inactive character-selector ticks no longer run behind the world. Native locomotion blending and sustained frame pacing remain under review.

## Static collision and initial world audio

- Legacy ADT/WMO model references ending in `.mdl` or `.mdx` normalize to `.m2` before lookup. This includes the reported `TORCH.MDL` reference.
- The WASM world module admits source M2 bounding triangles and WMO collision/render faces, transforms instance geometry, performs capsule/triangle checks, samples walkable model surfaces and integrates short movement steps with sliding. Nearby collision loading is independent of the render frustum; nearby WMO interior geometry can also be drawn.
- A bounded owner-resource preparation around Northshire admitted 57 collision meshes / 165 instances containing 9,643 triangles from the selected nearby records, including nine WMO groups. This is asset admission evidence; movement behavior remains manually validated.
- WASM resolves source area inheritance and SoundEntries file selection for music/ambience/intro channels. The browser supplies media decoding/output without blocking world frame delivery. Northshire's source data resolved music 2523 and ambience 4183 during local preparation.
- Ground movement sweeps the capsule onto stair edges and slopes. Q/E uses running clips with travel-facing legs and a lower-spine counter-rotation that keeps the torso forward, while network facing remains independent. WASM integrates jump/gravity/landing and queues movement snapshots for the network boundary. Turn-left, turn-right, jump and landing use their distinct protocol opcodes. These changes still require manual comparison.
- World music/ambience reads run separately from terrain decompression. Browser playback state is exposed in `window.worldStatus.audioPlayback`; blocked WebAudio contexts resume on trusted user interaction.
- World locomotion reuses one movement pose VM across the admitted source clips rather than allocating another full VM for every direction.
- The model-light argument regression introduced with the FrameXML type extension is corrected: the native bridge reads exactly the 13 supplied light values using the array's length.

## Loading presentation and follow camera

- Source Map field 57 resolves LoadingScreens artwork. Original wide variants and loading-bar background/fill/border textures are decoded by the C BLP decoder, including 1024-pixel artwork.
- WASM supplies loading-screen quads and monotonic progress; the browser submits those quads to the same WebGL canvas. Resource stages update the bar without fabricated time-based advancement. The screen remains until the first world frame, including when an introduction starts.
- The follow camera contracts its orbit against admitted static collision faces and terrain; it does not change the user's requested zoom distance. Normal jump descent no longer selects the ledge/free-fall clip.
- Compilation and bounded owner-asset admission do not establish native visual equivalence. Loading composition, torso/leg orientation, landing and camera behavior require manual review.

## WASM workspace and effect memory

- Character preparation, topology, camera and material decoders share synchronous workspaces. Materials rebind their own source model/skin before evaluation; asynchronous body-skin composition is serialized to preserve each atlas.
- Bone/UV runtimes share workspaces while retaining separate model, clock/orientation inputs and copied results. Particle instances share one WASM memory; their native pools allocate only the source emitter count, retain independent RNG/live state and release it when the preview is disposed.
- Particle state currently admits 128 contexts within a 16 MiB pool budget. Shared output/input workspaces are separate from that state budget; GPU resources and whole-browser memory are not covered by this number.
- Owner-resource preparation admitted HOUSESMOKE, BLACKSMITH_SMOKE, TORCH and NEWWATERFALL without issues. BLACKSMITH_SMOKE has zero mesh vertices and two particle emitters; the decoders now admit a consistently empty source mesh without dropping its effects. No browser rendering or long-session memory validation was performed.
- Landing requires nearby walkable support; a lateral capsule contact no longer independently produces a landing event. This avoids repeated fall/land transitions when sliding against a ledge. Manual movement comparison remains pending.

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
