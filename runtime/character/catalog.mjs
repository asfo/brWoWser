// Client-owned DBC bytes stay in memory; authored source contains no client tables.
export function parseDBC(bytes, { name = 'DBC', minFields = 1, allowPacked = false } = {}) {
  if (!(bytes instanceof Uint8Array) || bytes.length < 20 || bytes.length > 33554432)
    throw Error(name + ': table capacity');
  const view = new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength);
  const count = view.getUint32(4, true),
    fields = view.getUint32(8, true),
    stride = view.getUint32(12, true),
    strings = 20 + count * stride;
  if (
    view.getUint32(0, true) !== 0x43424457 ||
    count > 200000 ||
    fields < minFields ||
    fields > 256 ||
    (stride !== fields * 4 && !(allowPacked && stride % 4 === 0 && stride < fields * 4)) ||
    strings + view.getUint32(16, true) !== bytes.length
  )
    throw Error(name + ': unsupported WDBC layout');
  const at = (row, column) => {
    if (
      !Number.isInteger(row) ||
      row < 0 ||
      row >= count ||
      !Number.isInteger(column) ||
      column < 0 ||
      column >= stride / 4
    )
      throw Error(name + ': field outside table');
    return 20 + row * stride + column * 4;
  };
  const u = (row, column) => view.getUint32(at(row, column), true);
  return {
    name,
    count,
    fields,
    stride,
    rows: Array.from({ length: count }, (_, i) => i),
    u,
    i: (row, column) => view.getInt32(at(row, column), true),
    f: (row, column) => view.getFloat32(at(row, column), true),
    text(row, column) {
      const offset = u(row, column);
      if (offset >= bytes.length - strings) throw Error(name + ': invalid string offset');
      const start = strings + offset,
        end = bytes.indexOf(0, start);
      if (end < start || end - start > 4096) throw Error(name + ': invalid string');
      return new TextDecoder('utf-8', { fatal: true }).decode(bytes.subarray(start, end));
    },
    byId(id) {
      return this.rows.find((row) => u(row, 0) === id);
    },
  };
}

const modelPath = (path) => path.replace(/\.mdx$/i, '.m2');
const texturePath = (path) => path && (/\.blp$/i.test(path) ? path : path + '.blp');
export function parseCharacterCombinations(bytes) {
  if (!(bytes instanceof Uint8Array) || bytes.length < 20)
    throw Error('CharBaseInfo: invalid table');
  const v = new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength);
  const count = v.getUint32(4, true);
  if (
    v.getUint32(0, true) !== 0x43424457 ||
    count > 256 ||
    v.getUint32(8, true) !== 2 ||
    v.getUint32(12, true) !== 2 ||
    20 + count * 2 + v.getUint32(16, true) !== bytes.length
  )
    throw Error('CharBaseInfo: unsupported packed layout');
  return Array.from({ length: count }, (_, i) => ({
    race: bytes[20 + i * 2],
    class: bytes[21 + i * 2],
  }));
}
const expected = {
  races: ['ChrRaces', 69],
  displays: ['CreatureDisplayInfo', 16],
  models: ['CreatureModelData', 28],
  sections: ['CharSections', 10],
  hair: ['CharHairGeosets', 6],
  facial: ['CharacterFacialHairStyles', 8],
  outfits: ['CharStartOutfit', 77],
  items: ['ItemDisplayInfo', 25],
};

export async function createCharacterCatalog(readAsset) {
  const tables = Object.fromEntries(
    await Promise.all(
      Object.entries(expected).map(async ([key, [name, minFields]]) => [
        key,
        parseDBC(await readAsset('DBFilesClient\\' + name + '.dbc'), {
          name,
          minFields,
          allowPacked: key === 'outfits',
        }),
      ]),
    ),
  );
  const { races, displays, models, sections, hair, facial, outfits, items } = tables;
  const combinations = parseCharacterCombinations(
    await readAsset('DBFilesClient\\CharBaseInfo.dbc'),
  );
  const itemRows = new Map(items.rows.map((row) => [items.u(row, 0), row]));
  const clothingPaths = new Map();
  async function clothingPath(name, folder, gender) {
    const key = `${folder}:${name}:${gender}`;
    if (!clothingPaths.has(key))
      clothingPaths.set(
        key,
        (async () => {
          for (const suffix of [gender ? '_F' : '_M', '_U', '']) {
            const path = `Item\\TextureComponents\\${folder}\\${name}${suffix}.blp`;
            try {
              await readAsset(path);
              return path;
            } catch (error) {
              if (!/missing member/i.test(error.message)) throw error;
              /* Try the client's next filename variant. */
            }
          }
          throw Error('Starting outfit texture unavailable: ' + name);
        })().catch((error) => {
          clothingPaths.delete(key);
          throw error;
        }),
      );
    return clothingPaths.get(key);
  }
  async function resolve(appearance) {
    const { race, gender, skin, face, hairStyle, hairColor, facialHair } = appearance;
    if (
      ![race, gender, skin, face, hairStyle, hairColor, facialHair].every(Number.isInteger) ||
      gender < 0 ||
      gender > 1
    )
      throw Error('Invalid character preview appearance');
    if (!combinations.some((row) => row.race === race && row.class === appearance.class))
      throw Error('Character race/class combination unavailable');
    const raceRow = races.byId(race);
    if (raceRow === undefined) throw Error('Character race unavailable');
    const display = displays.byId(races.u(raceRow, gender ? 5 : 4));
    if (display === undefined) throw Error('Character display unavailable');
    const model = models.byId(displays.u(display, 1));
    if (model === undefined) throw Error('Character model unavailable');
    const path = modelPath(models.text(model, 2));
    if (!/^character[\\/]/i.test(path) || !/\.m2$/i.test(path))
      throw Error('Character model path rejected');
    const section = (type, variation, color) =>
      sections.rows.find(
        (row) =>
          sections.u(row, 1) === race &&
          sections.u(row, 2) === gender &&
          sections.u(row, 3) === type &&
          sections.u(row, 8) === variation &&
          sections.u(row, 9) === color &&
          (!(sections.u(row, 7) & 4) || appearance.class === 6) &&
          sections.u(row, 7) & 1,
      );
    const paths = (row) =>
      row === undefined ? [] : [4, 5, 6].map((column) => texturePath(sections.text(row, column)));
    const body = paths(section(0, 0, skin));
    if (!body[0]) throw Error('Character skin texture unavailable');
    const faceTextures = paths(section(1, face, skin));
    const facialTextures = paths(section(2, facialHair, hairColor));
    const hairTextures = paths(section(3, hairStyle, hairColor));
    const underwear = paths(section(4, 0, skin));
    const layers = [{ path: body[0], region: 'base' }];
    for (const [textures, regions] of [
      [underwear, ['pelvis-upper', 'torso-upper']],
      [faceTextures, ['face-lower', 'face-upper']],
      [facialTextures, ['face-lower', 'face-upper']],
    ])
      for (let i = 0; i < regions.length; i++)
        if (textures[i])
          layers.push({
            path: textures[i],
            region: regions[i],
            optional: textures === facialTextures,
          });
    // Hair texture 0 is a separate replaceable slot; the other two are scalp overlays.
    for (const [index, region] of [
      [1, 'face-lower'],
      [2, 'face-upper'],
    ])
      if (hairTextures[index]) layers.push({ path: hairTextures[index], region, optional: true });
    const hairRow = hair.rows.find(
      (row) => hair.u(row, 1) === race && hair.u(row, 2) === gender && hair.u(row, 3) === hairStyle,
    );
    const facialRow = facial.rows.find(
      (row) =>
        facial.u(row, 0) === race && facial.u(row, 1) === gender && facial.u(row, 2) === facialHair,
    );
    const geosets = new Map();
    // 702 exposes uncovered ears; 701 is the hidden-ear variant. Models
    // with ears built into the body have no group 7 and remain unaffected.
    geosets.set(7, 2);
    // 1501 closes the bare back using the body atlas; 1502+ are actual
    // capes using replaceable slot 2. Hiding the whole group leaves holes.
    geosets.set(15, 1);
    // The eye glow is an optional mesh, not a default facial feature.
    geosets.set(17, appearance.class === 6 ? 3 : race === 4 ? 2 : 0);
    if (facialRow !== undefined)
      for (let i = 0; i < 3; i++) geosets.set(i + 1, facial.u(facialRow, 3 + i));
    const outfitEquipment = [];
    if (appearance.mode === 'create') {
      const outfit = outfits.rows.find((row) => {
        const packed = outfits.u(row, 1);
        return (
          (packed & 255) === race &&
          ((packed >>> 8) & 255) === appearance.class &&
          ((packed >>> 16) & 255) === gender &&
          packed >>> 24 === 0
        );
      });
      if (outfit === undefined)
        throw Error('Starting outfit unavailable for race, class and gender');
      for (let i = 0; i < 24; i++) {
        const display = outfits.u(outfit, 26 + i),
          inventoryType = outfits.u(outfit, 50 + i);
        if (!display || display === 0xffffffff || !inventoryType || inventoryType === 0xffffffff)
          continue;
        const row = itemRows.get(display);
        if (row === undefined) throw Error('Starting item display unavailable: ' + display);
        outfitEquipment.push({ display, inventoryType, row });
      }
      const order = new Map([
        [4, 0],
        [7, 1],
        [8, 2],
        [9, 3],
        [5, 4],
        [20, 4],
        [6, 5],
        [10, 6],
        [19, 7],
      ]);
      const components = [
        ['ArmUpperTexture', 'arm-upper'],
        ['ArmLowerTexture', 'arm-lower'],
        ['HandTexture', 'hand'],
        ['TorsoUpperTexture', 'torso-upper'],
        ['TorsoLowerTexture', 'torso-lower'],
        ['LegUpperTexture', 'pelvis-upper'],
        ['LegLowerTexture', 'pelvis-lower'],
        ['FootTexture', 'foot'],
      ];
      for (const item of [...outfitEquipment].sort(
        (a, b) => (order.get(a.inventoryType) ?? 99) - (order.get(b.inventoryType) ?? 99),
      )) {
        if (!order.has(item.inventoryType)) continue;
        for (let i = 0; i < components.length; i++) {
          const name = items.text(item.row, 15 + i);
          if (!name) continue;
          const [folder, region] = components[i];
          layers.push({ path: await clothingPath(name, folder, gender), region });
        }
        if (item.inventoryType === 20) geosets.set(13, items.u(item.row, 9) + 1);
        if (item.inventoryType === 10) geosets.set(4, items.u(item.row, 7) + 1);
        if (item.inventoryType === 8) geosets.set(5, items.u(item.row, 8) + 1);
      }
    }
    const equipment =
      appearance.mode === 'create'
        ? outfitEquipment.map(({ display, inventoryType }) => ({ display, inventoryType }))
        : appearance.equipment || [];
    let cloakTexture = null;
    const cloak = equipment.find((item) => item.inventoryType === 16 && item.display);
    if (cloak) {
      const row = itemRows.get(cloak.display);
      if (row === undefined) throw Error('Cloak display unavailable: ' + cloak.display);
      const name = items.text(row, 3);
      if (name) {
        cloakTexture = texturePath(`Item\\ObjectComponents\\Cape\\${name}`);
        geosets.set(15, Math.max(2, items.u(row, 7) + 1));
      }
    }
    const attachments = [];
    for (const [attachmentId, types] of [
      [1, [13, 17, 21, 15, 25, 26]],
      [2, [14, 22, 23]],
    ]) {
      const item = equipment.find((entry) => types.includes(entry.inventoryType) && entry.display);
      if (!item) continue;
      const row = itemRows.get(item.display);
      if (row === undefined) throw Error('Held item display unavailable: ' + item.display);
      const file = items.text(row, 1),
        texture = items.text(row, 3);
      if (!file) continue;
      const folders = item.inventoryType === 14 ? ['Shield', 'Weapon'] : ['Weapon', 'Shield'];
      let supplied = false;
      for (const folder of folders) {
        const prefix = `Item\\ObjectComponents\\${folder}\\`;
        const path = modelPath(prefix + file).replace(/\.mdl$/i, '.m2');
        try {
          await readAsset(path);
          attachments.push({
            attachmentId,
            modelPath: path,
            skinPath: path.replace(/\.m2$/i, '00.skin'),
            texture: texturePath(prefix + texture),
          });
          supplied = true;
          break;
        } catch (error) {
          if (!/missing member/i.test(error.message)) throw error;
        }
      }
      if (!supplied) throw Error('Held item model unavailable: ' + file);
    }
    return {
      modelPath: path,
      skinPath: path.replace(/\.m2$/i, '00.skin'),
      textureLayers: layers,
      hairTexture: hairTextures[0] || body[0],
      extraSkinTexture: body[1] || body[0],
      cloakTexture,
      hairGeoset: hairRow === undefined ? 0 : hair.u(hairRow, 4),
      bald: hairRow === undefined || !!hair.u(hairRow, 5),
      geosets,
      equipment,
      attachments,
    };
  }
  return { resolve, tables };
}

const regions = {
  'face-upper': 0,
  'face-lower': 1,
  'torso-upper': 2,
  'pelvis-upper': 3,
  'arm-upper': 4,
  'arm-lower': 5,
  hand: 6,
  'torso-lower': 7,
  'pelvis-lower': 8,
  foot: 9,
};
export async function composeCharacterSkin(
  layers,
  decodeTexture,
  e,
  onIssue = () => {},
  checkpoint = () => {},
) {
  const base = await decodeTexture(layers[0].path);
  const validate = (image) => {
    if (
      !Number.isInteger(image.width) ||
      !Number.isInteger(image.height) ||
      image.width < 1 ||
      image.height < 1 ||
      image.width > 2048 ||
      image.height > 2048 ||
      !(image.rgba instanceof Uint8Array) ||
      image.rgba.length !== image.width * image.height * 4
    )
      throw Error('Character skin pixel dimensions rejected');
  };
  validate(base);
  if (e.character_skin_begin(base.width, base.height)) throw Error('Character skin atlas capacity');
  try {
    new Uint8Array(e.memory.buffer, e.character_skin_atlas(), base.rgba.length).set(base.rgba);
    for (const layer of layers.slice(1)) {
      checkpoint();
      const region = regions[layer.region];
      if (region === undefined) throw Error('Unsupported character skin region');
      let source;
      try {
        source = await decodeTexture(layer.path);
      } catch (error) {
        // Some shipped CharSections rows name absent scalp/facial overlays.
        // Their absence must not reject the required body or hide its model.
        if (!layer.optional || !/^missing member /i.test(error.message)) throw error;
        onIssue('Optional character overlay unavailable: ' + layer.path);
        continue;
      }
      validate(source);
      if (e.character_skin_layer(source.width, source.height))
        throw Error('Character skin layer capacity');
      new Uint8Array(e.memory.buffer, e.character_skin_pixels(), source.rgba.length).set(
        source.rgba,
      );
      if (e.character_skin_blend(region)) throw Error('Character skin composition rejected');
    }
    return {
      width: base.width,
      height: base.height,
      rgba: new Uint8Array(e.memory.buffer, e.character_skin_atlas(), base.rgba.length).slice(),
    };
  } finally {
    e.character_skin_end();
  }
}
