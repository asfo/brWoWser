// Original selected programmable boundary: 0x873635..0x87367f.
// The admitted asset has translation-only matrices: UV' = UV + matrix[12:14].
export const uvVertexExpression = 'texcoord=(uvset==0?coords.xy:coords.zw)+uvTranslation;';
export function stageTranslation(rows, stages, batch, matrices, stageOffset = 0) {
  if (!Number.isInteger(stageOffset) || stageOffset < 0 || stageOffset >= rows[batch * 9 + 7])
    throw Error('UV stage outside batch');
  const stage = rows[batch * 9 + 6] + stageOffset,
    index = stages[stage * 3 + 2];
  if (index === 65535) return [0, 0];
  if (!Number.isInteger(index) || index < 0 || index * 16 + 15 >= matrices.length)
    throw Error('UV transform outside evaluated palette');
  const x = matrices[index * 16 + 12],
    y = matrices[index * 16 + 13];
  if (!Number.isFinite(x) || !Number.isFinite(y)) throw Error('nonfinite UV translation');
  return [x, y];
}
