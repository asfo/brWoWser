# Third-party dependencies

No third-party game executable, asset pack, font, extracted script, or capture is committed.

Build/runtime dependencies are obtained separately:

- `@yowasp/clang` 21.1.4-3: public WASM compiler package, pinned by npm and its lockfile.
- Lua 5.1.1: public source from lua.org, archive SHA-256 verified by `tools/setup.py`.
- FreeType 2.15.0: public source archive, SHA-256 verified by `tools/setup.py`.
- libx86emu: public source revision and per-file hashes pinned in `runtime/network/x86-toolchain.json`; fetched by the networking build.
- `ws`: Node WebSocket transport dependency, pinned by `package-lock.json`.
- ESLint, Prettier, Ruff, and clang-format: development tools described in the dependency manifests.
- `ffmpeg`/`ffprobe`: system tools used to convert owner-local cinematics; installed separately.

Their licenses and upstream notices continue to apply. Downloaded source, package license files, and build artifacts remain in ignored dependency directories. The source repository does not grant a license to Blizzard software or assets. A project-wide license for brWoWser has not yet been selected.
