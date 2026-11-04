// Resource discovery only. The C loader parses XML, composes templates and
// executes the unchanged Lua sources. No owner source bytes are shipped in Git.
const root = 'Interface/FrameXML/';
export const frameXMLHash = async (bytes) =>
  Array.from(new Uint8Array(await crypto.subtle.digest('SHA-256', bytes)), (v) =>
    v.toString(16).padStart(2, '0'),
  ).join('');
export const worldUIEntries = [
  'GlobalStrings.lua',
  'Constants.lua',
  'Fonts.xml',
  'FontStyles.xml',
  'BasicControls.xml',
  'WorldFrame.xml',
  'UIParent.xml',
  'GameTooltip.xml',
  'UIDropDownMenu.xml',
  'SecureTemplates.xml',
  'TextStatusBar.lua',
  'TextStatusBar.xml',
  'UnitFrame.xml',
  'VoiceChat.xml',
  'ReadyCheck.xml',
  'UIPanelTemplates.lua',
  'UIPanelTemplates.xml',
  'ItemButtonTemplate.xml',
  'Cooldown.xml',
  'ActionButtonTemplate.xml',
  'MainMenuBar.xml',
  'MainMenuBarMicroButtons.xml',
  'ActionBarFrame.xml',
  'BonusActionBarFrame.xml',
  'MainMenuBarBagButtons.xml',
  'AutoComplete.xml',
  'UIMenu.xml',
  'ChatFrame.xml',
  'FloatingChatFrame.xml',
  'TutorialFrame.xml',
  'PlayerFrame.xml',
  'RuneFrame.xml',
  'Minimap.xml',
  'GameTime.xml',
];
const templateEntries = new Set([
  'GameTooltip.xml',
  'UIDropDownMenu.xml',
  'SecureTemplates.xml',
  'AutoComplete.xml',
  'VoiceChat.xml',
  'ReadyCheck.xml',
]);
function pathOf(name, base = root) {
  const parts = (/^Interface[\\/]/i.test(name) ? name : base + name)
    .replaceAll('\\', '/')
    .split('/');
  const normalized = [];
  for (const part of parts) {
    if (!part || part === '.') continue;
    if (part === '..') {
      if (!normalized.length) throw Error('FrameXML resource escapes its root');
      normalized.pop();
    } else normalized.push(part);
  }
  const path = normalized.join('/');
  if (!/^Interface\/FrameXML\/[\x20-\x7e]+\.(?:xml|lua)$/i.test(path) || path.length > 511)
    throw Error('Unsupported FrameXML resource path: ' + path);
  return path;
}
export async function collectFrameXML(readAsset) {
  const decoder = new TextDecoder('utf-8', { fatal: true });
  const tocBytes = await readAsset(root + 'FrameXML.toc');
  const tocText = decoder.decode(tocBytes);
  if (!/^##\s*Interface:\s*30300\s*$/m.test(tocText.replaceAll('\r', '')))
    throw Error('Unsupported original FrameXML interface version');
  const toc = tocText
    .split(/\r?\n/)
    .map((v) => v.trim())
    .filter((v) => v && !v.startsWith('#'))
    .map((v) => pathOf(v));
  const resources = new Map(),
    events = new Set(['PLAYER_LOGIN', 'PLAYER_ENTERING_WORLD']),
    fonts = new Set(),
    textures = new Set(),
    optionalTextures = new Set();
  let total = 0;
  async function collect(path, depth = 0) {
    const key = path.toLowerCase();
    if (resources.has(key)) return;
    if (depth > 32 || resources.size >= 256) throw Error('FrameXML dependency capacity');
    const bytes = new Uint8Array(await readAsset(path));
    total += bytes.length;
    if (bytes.length > 1048576 || total > 16 * 1048576) throw Error('FrameXML source capacity');
    const source = decoder.decode(bytes);
    resources.set(key, { path, bytes, sha256: await frameXMLHash(bytes) });
    for (const match of source.matchAll(/RegisterEvent\(\s*["']([A-Z][A-Z_0-9]+)["']/g))
      events.add(match[1]);
    if (/\.lua$/i.test(path)) {
      for (const match of source.matchAll(/["'](Interface(?:\\\\|\\)[^"'\r\n]+)["']/g)) {
        const texture = match[1].replaceAll('\\\\', '\\');
        if (!/\.(lua|xml|m2|mdx|mdl)$/i.test(texture) && !texture.includes('%')) {
          textures.add(texture);
          optionalTextures.add(texture);
        }
      }
    }
    if (/\.xml$/i.test(path)) {
      // Only discover literal resource references. XML semantics stay in WASM.
      for (const match of source.matchAll(/\bfont="([^"\r\n]+\.ttf)"/gi)) fonts.add(match[1]);
      for (const match of source.matchAll(
        /\b(?:file|bgFile|edgeFile)="(Interface[\\/][^"\r\n]+)"/gi,
      ))
        if (!/\.(?:xml|lua|m2|mdx|mdl|ttf)$/i.test(match[1])) textures.add(match[1]);
      for (const match of source.matchAll(/LoadMicroButtonTextures\(self,\s*"([A-Za-z]+)"/g))
        for (const suffix of ['Up', 'Down', 'Disabled'])
          textures.add('Interface\\Buttons\\UI-MicroButton-' + match[1] + '-' + suffix);
      const base = path.slice(0, path.lastIndexOf('/') + 1);
      for (const match of source.matchAll(/<(?:Script|Include)\b[^>]*\bfile="([^"]+)"/g))
        await collect(pathOf(match[1], base), depth + 1);
    }
  }
  for (const name of worldUIEntries) {
    const path = pathOf(name);
    if (!toc.some((v) => v.toLowerCase() === path.toLowerCase()) && name !== 'TextStatusBar.lua')
      throw Error('FrameXML entry absent from original TOC: ' + name);
    await collect(path);
  }
  const canonical = (path) => path.toLowerCase().replaceAll('\\', '/');
  const uniqueTextures = (values) => [
    ...new Map(values.map((path) => [canonical(path), path])).values(),
  ];
  return {
    toc,
    resources: [...resources.values()],
    events: [...events],
    fonts: [...fonts],
    textures: uniqueTextures([
      ...textures,
      'Interface\\Buttons\\UI-Quickslot',
      'Interface\\Buttons\\UI-MicroButtonCharacter-Up',
      'Interface\\Buttons\\UI-MicroButtonCharacter-Down',
      'Interface\\Buttons\\UI-MicroButton-Hilight',
      'Interface\\Buttons\\Button-Backpack-Up',
      'Interface\\TargetingFrame\\UI-PVP-Alliance',
      'Interface\\TargetingFrame\\UI-PVP-Horde',
    ]),
    optionalTextures: [...optionalTextures],
    entries: worldUIEntries.map((name) => ({
      path: pathOf(name),
      templates: templateEntries.has(name),
    })),
  };
}
