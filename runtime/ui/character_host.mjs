// Host serialization only. Choice, cursor and customization state remain in C.
function record(api, kind, fields, strings = [], context = '') {
  const encoded = strings.map((s) => new TextEncoder().encode(String(s)));
  const bytes = new Uint8Array(fields.length * 4 + encoded.reduce((n, b) => n + b.length + 1, 0));
  const view = new DataView(bytes.buffer);
  for (let i = 0; i < fields.length; i++) {
    const value = fields[i];
    if (!Number.isInteger(value) || value < 0 || value > 0xffffffff)
      throw Error('Invalid character metadata number');
    view.setUint32(i * 4, value, true);
  }
  let at = fields.length * 4;
  for (const text of encoded) {
    if (text.includes(0)) throw Error('Invalid character metadata string');
    bytes.set(text, at);
    at += text.length + 1;
  }
  api.input(bytes);
  if (api.e.glue_character_import(kind, bytes.length))
    throw Error(
      'Character metadata import rejected: kind ' + kind + (context ? ' (' + context + ')' : ''),
    );
}

export function importCharacterMetadata(api, metadata) {
  if (!metadata) return;
  for (const [kind, rows] of [
    [1, metadata.races],
    [2, metadata.classes],
  ]) {
    if (!Array.isArray(rows) || rows.length > 10) throw Error('Invalid character choices');
    for (const row of rows)
      record(
        api,
        kind,
        [row.id, row.faction === 'Horde' ? 1 : 0, +!!row.tank, +!!row.healer, +!!row.damage],
        [
          row.name,
          row.token,
          row.hairCustomization || '',
          row.facialHairCustomization || '',
          row.facialHairCustomizationFemale || '',
          row.background || row.token,
        ],
        (kind === 1 ? 'race ' : 'class ') + row.id,
      );
  }
  for (const row of metadata.combinations || [])
    record(api, 3, [row.race, row.class], [], `race ${row.race}, class ${row.class}`);
  for (const row of metadata.sections || [])
    record(
      api,
      4,
      [row.race, row.gender, row.type, row.variation, row.color, row.flags || 0],
      [],
      `race ${row.race}, gender ${row.gender}, section ${row.type}`,
    );
  for (const row of metadata.facialHair || [])
    record(
      api,
      5,
      [row.race, row.gender, 0, row.variation, 0],
      [],
      `race ${row.race}, gender ${row.gender}`,
    );
  let seed = 0;
  while (!seed) seed = crypto.getRandomValues(new Uint32Array(1))[0];
  record(api, 7, [seed]);
  for (const [index, row] of (metadata.names || []).entries())
    record(api, 8, [row.race, row.gender], [row.name], `name row ${index}`);
}

export function importCharacterRoster(api, rows, metadata) {
  if (!Array.isArray(rows) || rows.length > 50 || api.e.glue_character_roster_begin())
    throw Error('Character roster import rejected');
  const zones = new Map((metadata?.zones || []).map((row) => [row.id, row.name]));
  for (const row of rows)
    record(
      api,
      6,
      [
        row.guid[0],
        row.guid[1],
        row.race,
        row.class,
        row.gender,
        row.level,
        row.flags,
        row.customization,
        row.skin,
        row.face,
        row.hairStyle,
        row.hairColor,
        row.facialHair,
      ],
      [row.name, zones.get(row.zone) || ''],
    );
  if (api.e.glue_character_connected(1)) throw Error('Character session import rejected');
}

export function characterPreview(e, rows) {
  const screen = e.glue_screen_state();
  if (screen !== 4 && screen !== 5) return null;
  const role = screen === 5 ? 1 : 0;
  const selected = screen === 4 ? rows[e.glue_character_metric(2) - 1] : null;
  const parameter = (field, index = 0) => e.frame_model_parameter(role, field, index);
  const lighting = (group) => ({
    fog: {
      enabled: !!parameter(0),
      rgb: [parameter(1), parameter(2), parameter(3)],
      near: parameter(4),
      far: parameter(5),
    },
    glow: parameter(6),
    lights: Array.from({ length: parameter(7, group) }, (_, i) =>
      Array.from({ length: 13 }, (_, n) => parameter(8, group * 52 + i * 13 + n)),
    ),
  });
  const backgroundLighting = lighting(0),
    characterLighting = lighting(selected?.flags & 0x2000 ? 3 : 2);
  if (screen === 4) {
    return {
      mode: 'select',
      ...(selected || {}),
      visible: !!selected,
      lighting: characterLighting,
      backgroundLighting,
      facing: e.glue_character_facing(0),
      revision: e.glue_character_metric(11),
    };
  }
  const n = (field) => e.glue_character_metric(field);
  return {
    mode: 'create',
    lighting: characterLighting,
    backgroundLighting,
    visible: true,
    race: n(13),
    class: n(14),
    gender: n(5),
    skin: n(6),
    face: n(7),
    hairStyle: n(8),
    hairColor: n(9),
    facialHair: n(10),
    equipment: [],
    facing: e.glue_character_facing(1),
    revision: n(11),
  };
}

export function readCharacterCreate(api) {
  const length = api.e.glue_character_request();
  if (length < 10 || length > 58) throw Error('Character creation handoff rejected');
  const bytes = new Uint8Array(api.e.memory.buffer, api.e.frame_input_ptr(), length);
  try {
    const end = bytes.indexOf(0);
    if (end < 0 || end + 10 !== length) throw Error('Invalid character creation handoff');
    const result = {
      name: new TextDecoder('utf-8', { fatal: true }).decode(bytes.subarray(0, end)),
    };
    for (const [i, field] of [
      'race',
      'class',
      'gender',
      'skin',
      'face',
      'hairStyle',
      'hairColor',
      'facialHair',
      'outfit',
    ].entries())
      result[field] = bytes[end + 1 + i];
    return result;
  } finally {
    bytes.fill(0);
  }
}
