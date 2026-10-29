import { createParticleRuntime } from '../particle_runtime/runtime.mjs';
import { createBonePoseRuntime } from './bones.mjs';
import { createBillboard64Runtime } from './billboards.mjs';
import { createOpacityRuntime } from './opacity.mjs';
import { createCameraPositionSampler } from './camera_animation.mjs';
import { createUVTranslationRuntime } from './uv.mjs';
import { createPassRuntime } from './pass_selector.mjs';
const normalize = (path) => path.toLowerCase().replaceAll('\\', '/');
function worldFor(camera, offset) {
  const eye = camera.slice(3, 6).map((value, i) => value + offset[i]),
    target = camera.slice(6, 9),
    norm = (v) => {
      const n = Math.hypot(...v);
      if (!n) throw Error('degenerate camera');
      return v.map((x) => x / n);
    },
    cross = (a, b) => [
      a[1] * b[2] - a[2] * b[1],
      a[2] * b[0] - a[0] * b[2],
      a[0] * b[1] - a[1] * b[0],
    ],
    dot = (a, b) => a.reduce((sum, x, i) => sum + x * b[i], 0),
    f = norm(target.map((v, i) => v - eye[i])),
    r0 = norm(cross(f, [0, 0, 1])),
    u0 = cross(r0, f),
    c = Math.cos(camera[9]),
    s = Math.sin(camera[9]),
    r = r0.map((v, i) => v * c + u0[i] * s),
    u = u0.map((v, i) => v * c - r0[i] * s);
  return [
    r[0],
    u[0],
    f[0],
    0,
    r[1],
    u[1],
    f[1],
    0,
    r[2],
    u[2],
    f[2],
    0,
    -dot(eye, r),
    -dot(eye, u),
    -dot(eye, f),
    1,
  ];
}
export async function createScene({ model, skin, prepared, texturePixels, modules }) {
  if (
    !(model instanceof Uint8Array) ||
    !(skin instanceof Uint8Array) ||
    !(prepared instanceof Uint8Array) ||
    prepared.length % 48
  )
    throw Error('invalid scene resource payload');
  const instantiate = async (key) =>
    (await WebAssembly.instantiate(modules[key], {})).instance.exports;
  const material = await instantiate('materials');
  new Uint8Array(material.memory.buffer, material.model_ptr(), model.length).set(model);
  new Uint8Array(material.memory.buffer, material.skin_ptr(), skin.length).set(skin);
  if (material.decode_materials(model.length, skin.length))
    throw Error('scene material profile rejected');
  const counts = [...new Uint32Array(material.memory.buffer, material.info_ptr(), 3)],
    rows = [...new Uint32Array(material.memory.buffer, material.rows_ptr(), counts[0] * 9)],
    stages = [...new Uint32Array(material.memory.buffer, material.stages_ptr(), counts[1] * 3)];
  const passes = await createPassRuntime(modules.passes, model, skin);
  if (passes.resolved.length !== counts[0] * 4) throw Error('scene pass count mismatch');
  const mesh = await instantiate('mesh');
  new Uint8Array(mesh.memory.buffer, mesh.skin_ptr(), skin.length).set(skin);
  if (mesh.decode_mesh(skin.length, prepared.length / 48)) throw Error('scene topology rejected');
  const info = [...new Uint32Array(mesh.memory.buffer, mesh.info_ptr(), 3)],
    indices = new Uint32Array(mesh.memory.buffer, mesh.index_ptr(), info[0]).slice(),
    rangesRaw = new Uint32Array(mesh.memory.buffer, mesh.range_ptr(), info[1] * 4),
    ranges = Array.from({ length: info[1] }, (_, i) => ({
      vertex_start: rangesRaw[i * 4],
      vertex_count: rangesRaw[i * 4 + 1],
      index_start: rangesRaw[i * 4 + 2],
      index_count: rangesRaw[i * 4 + 3],
    }));
  const camera = await instantiate('camera');
  new Uint8Array(camera.memory.buffer, camera.input_ptr(), model.length).set(model);
  const cameraStatus = camera.decode_camera_zero(model.length);
  if (cameraStatus) throw Error('scene camera profile rejected (' + cameraStatus + ')');
  const cameraValues = [...new Float32Array(camera.memory.buffer, camera.values_ptr(), 10)];
  const md = new DataView(model.buffer, model.byteOffset, model.byteLength),
    sd = new DataView(skin.buffer, skin.byteOffset, skin.byteLength),
    textureOffset = md.getUint32(0x54, true),
    textures = [];
  const available = new Map(texturePixels.map((row) => [normalize(row.name), row]));
  for (let i = 0; i < counts[2]; i++) {
    const at = textureOffset + i * 16,
      length = md.getUint32(at + 8, true),
      offset = md.getUint32(at + 12, true),
      name = new TextDecoder().decode(model.subarray(offset, offset + length - 1)),
      source = available.get(normalize(name));
    if (!source) throw Error('scene texture unavailable: ' + name);
    textures.push({ name: source.name, width: source.width, height: source.height });
  }
  const bones = await createBonePoseRuntime(modules.bones, model, prepared),
    billboards = await createBillboard64Runtime(modules.billboards, model, prepared),
    opacity = await createOpacityRuntime(modules.opacity, model, skin),
    cameraAnimation = await createCameraPositionSampler(modules.cameraAnimation, model),
    uv = await createUVTranslationRuntime(modules.uv, model),
    subsetOffset = sd.getUint32(32, true),
    batchOffset = sd.getUint32(40, true),
    centres = ranges.map((_, i) => ({
      bone: sd.getUint16(subsetOffset + i * 48 + 18, true),
      xyz: [0, 1, 2].map((j) => sd.getFloat32(subsetOffset + i * 48 + 32 + j * 4, true)),
    })),
    priorities = rows
      .filter((_, i) => i % 9 === 0)
      .map((_, i) => sd.getInt8(batchOffset + i * 24 + 1));
  const particles = modules.particles
    ? await createParticleRuntime({
        wasm: modules.particles,
        model,
        randomTable: new Uint8Array(modules.particleTable),
        boneCount: md.getUint32(0x2c, true),
        sequenceDurationMs: md.getUint32(md.getUint32(32, true) + 4, true),
      })
    : null;
  if (md.getUint32(16, true) !== 0x18) throw Error('scene sort profile unsupported');
  function sample(sequenceMs, globalMs = sequenceMs) {
    const pose = bones.sample(sequenceMs, globalMs),
      cameraOffset = cameraAnimation(sequenceMs),
      bill = billboards(worldFor(cameraValues, cameraOffset)),
      positions = new Float32Array(pose.positions),
      distances = new Float64Array(counts[0]),
      eye = cameraValues.slice(3, 6).map((value, i) => value + cameraOffset[i]);
    for (let i = 0; i < pose.vertexValid.length; i++) {
      if (pose.vertexValid[i] + bill.vertexValid[i] !== 1)
        throw Error('scene pose coverage unsupported');
      if (bill.vertexValid[i]) positions.set(bill.positions.subarray(i * 3, i * 3 + 3), i * 3);
    }
    for (let i = 0; i < counts[0]; i++) {
      const centre = centres[rows[i * 9]],
        palette = bill.boneValid[centre.bone] ? bill : pose,
        at = centre.bone * 16;
      if (!palette.boneValid[centre.bone]) continue;
      const matrix = palette.matrices,
        point = [0, 1, 2].map(
          (j) =>
            centre.xyz[0] * matrix[at + j] +
            centre.xyz[1] * matrix[at + 4 + j] +
            centre.xyz[2] * matrix[at + 8 + j] +
            matrix[at + 12 + j],
        );
      distances[i] = point.reduce((sum, value, j) => sum + (value - eye[j]) ** 2, 0);
    }
    return {
      particles: particles?.sample({
        sequenceMs,
        globalMs,
        matrices: pose.matrices,
        boneValid: pose.boneValid,
        view: worldFor(cameraValues, cameraOffset),
      }),
      positions: positions.buffer,
      appearance: opacity.sample(sequenceMs, globalMs % opacity.global10Duration).values.buffer,
      cameraOffset,
      distances: distances.buffer,
      uvMatrices: uv.sample(globalMs).matrices.buffer,
      sequenceMs,
      globalMs,
    };
  }
  const initial = sample(0),
    bounds = { min: [Infinity, Infinity, Infinity], max: [-Infinity, -Infinity, -Infinity] },
    positions = new Float32Array(initial.positions);
  for (let i = 0; i < positions.length; i++) {
    bounds.min[i % 3] = Math.min(bounds.min[i % 3], positions[i]);
    bounds.max[i % 3] = Math.max(bounds.max[i % 3], positions[i]);
  }
  return {
    cameraValues,
    sample,
    payload: {
      ...initial,
      rows,
      stages,
      textures,
      ranges,
      indices: indices.buffer,
      priorities,
      bounds,
      resolvedPasses: passes.resolved.buffer,
    },
    metadata: {
      sequenceDurationMs: md.getUint32(md.getUint32(32, true) + 4, true),
      vertices: prepared.length / 48,
      indices: info[0],
      triangles: info[2],
      subsets: info[1],
      batches: counts[0],
      textureReferences: counts[2],
      particles: !!particles,
      scope:
        'Selected C/WASM pose, billboard, material, camera and UV profiles on supplied verified resources. Explicit clocks; complete native shaders, clocks and pixels remain separate.',
    },
  };
}
