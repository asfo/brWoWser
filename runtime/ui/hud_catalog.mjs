import { parseDBC } from '../character/catalog.mjs';
// Owner-resource discovery only. Packet interpretation, paging, resource values,
// tutorial policy and UI composition remain in the C/Lua runtimes.
export function createHUDCatalog(readAsset) {
  let tables;
  return async (state) => {
    tables ??= (async () =>
      Object.fromEntries(
        await Promise.all(
          ['Spell', 'SpellIcon', 'Item', 'ItemDisplayInfo'].map(async (name) => [
            name,
            parseDBC(await readAsset('DBFilesClient/' + name + '.dbc'), {
              name,
              maxBytes: 67108864,
            }),
          ]),
        ),
      ))();
    const t = await tables,
      spells = t.Spell,
      icons = t.SpellIcon;
    const iconRows = new Map(icons.rows.map((row) => [icons.u(row, 0), row]));
    const actions = new Set(
      state.actions.filter((value) => value && value >>> 24 === 0).map((value) => value & 0xffffff),
    );
    const learned = new Set(state.spells),
      records = [];
    for (const row of spells.rows) {
      const id = spells.u(row, 0);
      if (!actions.has(id) && !learned.has(id)) continue;
      const shape = [0, 1, 2].find((i) => spells.u(row, 95 + i) === 36);
      const aura = spells.u(row, 208) === 10 && !!(spells.u(row, 211) & 0x20);
      const stance = learned.has(id) && (shape !== undefined || aura);
      if (!actions.has(id) && !stance) continue;
      const icon = iconRows.get(spells.u(row, 133));
      if (icon === undefined) continue;
      records.push({
        id,
        kind: 0,
        order: stance ? spells.u(row, 215) + 1 : 0,
        form: shape === undefined ? 0 : spells.u(row, 110 + shape),
        path: icons.text(icon, 1),
        name: spells.text(row, 136),
      });
    }
    const itemActions = new Set(
      state.actions.filter((value) => value >>> 24 === 0x80).map((value) => value & 0xffffff),
    );
    const displays = t.ItemDisplayInfo;
    const displayRows = new Map(displays.rows.map((row) => [displays.u(row, 0), row]));
    for (const row of t.Item.rows)
      if (itemActions.has(t.Item.u(row, 0))) {
        const display = displayRows.get(t.Item.u(row, 5));
        if (display !== undefined) {
          let path = displays.text(display, 5);
          if (path && !/^Interface[\\/]/i.test(path)) path = 'Interface\\Icons\\' + path;
          records.push({ id: t.Item.u(row, 0), kind: 0x80, order: 0, form: 0, path, name: '' });
        }
      }
    if (records.length > 512) throw Error('HUD metadata capacity exceeded');
    return records;
  };
}
