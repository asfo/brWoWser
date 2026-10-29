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
- [ ] Complete create-character submission, error display, refresh, and selection of the created character.
- [ ] Verify retry/reconnect paths recover cleanly after a failed action or server disconnect.
- [ ] Finish options/credits layout, transitions, and performance review.

## Login and session completion

- [ ] Document supported local server configurations and remaining protocol limitations.
- [ ] Review session lifetime, keepalives, timeout handling, and protocol error recovery.
- [ ] Define supported international input/password behavior.
- [ ] Finish functional settings after visual/default behavior is stable.

## In-world milestones

- [ ] Enter world with a selected character.
- [ ] Load maps, terrain, world models, and character placement.
- [ ] Movement, camera controls, world updates, and interpolation.
- [ ] In-game UI, objects/NPCs, interaction, inventory, spells, and quests.
- [ ] Streaming, resource budgets, and performance work for large scenes.

## Hosting and release preparation

- [ ] Separate a deployable browser bundle from owner-local preparation services.
- [ ] Support production transport URLs, HTTPS/WSS, and a defined remote-server deployment model.
- [ ] Document a supported asset delivery strategy without including game content in Git.
- [ ] Choose a project license and review notices before any public release.
