# Known issues

## Character rendering

- Some races/backgrounds still differ from the original in lighting, glow, fog, and material appearance.
- Night Elf eye glow is selected and submitted but has been reported missing visually. `window.characterPreviewStatus.eye_glow_triangles` reports submitted geometry, not visible pixels.
- Character scale/framing and held-item grips were recently changed and still require manual comparison across races and sexes.
- Shoulder/head attachments and complete equipment/geoset behavior are unfinished.
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
- Entering the world and in-game gameplay are not implemented.

## Build and data

- The runtime is bounded to the selected WotLK build/profile. Executables with different compatibility strings or particle-table layouts are rejected.
- Cinematics must be present in the supplied local client. Conversion requires `ffmpeg` and `ffprobe`.
- The first build needs access to pinned public build dependencies. Generated outputs and extracted client data are private local state, not repository contents.
