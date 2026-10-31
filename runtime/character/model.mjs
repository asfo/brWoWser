import { instantiate } from '../wasm/instantiate.mjs';
import { createBonePoseRuntime } from '../scene/bones.mjs';
import { createUVTranslationRuntime } from '../scene/uv.mjs';
import { composeCharacterSkin } from './catalog.mjs';
import { createParticleRuntime } from '../particle_runtime/runtime.mjs';

const bytesOf = (value) => (value instanceof Uint8Array ? value : new Uint8Array(value));
function binary(bytes, magic, minimum, maximum) {
  bytes = bytesOf(bytes);
  if (bytes.length < minimum || bytes.length > maximum) throw Error('Character model capacity');
  const view = new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength);
  if (view.getUint32(0, true) !== magic) throw Error('Unsupported character model signature');
  const span = (offset, count, stride) => {
    if (
      !Number.isInteger(offset) ||
      !Number.isInteger(count) ||
      offset < 0 ||
      count < 0 ||
      offset > bytes.length ||
      count > (bytes.length - offset) / stride
    )
      throw Error('Character model table outside input');
  };
  return { bytes, view, span };
}
async function moduleExports(wasm) {
  if (!wasm) throw Error('Character decoder module unavailable');
  return (await instantiate(wasm)).instance.exports;
}
function copyIn(e, pointer, bytes) {
  if (pointer > e.memory.buffer.byteLength || bytes.length > e.memory.buffer.byteLength - pointer)
    throw Error('Character decoder input capacity');
  new Uint8Array(e.memory.buffer, pointer, bytes.length).set(bytes);
}
function rebindTextures(model, names) {
  const source = new DataView(model.buffer, model.byteOffset, model.byteLength);
  const strings = names.map((name) => new TextEncoder().encode(name + '\0'));
  const total = model.length + strings.reduce((n, bytes) => n + bytes.length, 0);
  if (total > 8388608) throw Error('Character material input capacity');
  const output = new Uint8Array(total);
  output.set(model);
  const view = new DataView(output.buffer);
  let offset = model.length;
  for (let i = 0; i < names.length; i++) {
    const at = source.getUint32(0x54, true) + i * 16;
    view.setUint32(at, 0, true);
    view.setUint32(at + 8, strings[i].length, true);
    view.setUint32(at + 12, offset, true);
    output.set(strings[i], offset);
    offset += strings[i].length;
  }
  return output;
}

// Collapse the chosen local animation's outer arrays for the already bounded C
// pose decoder. Source bytes remain immutable, and external .anim offsets are
// relocated only into this private in-memory working copy.
async function idleModel(source, path, readAsset, heldAttachments = [], animationId = 0) {
  const { view, span } = binary(source, 0x3032444d, 0x138, 8388608);
  const count = view.getUint32(28, true),
    at = view.getUint32(32, true);
  span(at, count, 64);
  let index = -1;
  let handIndex = -1;
  for (let i = 0; i < count; i++) {
    if (view.getUint16(at + i * 64, true) === 15 && handIndex < 0) handIndex = i;
    if (view.getUint16(at + i * 64, true) === animationId) {
      if (index < 0) index = i;
    }
  }
  if (index < 0) throw Error('Character animation unavailable: ' + animationId);
  const sequence = at + index * 64;
  const flags = view.getUint32(sequence + 12, true);
  const external = !(flags & 0x20);
  const animation = external
    ? bytesOf(
        await readAsset(
          path.replace(
            /\.m2$/i,
            `${String(view.getUint16(sequence, true)).padStart(4, '0')}-${String(view.getUint16(sequence + 2, true)).padStart(2, '0')}.anim`,
          ),
        ),
      )
    : null;
  const handSequence = handIndex < 0 ? -1 : at + handIndex * 64;
  const handAnimation =
    heldAttachments.length && handSequence >= 0 && !(view.getUint32(handSequence + 12, true) & 0x20)
      ? bytesOf(
          await readAsset(
            path.replace(
              /\.m2$/i,
              `0015-${String(view.getUint16(handSequence + 2, true)).padStart(2, '0')}.anim`,
            ),
          ),
        )
      : null;
  const handOffset = source.length + (animation?.length || 0);
  const output = new Uint8Array(handOffset + (handAnimation?.length || 0));
  if (output.length > 8388608) throw Error('Character animation working-copy capacity');
  output.set(source);
  if (animation) output.set(animation, source.length);
  if (handAnimation) output.set(handAnimation, handOffset);
  const result = new DataView(output.buffer),
    bones = view.getUint32(44, true),
    boneAt = view.getUint32(48, true);
  // The pose decoder consumes a single sequence, so relocate its header as
  // well as the bone tracks. Player files contain many unrelated animations.
  result.setUint32(28, 1, true);
  result.setUint32(32, sequence, true);
  span(boneAt, bones, 88);
  const fingerRoots = new Set();
  const keyCount = view.getUint32(52, true),
    keyAt = view.getUint32(56, true);
  span(keyAt, keyCount, 2);
  for (const hand of heldAttachments) {
    const first = hand === 1 ? 8 : hand === 2 ? 13 : -1;
    if (first < 0) continue;
    for (let key = first; key < first + 5 && key < keyCount; key++) {
      const bone = view.getUint16(keyAt + key * 2, true);
      if (bone < bones) fingerRoots.add(bone);
    }
  }
  const gripping = (bone) => {
    for (let steps = 0; steps < bones && bone < bones; steps++) {
      if (fingerRoots.has(bone)) return true;
      bone = view.getUint16(boneAt + bone * 88 + 8, true);
    }
    return false;
  };
  for (let bone = 0; bone < bones; bone++)
    for (const delta of [16, 36, 56]) {
      const track = boneAt + bone * 88 + delta;
      const global = view.getUint16(track + 2, true) !== 0xffff;
      for (const outer of [4, 12]) {
        const n = view.getUint32(track + outer, true),
          offset = view.getUint32(track + outer + 4, true);
        if (!n) continue;
        span(offset, n, 8);
        const hand = !global && handIndex >= 0 && gripping(bone);
        const chosen = global ? 0 : hand ? handIndex : index;
        if (chosen >= n) throw Error('Character animation track index unavailable');
        const inner = offset + chosen * 8;
        result.setUint32(track + outer, 1, true);
        result.setUint32(track + outer + 4, inner, true);
        const keys = view.getUint32(inner, true),
          data = view.getUint32(inner + 4, true);
        const externalKeys = hand ? handAnimation : animation;
        if (externalKeys && !global && keys) {
          const stride = outer === 4 ? 4 : delta === 36 ? 8 : 12;
          if (data > externalKeys.length || keys > (externalKeys.length - data) / stride)
            throw Error('Character external animation key bounds');
          result.setUint32(inner + 4, (hand ? handOffset : source.length) + data, true);
        }
      }
    }
  return { model: output, duration: view.getUint32(sequence + 4, true) };
}

export async function loadCharacterModel({
  readAsset,
  decodeTexture,
  appearance,
  catalog,
  modules,
  prepareWasm,
  backgroundPath,
  attachmentSpec,
  worldPropPath,
  isCurrent = () => true,
  onIssue = () => {},
}) {
  const checkpoint = () => {
    if (!isCurrent()) throw Error('Character load superseded');
  };
  const metadata = worldPropPath
    ? {
        modelPath: worldPropPath,
        skinPath: worldPropPath.replace(/\.m2$/i, '00.skin'),
        isWorldProp: true,
        equipment: [],
        attachments: [],
        geosets: new Map(),
      }
    : attachmentSpec
      ? {
          ...attachmentSpec,
          isAttachment: true,
          equipment: [],
          attachments: [],
          geosets: new Map(),
        }
      : backgroundPath
        ? {
            modelPath: backgroundPath,
            skinPath: backgroundPath.replace(/\.m2$/i, '00.skin'),
            geosets: new Map(),
            equipment: [],
            isBackground: true,
          }
        : await catalog.resolve(appearance);
  checkpoint();
  const [source, skinBytes] = await Promise.all([
    readAsset(metadata.modelPath),
    readAsset(metadata.skinPath),
  ]);
  const model = bytesOf(source),
    skin = bytesOf(skinBytes);
  checkpoint();
  const m = binary(model, 0x3032444d, 0x138, 8388608);
  const s = binary(skin, 0x4e494b53, 48, 262144);
  if (m.view.getUint32(4, true) !== 264) throw Error('Unsupported character M2 version');
  const prepare = await moduleExports(prepareWasm);
  copyIn(prepare, prepare.model_ptr(), model);
  copyIn(prepare, prepare.skin_ptr(), skin);
  if (prepare.prepare_records(model.length, skin.length))
    throw Error('Character vertex preparation rejected');
  const vertexCount = new Uint32Array(prepare.memory.buffer, prepare.info_ptr(), 4)[0];
  const prepared = new Uint8Array(
    prepare.memory.buffer,
    prepare.output_ptr(),
    vertexCount * 48,
  ).slice();
  const mesh = await moduleExports(modules.mesh);
  copyIn(mesh, mesh.skin_ptr(), skin);
  if (mesh.decode_mesh(skin.length, vertexCount)) throw Error('Character topology rejected');
  const meshInfo = new Uint32Array(mesh.memory.buffer, mesh.info_ptr(), 3);
  const indices = new Uint32Array(mesh.memory.buffer, mesh.index_ptr(), meshInfo[0]).slice();
  const rawRanges = new Uint32Array(mesh.memory.buffer, mesh.range_ptr(), meshInfo[1] * 4).slice();
  const subsetAt = s.view.getUint32(32, true);
  s.span(subsetAt, meshInfo[1], 48);
  const ranges = Array.from({ length: meshInfo[1] }, (_, i) => ({
    geoset: s.view.getUint16(subsetAt + i * 48, true),
    start: rawRanges[i * 4 + 2],
    count: rawRanges[i * 4 + 3],
  }));
  function visible(id) {
    if (metadata.isBackground || metadata.isAttachment || metadata.isWorldProp) return true;
    if (id === 0) return true;
    if (id < 100) return id === metadata.hairGeoset || (id === 1 && metadata.bald);
    const group = Math.floor(id / 100),
      variant = id % 100;
    // Variant 1 is the unequipped body, even when that group has no mesh
    // for it. Choosing the lowest existing variant enables optional sleeves,
    // tabards, trouser flares and glowing eyes on an ordinary bare body.
    const fallback = group > 3 ? 1 : 0;
    return variant === (metadata.geosets.get(group) ?? fallback);
  }
  const textureCount = m.view.getUint32(0x50, true),
    textureAt = m.view.getUint32(0x54, true);
  m.span(textureAt, textureCount, 16);
  if (!textureCount || textureCount > 256) throw Error('Character texture capacity');
  const batchCount = s.view.getUint32(36, true),
    batchAt = s.view.getUint32(40, true),
    lookupCount = m.view.getUint32(0x80, true),
    lookupAt = m.view.getUint32(0x84, true),
    used = new Set();
  s.span(batchAt, batchCount, 24);
  m.span(lookupAt, lookupCount, 2);
  for (let i = 0; i < batchCount; i++) {
    const at = batchAt + i * 24,
      subset = s.view.getUint16(at + 4, true),
      count = s.view.getUint16(at + 14, true),
      start = s.view.getUint16(at + 16, true);
    if (subset >= ranges.length || start > lookupCount || count > lookupCount - start)
      throw Error('Character batch texture reference rejected');
    if (visible(ranges[subset].geoset))
      for (let j = 0; j < count; j++) used.add(m.view.getUint16(lookupAt + (start + j) * 2, true));
  }
  const emitterCount =
    metadata.isBackground || metadata.isWorldProp ? m.view.getUint32(0x128, true) : 0;
  const lightCount =
      metadata.isBackground || metadata.isWorldProp ? m.view.getUint32(0x108, true) : 0,
    lightOffset = metadata.isBackground || metadata.isWorldProp ? m.view.getUint32(0x10c, true) : 0;
  if (lightCount > 64) throw Error('Character background light capacity');
  m.span(lightOffset, lightCount, 156);
  const hasPointLights = Array.from({ length: lightCount }, (_, i) =>
    m.view.getUint16(lightOffset + i * 156, true),
  ).includes(1);
  if (emitterCount) {
    const emitterAt = m.view.getUint32(0x12c, true);
    if (emitterCount > 64) throw Error('Character background emitter capacity');
    m.span(emitterAt, emitterCount, 476);
    for (let i = 0; i < emitterCount; i++) {
      const texture = m.view.getUint16(emitterAt + i * 476 + 22, true);
      if (texture >= textureCount) throw Error('Character background particle texture reference');
      used.add(texture);
    }
  }
  const textureFlags = Array.from({ length: textureCount }, (_, i) =>
    m.view.getUint32(textureAt + i * 16 + 4, true),
  );
  const textures = [],
    names = [];
  const replacements = new Map();
  const requiredKinds = new Set([...used].map((i) => m.view.getUint32(textureAt + i * 16, true)));
  if (metadata.isAttachment) {
    if (requiredKinds.has(2) && metadata.texture)
      replacements.set(2, await decodeTexture(metadata.texture));
  } else if (!metadata.isBackground && !metadata.isWorldProp) {
    replacements.set(
      1,
      await composeCharacterSkin(
        metadata.textureLayers,
        decodeTexture,
        prepare,
        onIssue,
        checkpoint,
      ),
    );
    if (metadata.cloakTexture) replacements.set(2, await decodeTexture(metadata.cloakTexture));
    for (const [slot, path] of [
      [6, metadata.hairTexture],
      [8, metadata.extraSkinTexture],
    ])
      if (requiredKinds.has(slot)) replacements.set(slot, await decodeTexture(path));
  }
  for (let i = 0; i < textureCount; i++) {
    checkpoint();
    const at = textureAt + i * 16,
      kind = m.view.getUint32(at, true);
    let texture, name;
    if (!used.has(i)) {
      textures.push(null);
      names.push('character-unused-' + i);
      continue;
    }
    if (!kind) {
      const length = m.view.getUint32(at + 8, true),
        offset = m.view.getUint32(at + 12, true);
      m.span(offset, length, 1);
      if (length < 2 || length > 512 || model[offset + length - 1])
        throw Error('Character texture path');
      name = new TextDecoder().decode(model.subarray(offset, offset + length - 1));
      texture = await decodeTexture(name);
    } else {
      texture = replacements.get(kind);
      if (!texture) throw Error('Character replaceable texture slot is not implemented: ' + kind);
      name = 'character-slot-' + kind;
    }
    textures.push(texture);
    names.push(name);
  }
  const materialModel = rebindTextures(model, names);
  checkpoint();
  const material = await moduleExports(modules.materials);
  copyIn(material, material.model_ptr(), materialModel);
  copyIn(material, material.skin_ptr(), skin);
  if (material.decode_materials(materialModel.length, skin.length))
    throw Error('Character material decoder rejected');
  const info = new Uint32Array(material.memory.buffer, material.info_ptr(), 3);
  const rows = new Uint32Array(material.memory.buffer, material.rows_ptr(), info[0] * 9).slice();
  const stages = new Uint32Array(
    material.memory.buffer,
    material.stages_ptr(),
    info[1] * 3,
  ).slice();
  const view = new DataView(prepared.buffer);
  const positions = new Float32Array(vertexCount * 3),
    normals = new Float32Array(vertexCount * 3),
    uv = new Float32Array(vertexCount * 4);
  const bounds = { min: [Infinity, Infinity, Infinity], max: [-Infinity, -Infinity, -Infinity] };
  for (let i = 0; i < vertexCount; i++) {
    for (let j = 0; j < 3; j++) {
      const value = view.getFloat32(i * 48 + j * 4, true);
      if (!Number.isFinite(value)) throw Error('Character vertex is not finite');
      positions[i * 3 + j] = value;
      normals[i * 3 + j] = view.getFloat32(i * 48 + 20 + j * 4, true);
      bounds.min[j] = Math.min(bounds.min[j], value);
      bounds.max[j] = Math.max(bounds.max[j], value);
    }
    for (let j = 0; j < 4; j++) uv[i * 4 + j] = view.getFloat32(i * 48 + 32 + j * 4, true);
  }
  let pose,
    duration = 0;
  let camera = null;
  if (metadata.isBackground) {
    const decoder = await moduleExports(modules.camera);
    copyIn(decoder, decoder.input_ptr(), model);
    const cameraStatus = decoder.decode_camera_zero(model.length);
    if (cameraStatus)
      throw Error(
        `Character background camera decoder rejected (${cameraStatus}): ${metadata.modelPath}`,
      );
    camera = Array.from(new Float32Array(decoder.memory.buffer, decoder.values_ptr(), 10));
  }
  let uvAnimation = null;
  if ((metadata.isBackground || metadata.isWorldProp) && modules.uv) {
    try {
      const runtime = await createUVTranslationRuntime(modules.uv, model);
      if (runtime.sampleScroll(0)) uvAnimation = runtime;
    } catch (error) {
      onIssue('Background cloud scroll unavailable: ' + error.message);
    }
  }
  try {
    const idle = await idleModel(
      model,
      metadata.modelPath,
      readAsset,
      (metadata.attachments || []).map((item) => item.gripHand ?? item.attachmentId),
    );
    pose = await createBonePoseRuntime(modules.bones, idle.model, prepared);
    // The loading pass has no camera yet; billboard vertices are expected to
    // become valid when the renderer supplies its actual view.
    pose.sample(0, 0);
    duration = idle.duration;
  } catch (error) {
    pose = null;
    if (!metadata.isAttachment)
      onIssue('Character animation unavailable; showing original bind pose: ' + error.message);
  }
  let particles = null;
  let particleStatus = { state: emitterCount ? 'loading' : 'absent', emitters: emitterCount };
  if (emitterCount) {
    try {
      if (!pose || !duration || !modules.particles || !modules.particleTable)
        throw Error('Particle pose, clock or decoder unavailable');
      particles = await createParticleRuntime({
        wasm: modules.particles,
        model,
        randomTable: new Uint8Array(modules.particleTable),
        boneCount: m.view.getUint32(44, true),
        sequenceDurationMs: duration,
      });
      particleStatus = { state: 'ready', emitters: emitterCount };
    } catch (error) {
      particleStatus = { state: 'unavailable', emitters: emitterCount, error: error.message };
      onIssue('Character background particles unavailable: ' + error.message);
    }
  }
  let cachedTime = -1,
    cachedView,
    cachedPose,
    warnedUnsupportedPose = false;
  let runPose,
    moving = 0,
    motionStart = 0;
  const movementClips = new Map();
  if (appearance?.worldPreview && pose) {
    for (const animation of [5, 11, 12, 13]) {
      try {
        const clip = await idleModel(
          model,
          metadata.modelPath,
          readAsset,
          (metadata.attachments || []).map((item) => item.gripHand ?? item.attachmentId),
          animation,
        );
        movementClips.set(animation, clip);
        if (!runPose) runPose = await createBonePoseRuntime(modules.bones, clip.model, prepared);
      } catch (error) {
        onIssue('Movement animation ' + animation + ': ' + error.message);
      }
    }
  }
  function samplePose(elapsedMs, view) {
    const ms = Math.floor(elapsedMs);
    if (ms !== cachedTime || (view && (!cachedView || view.some((v, i) => v !== cachedView[i])))) {
      const runtime = moving && runPose ? runPose : pose;
      const clip =
        movementClips.get(moving) || movementClips.get(5) || movementClips.values().next().value;
      const clock = moving && runPose ? clip.duration : duration;
      const sequenceMs = moving ? Math.max(0, ms - motionStart) : ms;
      cachedPose = runtime?.sample(clock ? sequenceMs % clock : 0, ms, view);
      if (view && cachedPose && !warnedUnsupportedPose && cachedPose.vertexValid.some((v) => !v)) {
        warnedUnsupportedPose = true;
        onIssue(
          'Some vertices still require unsupported bone behavior after the camera was supplied',
        );
      }
      if (cachedPose)
        for (let i = 0; i < cachedPose.vertexValid.length; i++)
          if (!cachedPose.vertexValid[i]) {
            cachedPose.positions.set(positions.subarray(i * 3, i * 3 + 3), i * 3);
            cachedPose.normals?.set(normals.subarray(i * 3, i * 3 + 3), i * 3);
          }
      cachedTime = ms;
      cachedView = view?.slice();
    }
    return cachedPose;
  }
  return {
    localView(view, placement) {
      if (!metadata.isWorldProp) return view;
      new Float32Array(prepare.memory.buffer, prepare.character_instance_input(), 32).set([
        ...view,
        ...placement,
      ]);
      if (prepare.character_instance_matrix()) throw Error('World instance view rejected');
      return new Float32Array(
        prepare.memory.buffer,
        prepare.character_instance_output(),
        16,
      ).slice();
    },
    motion(value, elapsedMs = 0) {
      const animation = typeof value === 'boolean' ? (value ? 5 : 0) : value;
      if (![0, 5, 11, 12, 13].includes(animation)) throw Error('Unsupported locomotion animation');
      if (moving !== animation) {
        moving = animation;
        motionStart = Math.floor(elapsedMs);
        const clip =
          movementClips.get(moving) || movementClips.get(5) || movementClips.values().next().value;
        if (moving && runPose && clip) runPose.setModel(clip.model);
        cachedTime = -1;
      }
    },
    metadata,
    camera,
    placeAttachment(id, elapsedMs, parent, facing) {
      const attachment = this.attachment(id, elapsedMs);
      const input = new Float32Array(
        prepare.memory.buffer,
        prepare.character_placement_input(),
        33,
      );
      input.set(parent);
      input.set(attachment, 16);
      input[32] = facing;
      if (prepare.character_placement()) throw Error('Held item placement rejected');
      return new Float32Array(
        prepare.memory.buffer,
        prepare.character_placement_output(),
        16,
      ).slice();
    },
    pointLights(elapsedMs) {
      if (!hasPointLights) return [];
      const palette = samplePose(elapsedMs),
        ms = Math.floor(elapsedMs);
      if (palette) {
        new Float32Array(
          prepare.memory.buffer,
          prepare.character_light_bones(),
          palette.matrices.length,
        ).set(palette.matrices);
        const valid = new Uint8Array(prepare.memory.buffer, prepare.character_light_valid(), 512);
        valid.fill(0);
        valid.set(palette.boneValid);
      }
      const status = prepare.character_lights(model.length, duration ? ms % duration : ms, ms);
      if (status) throw Error('Character model lights rejected: ' + status);
      const values = new Float32Array(
        prepare.memory.buffer,
        prepare.character_light_output(),
        prepare.character_light_count() * 12,
      );
      return Array.from({ length: values.length / 12 }, (_, i) =>
        values.slice(i * 12, i * 12 + 12),
      );
    },
    attachment(id, elapsedMs) {
      const count = m.view.getUint32(0xf0, true),
        offset = m.view.getUint32(0xf4, true);
      m.span(offset, count, 40);
      const lookupCount = m.view.getUint32(0xf8, true),
        lookupOffset = m.view.getUint32(0xfc, true);
      m.span(lookupOffset, lookupCount, 2);
      if (id >= lookupCount) throw Error('Character scene attachment unavailable');
      const index = m.view.getUint16(lookupOffset + id * 2, true);
      if (index >= count) throw Error('Character scene attachment lookup rejected');
      const at = offset + index * 40,
        bone = m.view.getUint16(at + 4, true);
      const palette = samplePose(elapsedMs);
      if (!palette?.boneValid[bone]) throw Error('Character scene attachment bone unavailable');
      const input = new Float32Array(
        prepare.memory.buffer,
        prepare.character_attachment_input(),
        19,
      );
      input.set(palette.matrices.subarray(bone * 16, bone * 16 + 16));
      for (let i = 0; i < 3; i++) input[16 + i] = m.view.getFloat32(at + 8 + i * 4, true);
      if (prepare.character_attachment())
        throw Error('Character scene attachment transform rejected');
      return new Float32Array(
        prepare.memory.buffer,
        prepare.character_attachment_output(),
        16,
      ).slice();
    },
    view(aspect, authoredCamera = camera, diagonalFraming = false) {
      const values = new Float32Array(16);
      if (authoredCamera) values.set(authoredCamera, 0);
      values.set(bounds.min, 10);
      values.set(bounds.max, 13);
      new Float32Array(prepare.memory.buffer, prepare.character_view_input(), 16).set(values);
      const status = prepare.character_view(authoredCamera ? (diagonalFraming ? 2 : 1) : 0, aspect);
      if (status) throw Error('Character camera rejected: ' + status);
      const output = new Float32Array(
        prepare.memory.buffer,
        prepare.character_view_output(),
        67,
      ).slice();
      return {
        view: output.subarray(0, 16),
        projection: output.subarray(16, 32),
        transform: output.subarray(32, 48),
        particleView: output.subarray(48, 64),
        eye: output.subarray(64, 67),
      };
    },
    positions,
    normals,
    uv,
    bounds,
    indices,
    ranges,
    rows,
    stages,
    appearance(elapsedMs) {
      const ms = Math.floor(elapsedMs);
      const status = material.sample_appearance(
        materialModel.length,
        skin.length,
        duration ? ms % duration : ms,
        ms,
      );
      if (status) throw Error('Character material animation rejected: ' + status);
      return new Float32Array(
        material.memory.buffer,
        material.appearance_ptr(),
        (rows.length / 9) * 4,
      );
    },
    drawOrder(eye) {
      if (material.sort_batches(skin.length, ...eye)) throw Error('Character draw order rejected');
      return new Uint32Array(material.memory.buffer, material.order_ptr(), rows.length / 9);
    },
    textures,
    textureFlags,
    visible,
    animated: !!pose,
    uvAnimated: !!uvAnimation,
    uvMatrices(elapsedMs) {
      const ms = Math.floor(elapsedMs);
      if (!uvAnimation || !Number.isInteger(ms) || ms < 0 || ms > 0xffffffff) return null;
      return uvAnimation.sampleScroll(ms);
    },
    sample(elapsedMs, view) {
      return samplePose(elapsedMs, view)?.positions || positions;
    },
    animatedNormals() {
      return cachedPose?.normals || normals;
    },
    get particleStatus() {
      return { ...particleStatus };
    },
    sampleParticles(elapsedMs, view) {
      if (!particles) return null;
      const palette = samplePose(elapsedMs);
      try {
        const sample = particles.sample({
          sequenceMs: Math.floor(elapsedMs) % duration,
          globalMs: Math.floor(elapsedMs),
          matrices: palette.matrices,
          boneValid: palette.boneValid,
          view,
        });
        particleStatus = { state: 'running', ...sample.stats };
        return sample;
      } catch (error) {
        particles = null;
        particleStatus = { state: 'stopped', emitters: emitterCount, error: error.message };
        onIssue('Character background particles stopped: ' + error.message);
        return null;
      }
    },
  };
}
