// A single original login fade can reuse an immutable premultiplied UI surface.
// C/Lua still owns alpha and timing. Mixed/modal trees use ordinary commands.
export function normalizeLoginFade(packet, items) {
  const root = items.find((item) => item.name === 'AccountLoginUI');
  if (!root || !packet.commands.length) return packet;
  const byId = new Map(items.map((item) => [item.object, item])),
    commands = [];
  for (const command of packet.commands) {
    let item = byId.get(command.object),
      alpha = 1,
      found = false;
    for (let depth = 0; item && depth < items.length; depth++) {
      if (item.object === root.object) {
        found = true;
        break;
      }
      if (!Number.isFinite(item.ownAlpha) || item.ownAlpha < 0 || item.ownAlpha > 1) return packet;
      alpha *= item.ownAlpha;
      item = byId.get(item.parent);
    }
    if (!found) return packet;
    commands.push({ ...command, alpha });
  }
  return { ...packet, commands, surfaceAlpha: root.alpha };
}
