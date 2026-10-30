# Known issues

## Character rendering

- Some races/backgrounds still differ from the original in lighting, glow, fog, and material appearance.
- Night Elf eye glow is selected and submitted but has been reported missing visually. `window.characterPreviewStatus.eye_glow_triangles` reports submitted geometry, not visible pixels.
- Character scale/framing and held-item grips were recently changed and still require manual comparison across races and sexes.
- Shoulder/head attachments and complete equipment/geoset behavior are unfinished.
- Scalp/facial customization, ranged-weapon selection, shield mounts, and roster clothing were recently corrected from local resource inspection; their final appearance still requires manual comparison.
- The portable bloom kernel and final lighting are not established as native pixel equivalents.

## Interface and media

- Credits/options layout, transitions, and performance need further review.
- Audio may require a browser user gesture; browser autoplay policy applies.
- Audio/cinematic decoding and startup behavior depend on supplied files and supported media encodings.
- Some actions and configuration controls remain intentionally limited while the original default presentation is reconstructed.

## Networking and gameplay

- The bridge currently accepts auth/world routes on the same machine. Public-server compatibility is not guaranteed.
- Session stability, retry behavior, and server-specific protocol behavior need further work.
- Character creation is under development; the presence of a preview does not establish successful creation on every server.
- Initial outdoor world entry is available. General in-game gameplay is not implemented.

## Build and data

- The runtime is bounded to the selected WotLK build/profile. Executables with different compatibility strings or particle-table layouts are rejected.
- Cinematics must be present in the supplied local client. Conversion requires `ffmpeg` and `ffprobe`.
- The first build needs access to pinned public build dependencies. Generated outputs and extracted client data are private local state, not repository contents.

## First outdoor world entry

- Resource-load failures show a Retry/Cancel dialog while preserving the world connection. They are reported separately from transport/server disconnections; manual recovery validation is still pending.

- World entry has been exercised manually. The latest streaming, introduction and frame-presentation changes still need manual comparison; compilation and resource decoding do not establish native visual equivalence.
- Outdoor scenes admit terrain, WMO exterior groups, their selected/default decoration sets and referenced M2 props. Particle/UV-driven props use the C/WASM model and particle pipeline. Water, portal-based indoor traversal, complete material permutations and native world-light blending remain incomplete.
- Sky color bands and time selection come from owner Light tables and server time. `window.worldStatus.game_time_minutes` exposes the current server-clock minute for manual day/night comparison. Cloud projection is reconstructed; full regional skyboxes, celestial bodies and exact native sky pixels remain unfinished.
- Repeated effect models share a simulation in local space and receive per-instance camera transforms. Independent native RNG/phase per emitter instance is not reconstructed.
- Ground movement uses loaded terrain heights. Building/object collision, gravity, jumping, swimming, transports and camera collision are not implemented. Stay outdoors near the starting location.
- The player can use stand/run poses, but animation blending, backward/strafe-specific poses and authoritative object-update reconciliation remain incomplete.
- Server object-update packets are received but their entities are not reconstructed yet. NPCs, other players, combat, quests and the in-game HUD are not available.
- Introduction cameras and narration use the current client's CinematicSequences, CinematicCamera and SoundEntries data. Escape confirmation currently reuses the C/Lua glue dialog; the complete in-game cinematic frame and letterboxing are not reconstructed yet.
- Outdoor streaming keeps a 3x3 terrain neighborhood, a bounded ground cache and up to 256 distinct structure/decoration assets. Entry initially waits for the camera/player tiles; neighboring tiles and subsequent scenery stream without pausing narration. Camera-relative terrain/static-structure projection and a shared per-frame effect clock reduce precision/phase instability, but visual validation remains manual. This is an initial-zone implementation, not unrestricted world traversal.

- Character deletion uses the original typed confirmation and server result. Its dialog-open hook captures the selected GUID without replacing the original Lua delete function. Live deletion/error-flow validation remains manual; no character is removed optimistically or automatically retried.
