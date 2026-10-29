// Provider transport only. Asset identities must be admitted after host-side
// verification; successful Lua enqueue never means audible output.
export function createGlueAudioBridge(exports, assets) {
  const catalogue = new Map();
  let busy = false,
    closed = false;
  for (const asset of assets) {
    const key = `${asset.channel}:${asset.id}`;
    if (
      ![0, 1, 2].includes(asset.channel) ||
      !Number.isInteger(asset.id) ||
      asset.id <= 0 ||
      catalogue.has(key) ||
      typeof asset.player?.play !== 'function' ||
      typeof asset.player?.stop !== 'function'
    )
      throw Error('invalid audio catalogue');
    catalogue.set(key, asset.player);
  }
  const active = new Map(),
    started = new Set();
  return {
    async drain() {
      if (closed || busy) throw Error('audio bridge unavailable');
      busy = true;
      const results = [];
      try {
        while (exports.glue_audio_next() === 0) {
          const request = {
            token: exports.glue_audio_token(),
            operation: exports.glue_audio_operation(),
            channel: exports.glue_audio_channel(),
            asset: exports.glue_audio_id(),
            fade_seconds: exports.glue_audio_fade(),
          };
          let failure = null;
          try {
            if (request.operation === 1) {
              const player = catalogue.get(`${request.channel}:${request.asset}`);
              if (!player) throw Error('unverified audio asset');
              const previous = request.channel === 2 ? null : active.get(request.channel);
              if (previous && previous !== player) await previous.stop({ fadeSeconds: 0 });
              const result = await player.play({ fadeSeconds: request.fade_seconds });
              if (result?.cancelled) throw Error('audio playback cancelled');
              started.add(player);
              if (request.channel !== 2) active.set(request.channel, player);
            } else if (request.operation === 2) {
              const players =
                request.channel === 2
                  ? [...catalogue]
                      .filter(([key, player]) => key.startsWith('2:') && started.has(player))
                      .map(([, player]) => player)
                  : [active.get(request.channel)].filter(Boolean);
              const unique = [...new Set(players)],
                results = await Promise.all(
                  unique.map((player) => player.stop({ fadeSeconds: request.fade_seconds })),
                );
              if (results.some((result) => result?.cancelled)) throw Error('audio stop cancelled');
              for (const player of unique) started.delete(player);
              active.delete(request.channel);
            } else throw Error('invalid audio operation');
          } catch (error) {
            failure = String(error?.message || error);
          }
          if (exports.glue_audio_ack(request.token, failure ? 1 : 0) !== 0)
            throw Error('audio acknowledgement rejected');
          results.push({ ...request, completed: !failure, error: failure });
        }
        return results;
      } finally {
        busy = false;
      }
    },
    async close() {
      if (busy) throw Error('audio operation pending');
      closed = true;
      for (const player of started) await player.stop({ fadeSeconds: 0 });
      active.clear();
      started.clear();
    },
  };
}
