// Bounded import of decoded server records. C owns categories, sorting and selection.
export function importRealmList(api, realms) {
  if (!Array.isArray(realms) || realms.length > 256 || api.e.glue_realm_begin())
    throw Error('Realm list import rejected');
  const encoder = new TextEncoder();
  for (const row of realms) {
    const name = encoder.encode(row.name),
      address = encoder.encode(row.address);
    if (name.length > 255 || address.length > 255) throw Error('Realm text exceeds limit');
    const bytes = new Uint8Array(46 + name.length + address.length),
      view = new DataView(bytes.buffer);
    for (const [index, field] of [
      'id',
      'type',
      'flags',
      'locked',
      'characters',
      'timezone',
      'major',
      'minor',
      'revision',
      'build',
    ].entries())
      view.setUint32(index * 4, Number(row[field] || 0), true);
    view.setFloat32(40, row.population, true);
    bytes.set(name, 44);
    bytes.set(address, 45 + name.length);
    api.input(bytes);
    if (api.e.glue_realm_add(bytes.length)) throw Error('Realm record rejected');
  }
  if (api.e.glue_realm_finalize()) throw Error('Realm list finalization failed');
}
