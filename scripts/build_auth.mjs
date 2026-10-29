import { createHash } from 'node:crypto';
import { readFile, writeFile, mkdir } from 'node:fs/promises';
import { x86Sources } from './x86_toolchain.mjs';
import { fileTree } from '../engine/font-runtime/toolchain.mjs';
import { runClang } from '../tools/compiler.mjs';
const root = new URL('../', import.meta.url);
const files = Object.fromEntries(
  await Promise.all(
    [
      'srp.c',
      'warden_transfer.c',
      'warden_inflate.c',
      'warden_signature.c',
      'warden_image.c',
      'warden_cpu.c',
      'warden_memory.c',
      'warden_memory.h',
      'warden_client_image.c',
      'warden_client_image.h',
      'warden_cpu_mmx.c',
      'warden_pages.c',
      'warden_checks.c',
    ].map(async (name) => [name, await readFile(new URL('runtime/network/' + name, root))]),
  ),
);
Object.assign(files, await x86Sources());
// Keep upstream diagnostic noise scoped to third-party translation units.
const upstreamDiagnostics = Buffer.from(
  '#pragma clang diagnostic ignored "-Wunused-parameter"\n' +
    '#pragma clang diagnostic ignored "-Wsign-compare"\n' +
    '#pragma clang diagnostic ignored "-Wunterminated-string-initialization"\n',
);
for (const name of Object.keys(files))
  if (name.startsWith('x86/') && name.endsWith('.c'))
    files[name] = Buffer.concat([upstreamDiagnostics, files[name]]);
const sourceHash = createHash('sha256');
for (const [name, bytes] of Object.entries(files)) sourceHash.update(name + '\0').update(bytes);
const fingerprint = sourceHash
  .update(await readFile(new URL('scripts/build_auth.mjs', root)))
  .digest('hex');
const buildFolder = new URL('runtime/network/build/', root);
let cached = false;
try {
  const receipt = JSON.parse(await readFile(new URL('receipt.json', buildFolder), 'utf8'));
  const bytes = await readFile(new URL('auth.wasm', buildFolder));
  cached =
    receipt.fingerprint === fingerprint &&
    receipt.wasm_sha256 === createHash('sha256').update(bytes).digest('hex');
} catch {
  /* Missing or stale build is rebuilt below. */
}
if (cached) console.log(JSON.stringify({ status: 'AUTH_WASM_CURRENT' }));
else {
  const output = await runClang(
    [
      'clang',
      '-O2',
      '-Wall',
      '-Wextra',
      '-mexec-model=reactor',
      '-Wl,-z,stack-size=65536',
      '-Wl,--max-memory=67108864',
      ...[
        'bw_world_auth',
        'bw_warden_receive',
        'bw_warden_tick',
        'bw_warden_audit',
        'bw_warden_local',
        'bw_warden_reply_length',
        'bw_warden_client_begin',
        'bw_warden_client_chunk',
        'bw_warden_client_finish',
        'bw_warden_progress',
        'bw_warden_dependency',
        'bw_world_encrypt',
        'bw_world_decrypt',
        'bw_world_clear',
        'bw_world_characters',
        'bw_world_character_count',
        'bw_world_character_name',
        'bw_world_character_number',
        'bw_auth_io',
        'bw_auth_clear',
        'bw_auth_credentials',
        'bw_auth_challenge',
        'bw_auth_verify',
      ].map((name) => '-Wl,--export=' + name),
      '-o',
      'auth.wasm',
      'srp.c',
      'warden_cpu.c',
      'warden_memory.c',
      'warden_client_image.c',
      'x86/decode.c',
      'x86/ops.c',
      'x86/ops2.c',
      'x86/prim_ops.c',
    ],
    fileTree(files),
  );
  await mkdir(new URL('runtime/network/build/', root), { recursive: true });
  await writeFile(new URL('runtime/network/build/auth.wasm', root), output['auth.wasm']);
  await writeFile(
    new URL('receipt.json', buildFolder),
    JSON.stringify({
      fingerprint,
      wasm_sha256: createHash('sha256').update(output['auth.wasm']).digest('hex'),
    }),
  );
  console.log(JSON.stringify({ status: 'AUTH_WASM_BUILT' }));
}
