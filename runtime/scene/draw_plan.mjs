import { combinerKind } from './pass_selector.mjs';
// Selected original shader-word rewrite plus ps2 record0 arithmetic only.
export const OPACITY_EPSILON = 0.0001;
export function planDraws(
  rows,
  stages,
  ranges,
  appearance,
  {
    batch = -1,
    mode = 'scene',
    priorities = null,
    distances = null,
    resolvedPasses,
    firstPassOnly = false,
  } = {},
) {
  if (
    rows.length % 9 ||
    stages.length % 3 ||
    appearance.length !== (rows.length / 9) * 6 ||
    !(resolvedPasses instanceof Uint32Array) ||
    resolvedPasses.length !== (rows.length / 9) * 4
  )
    throw Error('invalid selected pass tables');
  const chosen = [],
    skipped = [],
    environmentFallback = [],
    used = new Set();
  for (let i = 0; i < rows.length / 9; i++) {
    if (batch >= 0 && i !== batch) continue;
    const subset = rows[i * 9],
      start = rows[i * 9 + 6],
      count = rows[i * 9 + 7],
      opacity = appearance[i * 6 + 2],
      word = resolvedPasses[i * 4];
    if (
      !ranges[subset] ||
      ![1, 2].includes(count) ||
      resolvedPasses[i * 4 + 1] !== count ||
      start > stages.length / 3 - count ||
      word > 65535
    )
      throw Error('invalid selected pass reference');
    const coord = stages[start * 3 + 1];
    if (!Number.isFinite(opacity) || opacity < 0 || opacity > 1)
      throw Error('invalid batch opacity');
    let reason = null;
    if (mode === 'scene' && opacity < OPACITY_EPSILON) reason = 'opacity';
    else if (!ranges[subset].index_count) reason = 'unsupported-pose';
    else if (coord > 1) reason = 'generated-coordinates';
    else if (firstPassOnly && batch < 0 && used.has(subset)) reason = 'additional-material-pass';
    if (reason) {
      skipped.push({ batch: i, subset, reason });
      continue;
    }
    const generated = !!(word & 0x88),
      single = firstPassOnly || generated;
    let combiner = combinerKind(word, count);
    if (single) combiner = ((word >>> 4) & 7) === 0 ? 0 : ((word >>> 4) & 7) === 1 ? 1 : -1;
    if (combiner < 0) throw Error('unsupported selected texture combiner');
    const secondStage = count === 2 && !single ? 1 : 0,
      coord1 = stages[(start + secondStage) * 3 + 1];
    if (coord1 > 1) throw Error('unresolved generated texture coordinates');
    if (generated) environmentFallback.push(i);
    used.add(subset);
    chosen.push({
      batch: i,
      subset,
      texture: stages[start * 3],
      texture1: stages[(start + secondStage) * 3],
      coord,
      coord1,
      secondStage,
      combiner,
      stageCount: single ? 1 : count,
      environmentComponentOmitted: generated,
      opacity: mode === 'scene' ? opacity : 1,
      tint: mode === 'scene' ? Array.from(appearance.slice(i * 6 + 3, i * 6 + 6)) : [1, 1, 1],
      flags: rows[i * 9 + 2],
      materialMode: rows[i * 9 + 3],
      range: ranges[subset],
    });
  }
  if (priorities && distances)
    chosen.sort((a, b) => {
      const ta = a.materialMode >= 2,
        tb = b.materialMode >= 2;
      if (ta !== tb) return ta ? 1 : -1;
      if (!ta) return a.batch - b.batch;
      return (
        priorities[a.batch] - priorities[b.batch] ||
        distances[b.batch] - distances[a.batch] ||
        a.batch - b.batch
      );
    });
  return {
    chosen,
    skipped,
    environmentFallback,
    policy:
      'Selected ps2 record0 one/two texture arithmetic and admitted extra passes; generated environment components omitted explicitly. Selected model priority/distance keys with stable unresolved ties; native permutation, fog/lighting and pixels unproven.',
  };
}
export function supportedTopology(indices, ranges, vertexValid) {
  const out = [],
    kept = [],
    omitted = [];
  for (let i = 0; i < ranges.length; i++) {
    const r = ranges[i],
      start = out.length;
    let lost = 0;
    for (let j = r.index_start; j < r.index_start + r.index_count; j += 3) {
      const tri = [indices[j], indices[j + 1], indices[j + 2]];
      if (tri.some((k) => k >= vertexValid.length)) throw Error('triangle outside vertex mask');
      if (tri.every((k) => vertexValid[k])) out.push(...tri);
      else lost++;
    }
    kept.push({ ...r, index_start: start, index_count: out.length - start });
    if (lost) omitted.push({ subset: i, triangles: lost });
  }
  return {
    indices: new Uint32Array(out),
    ranges: kept,
    omitted,
    scope:
      'Triangles using unsupported bone flags/ancestors are explicitly omitted, never replaced with unposed geometry.',
  };
}
