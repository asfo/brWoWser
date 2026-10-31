import { parseDBC, parseCharacterCombinations } from './catalog.mjs';

const raceTokens = {
  1: 'HUMAN',
  2: 'ORC',
  3: 'DWARF',
  4: 'NIGHTELF',
  5: 'SCOURGE',
  6: 'TAUREN',
  7: 'GNOME',
  8: 'TROLL',
  10: 'BLOODELF',
  11: 'DRAENEI',
};
const classTokens = {
  1: 'WARRIOR',
  2: 'PALADIN',
  3: 'HUNTER',
  4: 'ROGUE',
  5: 'PRIEST',
  6: 'DEATHKNIGHT',
  7: 'SHAMAN',
  8: 'MAGE',
  9: 'WARLOCK',
  11: 'DRUID',
};
const locales = ['enUS', 'koKR', 'frFR', 'deDE', 'zhCN', 'zhTW', 'esES', 'esMX', 'ruRU'];

// Names, appearance ranges and available combinations come from the selected
// local client's tables, never a fabricated list of server-side characters.
export async function readCharacterMetadata(readAsset, locale) {
  const table = async (name, minFields) =>
    parseDBC(await readAsset('DBFilesClient\\' + name + '.dbc'), {
      name,
      minFields,
      allowPacked: name === 'CharStartOutfit',
    });
  const [races, classes, sections, facial, baseInfo, areas, displays, models] = await Promise.all([
    table('ChrRaces', 69),
    table('ChrClasses', 60),
    table('CharSections', 10),
    table('CharacterFacialHairStyles', 8),
    readAsset('DBFilesClient\\CharBaseInfo.dbc'),
    table('AreaTable', 28),
    table('CreatureDisplayInfo', 16),
    table('CreatureModelData', 28),
  ]);
  const localized = (dbc, row, column) => {
    const index = Math.max(0, locales.indexOf(locale));
    return dbc.text(row, column + index) || dbc.text(row, column);
  };
  const result = {
    races: [],
    classes: [],
    sections: [],
    facialHair: [],
    combinations: [],
    zones: [],
    names: [],
    models: [],
  };
  for (let row = 0; row < races.count; row++) {
    const id = races.u(row, 0);
    if (!raceTokens[id] || races.u(row, 1) & 1) continue;
    const background = races.text(row, 11);
    if (!/^[A-Za-z][A-Za-z0-9_]{0,30}$/.test(background))
      throw Error('Invalid race background token: ' + id);
    result.races.push({
      id,
      name: localized(races, row, 14),
      token: raceTokens[id],
      background,
      faction: races.u(row, 13) === 0 ? 'Alliance' : 'Horde',
      expansion: races.u(row, 68),
      hairCustomization: races.text(row, 67),
      facialHairCustomization: races.text(row, 65),
      facialHairCustomizationFemale: races.text(row, 66),
    });
    for (const gender of [0, 1]) {
      const displayId = races.u(row, gender ? 5 : 4),
        display = displays.byId(displayId);
      if (display === undefined) continue;
      const model = models.byId(displays.u(display, 1));
      if (model === undefined) continue;
      const path = models.text(model, 2).replace(/\.mdx$/i, '.m2');
      if (/^character[\\/][\x20-\x7e]{1,500}\.m2$/i.test(path) && !result.models.includes(path))
        result.models.push(path);
    }
  }
  for (let row = 0; row < classes.count; row++) {
    const id = classes.u(row, 0);
    if (!classTokens[id]) continue;
    result.classes.push({
      id,
      name: localized(classes, row, 4),
      femaleName: localized(classes, row, 21) || localized(classes, row, 4),
      token: classTokens[id],
      expansion: classes.u(row, 59),
      tank: [1, 2, 6, 11].includes(id),
      healer: [2, 5, 7, 11].includes(id),
      damage: true,
    });
  }
  for (let row = 0; row < sections.count; row++) {
    const flags = sections.u(row, 7);
    if (!(flags & 1) || !raceTokens[sections.u(row, 1)]) continue;
    result.sections.push({
      race: sections.u(row, 1),
      gender: sections.u(row, 2),
      type: sections.u(row, 3),
      variation: sections.u(row, 8),
      color: sections.u(row, 9),
      flags,
    });
  }
  for (let row = 0; row < facial.count; row++)
    if (raceTokens[facial.u(row, 0)])
      result.facialHair.push({
        race: facial.u(row, 0),
        gender: facial.u(row, 1),
        variation: facial.u(row, 2),
      });
  // Outfits include unused/nonplayable combinations. The packed two-byte
  // CharBaseInfo rows define the actual race/class availability.
  result.combinations = parseCharacterCombinations(baseInfo).filter(
    (row) => raceTokens[row.race] && classTokens[row.class],
  );
  for (let row = 0; row < areas.count; row++) {
    const name = localized(areas, row, 11);
    if (name) result.zones.push({ id: areas.u(row, 0), name });
  }
  // GlueXML places the first five buttons in Alliance's column, then Horde's.
  const raceOrder = [1, 3, 4, 7, 11, 2, 5, 6, 8, 10];
  result.races.sort((a, b) => raceOrder.indexOf(a.id) - raceOrder.indexOf(b.id));
  result.classes.sort((a, b) => a.id - b.id);
  const names = await table('NameGen', 4);
  for (let row = 0; row < names.count; row++) {
    const race = names.u(row, 2),
      gender = names.u(row, 3),
      name = names.text(row, 1);
    if (raceTokens[race] && gender < 2 && name && new TextEncoder().encode(name).length <= 48)
      result.names.push({ race, gender, name });
  }
  if (
    !result.races.length ||
    !result.classes.length ||
    !result.combinations.length ||
    !result.sections.length
  )
    throw Error('Local character metadata is incomplete');
  return result;
}
