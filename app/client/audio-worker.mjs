import { readLocalSource } from '../../runtime/io/local_source.mjs';
import { listen, send } from '../../engine/workers/environment.mjs';
import { makePlanner } from '../../engine/archive-startup/planner.mjs';
import { openArchive } from '../../runtime/io/member_reader.mjs';
import { readRawExtent } from '../../engine/audio/read_raw.mjs';
import { readLargeSectorMember } from '../../engine/audio/read_sector.mjs';
let files, ordered, lookupWasm, decoderWasm;
const archives = new Map();
async function source(row) {
  return {
    size: row.size ?? row.file.size,
    read: (offset, count) => readLocalSource(row, offset, count),
  };
}
async function openMount(mount) {
  const key = mount.path.toLowerCase();
  let opened = archives.get(key);
  if (opened) return opened;
  const row = files.get(key);
  if (!row) throw Error('Missing audio archive: ' + mount.path);
  const input = await source(row);
  const archive = await openArchive({
    ...input,
    lookupWasm,
    decoderWasm,
  });
  opened = { archive, input };
  archives.set(key, opened);
  return opened;
}
let chain = Promise.resolve();
listen((data) => {
  chain = chain.then(async () => {
    try {
      let result;
      if (data.kind === 'init') {
        files = new Map(data.files.map((row) => [row.path.toLowerCase(), row]));
        ({ lookupWasm, decoderWasm } = data);
        const plan = (await makePlanner(data.startup)).plan(
          data.files.map((row) => row.path),
          data.locale || 'esES',
        );
        if (plan.status !== 1) throw Error('Audio mount planning failed');
        const e = (await WebAssembly.instantiate(data.orderWasm, {})).instance.exports;
        new Uint32Array(e.memory.buffer, e.rows_ptr(), plan.mounts.length * 3).set(
          plan.mounts.flatMap((row) => [row.priority, row.sequence, 0]),
        );
        if (e.rank_candidates(plan.mounts.length) !== 1) throw Error('Audio mount order failed');
        ordered = Array.from(
          new Uint32Array(e.memory.buffer, e.order_ptr(), plan.mounts.length),
          (i) => plan.mounts[i],
        );
        result = { mounts: ordered.length };
      } else if (data.kind === 'bytes') {
        let found;
        const requested = data.archive
            ? ordered.find((mount) => mount.path.toLowerCase() === data.archive.toLowerCase())
            : null,
          candidates = data.archive ? (requested ? [requested] : []) : ordered;
        if (data.archive && !requested) throw Error('Audio archive not admitted: ' + data.archive);
        for (const mount of candidates) {
          const { archive, input } = await openMount(mount);
          const member = archive.lookup(data.path);
          if (!member) continue;
          const raw =
            member.flags === 0x80000000
              ? await readRawExtent(input, member)
              : await readLargeSectorMember(input, member, decoderWasm);
          found = { bytes: raw.bytes, archive: mount.path, read_operations: raw.reads };
          break;
        }
        if (!found) throw Error('Audio member missing: ' + data.path);
        result = found;
      } else throw Error('Unknown audio operation');
      send({ id: data.id, result });
    } catch (error) {
      send({ id: data.id, error: error.message });
    }
  });
});
