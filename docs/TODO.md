# TODO

Keep this file and `STATUS.md` current when a milestone changes. Check a visual task only after manual comparison; a successful build alone does not establish appearance.

## Completed foundations

- [x] Original local GlueXML/Lua running through C/WASM.
- [x] Canvas UI, keyboard/pointer input, scrolling, original cursor, and browser configuration persistence.
- [x] Cinematic presentation, music/ambience, and login background animation.
- [x] Local bridge, authentication, realms, and character roster handling.
- [x] Character creation UI and source-derived race/class availability.
- [x] Character backgrounds, skin/outfit composition, particles, and held-item loading.
- [x] Application/build sources separated from local research and historical tests.

## Immediate priorities

- [ ] Resolve missing Night Elf eye glow.
- [ ] Finish manual review of character lighting, scale/framing, and background color/effects.
- [ ] Finish weapon alignment, hand grip, and shield placement across races and sexes.
- [ ] Complete clothing geosets, shoulder/head attachments, and remaining equipment visuals.
- [ ] Confirm customization, randomization, rotation, and selected button states remain consistent.
- [ ] Complete manual review of create/delete submission, server errors, roster refresh and selected-character behavior.
- [ ] Verify retry/reconnect paths recover cleanly after a failed action or server disconnect.
- [ ] Finish options/credits layout, transitions, and performance review.

## Login and session completion

- [ ] Document supported local server configurations and remaining protocol limitations.
- [ ] Review session lifetime, keepalives, timeout handling, and protocol error recovery.
- [ ] Define supported international input/password behavior.
- [ ] Finish functional settings after visual/default behavior is stable.

## In-world milestones

- [ ] Manually validate the new selected-character world-entry path and retry/disconnect recovery.
- [ ] Manually validate initial outdoor terrain, WMO/static decoration and character placement; finish water, sky, lighting, collision and animated scenery.
- [ ] Validate initial ground movement/camera controls; implement authoritative world objects, reconciliation, collision, gravity, jumping, swimming and transports.
- [ ] Compare race introductions, sky/cloud rendering, WMO decoration, particles, narration and skip-to-player handoff against the native client.
- [x] Load the initial selected original FrameXML/Lua dependency closure in a separate C/WASM VM and draw player/main-menu artwork inside the game canvas.
- [ ] Finish general FrameXML lifecycle activation and remaining source dependencies; add the portrait and real unit-state updates.
- [ ] Decode authoritative player/object fields needed by Unit and action APIs; deliver initial world-entry and player-state events to the original Lua frames.
- [ ] Bring up the minimal in-game HUD (player frame, action bars, chat, main menu), then enable controls against real server state.
- [ ] In-game objects/NPCs, interaction, inventory, spells, and quests.
- [ ] Streaming, resource budgets, and performance work for large scenes.

## Hosting and release preparation

- [ ] Separate a deployable browser bundle from owner-local preparation services.
- [ ] Support production transport URLs, HTTPS/WSS, and a defined remote-server deployment model.
- [ ] Document a supported asset delivery strategy without including game content in Git.
- [ ] Choose a project license and review notices before any public release.
