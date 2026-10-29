export async function makePlanner(wasm) {
  const { instance } = await WebAssembly.instantiate(wasm, {}),
    e = instance.exports;
  return {
    plan(paths, locale) {
      if (paths.length > 128) return { status: e.plan(paths.length), mounts: [] };
      const names = new Uint8Array(e.memory.buffer, e.files_ptr(), 128 * 256),
        loc = new Uint8Array(e.memory.buffer, e.locale_ptr(), 8);
      names.fill(0);
      loc.fill(0);
      if (!/^[A-Za-z]{4}$/.test(locale)) return { status: 5, mounts: [] };
      loc.set(new TextEncoder().encode(locale));
      paths.forEach((name, i) => {
        if (!/^[\x20-\x7e]{1,255}$/.test(name)) throw Error('unsupported filename');
        names.set(new TextEncoder().encode(name), i * 256);
      });
      const status = e.plan(paths.length),
        mounts = [];
      if (status === 1) {
        const rows = new Uint32Array(e.memory.buffer, e.mounts_ptr(), e.mount_count() * 4);
        for (let i = 0; i < e.mount_count(); i++) {
          const path = paths[rows[i * 4]];
          mounts.push({
            name: path.replaceAll('/', '\\').split('\\').at(-1),
            priority: rows[i * 4 + 1],
            sequence: rows[i * 4 + 2],
            flags: rows[i * 4 + 3],
            path,
          });
        }
      }
      return { status, mounts };
    },
  };
}
export function verifyCases(planner, cases) {
  for (const c of cases) {
    const result = planner.plan(c.paths, c.locale);
    const clean = { ...result, mounts: result.mounts.map(({ path, ...m }) => m) };
    if (JSON.stringify(clean) !== JSON.stringify({ status: c.status, mounts: c.mounts }))
      throw Error('startup case mismatch: ' + c.name);
  }
  if (planner.plan(Array(129).fill('x'), 'esES').status !== 2) throw Error('capacity bound failed');
  return cases.length;
}
