// 12340 CMSG_CHAR_CREATE fields, in AzerothCore HandleCharCreateOpcode order.
// Appearance ranges and race/class restrictions are validated by the server.
export function encodeCharacterCreate(character) {
  if (!character || typeof character.name !== 'string') throw Error('A character name is required');
  const name = new TextEncoder().encode(character.name);
  // Empty names are valid wire input: the server returns CHAR_NAME_NO_NAME.
  if (name.length > 48 || character.name.includes('\0'))
    throw Error('Character name exceeds protocol limits');
  const fields = [
    'race',
    'class',
    'gender',
    'skin',
    'face',
    'hairStyle',
    'hairColor',
    'facialHair',
    'outfit',
  ];
  const values = fields.map((field) => {
    const value = character[field];
    if (!Number.isInteger(value) || value < 0 || value > 255)
      throw Error('Invalid character creation field: ' + field);
    return value;
  });
  if (!values[0] || !values[1] || values[2] > 1)
    throw Error('Invalid character race, class or gender');
  const bytes = new Uint8Array(name.length + 1 + fields.length);
  bytes.set(name);
  bytes.set(values, name.length + 1);
  return bytes;
}

const createResults = [
  'CHAR_CREATE_SUCCESS',
  'CHAR_CREATE_ERROR',
  'CHAR_CREATE_FAILED',
  'CHAR_CREATE_NAME_IN_USE',
  'CHAR_CREATE_DISABLED',
  'CHAR_CREATE_PVP_TEAMS_VIOLATION',
  'CHAR_CREATE_SERVER_LIMIT',
  'CHAR_CREATE_ACCOUNT_LIMIT',
  'CHAR_CREATE_SERVER_QUEUE',
  'CHAR_CREATE_ONLY_EXISTING',
  'CHAR_CREATE_EXPANSION',
  'CHAR_CREATE_EXPANSION_CLASS',
  'CHAR_CREATE_LEVEL_REQUIREMENT',
  'CHAR_CREATE_UNIQUE_CLASS_LIMIT',
  'CHAR_CREATE_CHARACTER_IN_GUILD',
  'CHAR_CREATE_RESTRICTED_RACECLASS',
  'CHAR_CREATE_CHARACTER_CHOOSE_RACE',
  'CHAR_CREATE_CHARACTER_ARENA_LEADER',
  'CHAR_CREATE_CHARACTER_DELETE_MAIL',
  'CHAR_CREATE_CHARACTER_SWAP_FACTION',
  'CHAR_CREATE_CHARACTER_RACE_ONLY',
  'CHAR_CREATE_CHARACTER_GOLD_LIMIT',
  'CHAR_CREATE_FORCE_LOGIN',
];
const nameResults = [
  'CHAR_NAME_SUCCESS',
  'CHAR_NAME_FAILURE',
  'CHAR_NAME_NO_NAME',
  'CHAR_NAME_TOO_SHORT',
  'CHAR_NAME_TOO_LONG',
  'CHAR_NAME_INVALID_CHARACTER',
  'CHAR_NAME_MIXED_LANGUAGES',
  'CHAR_NAME_PROFANE',
  'CHAR_NAME_RESERVED',
  'CHAR_NAME_INVALID_APOSTROPHE',
  'CHAR_NAME_MULTIPLE_APOSTROPHES',
  'CHAR_NAME_THREE_CONSECUTIVE',
  'CHAR_NAME_INVALID_SPACE',
  'CHAR_NAME_CONSECUTIVE_SPACES',
  'CHAR_NAME_RUSSIAN_CONSECUTIVE_SILENT_CHARACTERS',
  'CHAR_NAME_RUSSIAN_SILENT_CHARACTER_AT_BEGINNING_OR_END',
  'CHAR_NAME_DECLENSION_DOESNT_MATCH_BASE_NAME',
];
export function characterCreateResult(bytes) {
  if (bytes.length !== 1) throw Error('Invalid character creation response');
  const code = bytes[0];
  return Object.freeze({
    code,
    success: code === 0x2f,
    token: createResults[code - 0x2f] || nameResults[code - 0x57] || 'CHAR_CREATE_ERROR',
  });
}

// Selection uses the complete GUID, never a roster index carried across refreshes.
export function createCharacterRoster(initial) {
  let rows = [],
    selected = null;
  const key = (guid) => {
    if (
      !Array.isArray(guid) ||
      guid.length !== 2 ||
      guid.some((n) => !Number.isInteger(n) || n < 0 || n > 0xffffffff)
    )
      throw Error('Invalid character GUID');
    return guid[0] + ':' + guid[1];
  };
  function replace(next) {
    if (!Array.isArray(next) || next.length > 50) throw Error('Character roster exceeds limit');
    const ids = next.map((row) => key(row.guid));
    if (new Set(ids).size !== ids.length) throw Error('Duplicate character GUID');
    rows = Object.freeze(
      next.map((row) => {
        const copy = structuredClone(row);
        Object.freeze(copy.guid);
        return Object.freeze(copy);
      }),
    );
    selected = ids.includes(selected) ? selected : (ids[0] ?? null);
    return rows;
  }
  replace(initial);
  return {
    replace,
    get rows() {
      return rows;
    },
    get selected() {
      return rows.find((row) => key(row.guid) === selected) ?? null;
    },
    select(guid) {
      const id = key(guid);
      if (!rows.some((row) => key(row.guid) === id)) throw Error('Character is not in this roster');
      selected = id;
      return this.selected;
    },
  };
}
