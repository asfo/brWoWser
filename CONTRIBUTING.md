# Contributing

brWoWser is an experimental WotLK browser client. See `docs/ARCHITECTURE.md`, `docs/STATUS.md`, and `docs/TODO.md` before changing a runtime boundary.

- Use English for source, comments, documentation, and user-facing strings.
- Keep client-owned state and calculations in C/WASM where practical; browser code provides platform APIs and WebGL submission.
- Preserve original Lua/XML behavior where supported. Document approximations and unsupported behavior explicitly.
- Keep assets, executables, credentials, private configuration, captures, and historical research out of commits.
- Do not change external server source or configuration as part of a client fix.
- Update status, known issues, and TODOs when behavior changes. Distinguish a successful build from manual visual/runtime confirmation.

## Code style

```sh
npm ci
python3 -m venv .venv
.venv/bin/python -m pip install -r requirements-dev.txt
npm run format
npm run lint
```

C uses clang-format, Python uses Ruff, and JavaScript uses Prettier/ESLint. `scripts/repository_policy.py` limits formatting and source selection to distributable files, keeping private local material untouched.

Runtime and visual review is currently performed manually in the client. No automated client tests or CI workflows are shipped. Use `npm run build` for compilation and provide concise manual reproduction steps for behavior changes.
