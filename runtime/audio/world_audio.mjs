import { createMediaPlayer } from './media_player.mjs';

// Browser audio output only. Area inheritance, source IDs, file selection,
// volume and music silence intervals are supplied by the world WASM decoder.
export function createWorldAudio(source, { settings = [], onIssue = console.warn } = {}) {
  const values = new Map(settings.map((row) => [row.name.toLowerCase(), Number(row.value)]));
  const value = (name, fallback) => values.get(name.toLowerCase()) ?? fallback;
  const enabled = value('Sound_EnableAllSound', 1) * value('Sound_MasterVolume', 1);
  const gains = [
    enabled * value('Sound_EnableMusic', 1) * value('Sound_MusicVolume', 0.4),
    enabled * value('Sound_EnableAmbience', 1) * value('Sound_AmbienceVolume', 0.6),
    enabled * value('Sound_EnableMusic', 1) * value('Sound_MusicVolume', 0.4),
  ];
  const slots = Array.from({ length: 3 }, () => ({ generation: 0, key: '', timer: null }));
  let closed = false;
  function stop(slot) {
    slot.generation++;
    clearTimeout(slot.timer);
    slot.timer = null;
    if (slot.element) {
      slot.element.onended = null;
      slot.element.onerror = null;
    }
    slot.player?.dispose().catch((error) => onIssue(error.message));
    if (slot.url) URL.revokeObjectURL(slot.url);
    slot.player = slot.element = slot.url = null;
  }
  function sync(channels) {
    if (closed) return;
    for (let channel = 0; channel < 3; channel++) {
      const descriptor = channels?.[channel],
        slot = slots[channel];
      const key = descriptor && gains[channel] ? descriptor.id + ':' + descriptor.revision : '';
      if (key === slot.key) continue;
      stop(slot);
      slot.key = key;
      if (!key || !descriptor.path) continue;
      const mine = slot.generation;
      slot.timer = setTimeout(
        async () => {
          slot.timer = null;
          try {
            const bytes = await source.readAsset(descriptor.path);
            if (closed || mine !== slot.generation) return;
            const element = new Audio();
            const magic = String.fromCharCode(...bytes.subarray(0, 4));
            const mediaType =
              magic === 'RIFF' ? 'audio/wav' : magic === 'OggS' ? 'audio/ogg' : 'audio/mpeg';
            slot.url = URL.createObjectURL(
              new Blob([bytes], {
                type: mediaType,
              }),
            );
            slot.element = element;
            slot.player = createMediaPlayer(element);
            slot.player.setVolume(Math.min(1, Math.max(0, descriptor.volume * gains[channel])));
            element.loop = channel === 1;
            element.preload = 'auto';
            element.src = slot.url;
            element.onerror = () => onIssue('World audio decode failed: ' + descriptor.path);
            if (channel === 0)
              element.onended = () => {
                source
                  .world('audio-next', { channel })
                  .then((next) => {
                    if (!closed && mine === slot.generation) sync(next);
                  })
                  .catch((error) => onIssue(error.message));
              };
            await slot.player.play();
          } catch (error) {
            if (!closed && mine === slot.generation)
              onIssue('World audio ' + descriptor.path + ': ' + error.message);
          }
        },
        Math.max(0, descriptor.delay || 0),
      );
    }
  }
  return {
    sync,
    dispose() {
      if (closed) return;
      closed = true;
      for (const slot of slots) stop(slot);
    },
  };
}
