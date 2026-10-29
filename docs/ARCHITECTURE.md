# Architecture

brWoWser reconstructs supported WotLK client behavior rather than launching a desktop executable in a web page.

## Runtime boundaries

- **C/WebAssembly:** interface lifecycle, layout/text processing, configuration state, resource barriers, decoders, animation/particle calculations, transform composition, and client protocol components.
- **Browser/GPU:** WebGL shader execution and draw submission, workers, input delivery, storage, audio/media decoding, and canvas presentation.
- **Node services:** owner-local resource preparation/delivery and a WebSocket-to-TCP bridge for a compatible local auth/world server.
- **Owner-local client:** executable metadata, MPQ resources, fonts, original Lua/XML, and media. These are read as data and are never repository payloads.

`app/client/server.mjs` prepares the current local profile and serves the browser entry point. `current_profile.mjs` reads the configured interface resources from ranked local archives; `scene_manifest.mjs` derives login model/texture identities and prepared-buffer metadata from those files. Neither requires historical capture results.

`engine/` contains the adapters and module build inputs still needed by the runtime. `tools/build.mjs` coordinates compilation and verifies cached outputs. Public Lua/FreeType sources are checksum-pinned and kept in ignored dependency directories; the compiler is pinned in npm. The runtime itself remains in `runtime/`.

The original interface drives login and character-screen transitions. Character backgrounds, player models, and held items use authored source resources and animated attachments. Rendering and protocol coverage remain partial; consult `STATUS.md` and `KNOWN_ISSUES.md` rather than assuming complete native equivalence.

## Outdoor world entry

The character interface queues Enter World in C. The network session sends the selected GUID, admits the server's world location and cinematic trigger, and answers time synchronization. Existing session services continue independently of resource loading.

`runtime/world/world.c`, built by `engine/world/`, runs in the asset worker. It decodes ADT meshes, alpha maps and terrain holes; reads static WMO exterior groups and M2 decoration; converts instance transforms; evaluates camera splines; integrates ground movement; and builds placement/view matrices and terrain frustum masks. The browser uploads the resulting arrays through WebGL and forwards movement to the session worker.

Introductions resolve their sequence, camera origin/model and narration from owner-local DBC/MPQ resources. The browser supplies the audio playback clock; C/WASM samples the camera. Completion and confirmed skipping notify the game server. A nine-tile outdoor neighborhood and bounded caches form the initial streaming scope. Server object-update reconstruction and the full in-game interface are separate unfinished milestones.
