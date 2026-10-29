import { uploadWebGLResources } from '../graphics/webgl_resources.mjs';
// Session-owned immutable texture reuse. Exact byte comparison resolves hash
// collisions; a hash never establishes asset or pixel identity by itself.
export function createUITextureCache(
  gl,
  { budgetBytes = 16777216, upload = uploadWebGLResources } = {},
) {
  if (!Number.isSafeInteger(budgetBytes) || budgetBytes < 1) throw Error('invalid UI cache budget');
  const buckets = new Map(),
    hashes = new WeakMap(),
    groups = new Set();
  let bytes = 0,
    next = 0,
    closed = false;
  const key = (row) => {
    let hash = hashes.get(row.rgba);
    if (hash === undefined) {
      hash = 2166136261;
      for (const value of row.rgba) hash = Math.imul(hash ^ value, 16777619);
      hash >>>= 0;
      hashes.set(row.rgba, hash);
    }
    return row.width + ':' + row.height + ':' + hash;
  };
  const equal = (a, b) => {
    if (a === b) return true;
    if (a.length !== b.length) return false;
    for (let i = 0; i < a.length; i++) if (a[i] !== b[i]) return false;
    return true;
  };
  const drop = (group) => {
    group.pool.dispose();
    groups.delete(group);
    for (const entry of group.entries) {
      const bucket = buckets.get(entry.key),
        at = bucket.indexOf(entry);
      bucket.splice(at, 1);
      if (!bucket.length) buckets.delete(entry.key);
      bytes -= entry.rgba.byteLength;
    }
  };
  const evict = () => {
    for (const group of groups) {
      if (bytes <= budgetBytes) break;
      if (group.entries.every((row) => row.refs === 0)) drop(group);
    }
  };
  return {
    async acquire(rows) {
      if (closed) throw Error('UI texture cache closed');
      const selected = new Map(),
        held = [],
        missing = [];
      try {
        for (const row of rows) {
          if (selected.has(row.name)) throw Error('duplicate UI texture name');
          if (!(row.rgba instanceof Uint8Array) || row.rgba.length !== row.width * row.height * 4)
            throw Error('invalid UI texture bytes');
          const signature = key(row);
          let entry = (buckets.get(signature) || []).find((e) => equal(e.rgba, row.rgba));
          if (!entry) entry = missing.find((e) => e.key === signature && equal(e.rgba, row.rgba));
          if (!entry) {
            entry = {
              key: signature,
              width: row.width,
              height: row.height,
              rgba: row.rgba,
              refs: 0,
              name: 'ui-cache-' + next++,
            };
            missing.push(entry);
          }
          entry.refs++;
          held.push(entry);
          selected.set(row.name, entry);
        }
        if (missing.length) {
          const pool = await upload(gl, {
            textures: missing.map(({ name, width, height, rgba }) => ({
              name,
              width,
              height,
              rgba,
            })),
            buffers: [],
          });
          const group = { pool, entries: missing };
          if (closed) {
            pool.dispose();
            throw Error('UI texture cache closed during upload');
          }
          groups.add(group);
          for (const entry of missing) {
            entry.group = group;
            let bucket = buckets.get(entry.key);
            if (!bucket) {
              bucket = [];
              buckets.set(entry.key, bucket);
            }
            bucket.push(entry);
            bytes += entry.rgba.byteLength;
          }
        }
        let released = false;
        evict();
        return {
          texture(name) {
            if (released || closed) throw Error('released UI texture lease');
            const entry = selected.get(name);
            return entry?.group.pool.texture(entry.name);
          },
          dispose() {
            if (released) return;
            released = true;
            for (const entry of held) entry.refs--;
            if (!closed) evict();
          },
        };
      } catch (error) {
        for (const entry of held) entry.refs--;
        if (!closed) evict();
        throw error;
      }
    },
    metrics() {
      return { bytes, groups: groups.size };
    },
    dispose() {
      if (closed) return;
      closed = true;
      for (const group of [...groups]) drop(group);
    },
  };
}
