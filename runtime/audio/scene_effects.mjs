import { resumeContext } from '../media/autoplay.mjs';
import { createEventTimeline } from './event_timeline.mjs';
// decodeAudioData requires an owned ArrayBuffer and may detach it. Preserve
// the exact byte window of worker/WASM views without detaching their backing store.
export function copyAudioBytes(bytes) {
  const view =
    bytes instanceof ArrayBuffer
      ? new Uint8Array(bytes)
      : ArrayBuffer.isView(bytes)
        ? new Uint8Array(bytes.buffer, bytes.byteOffset, bytes.byteLength)
        : null;
  if (!view || !view.byteLength) throw Error('Invalid encoded audio bytes');
  const owned = new Uint8Array(view.byteLength);
  owned.set(view);
  return owned.buffer;
}
export async function createSceneEffects(clips) {
  const decoder = new OfflineAudioContext(2, 1, 48000),
    decoded = await Promise.all(
      clips.map(async (c) => ({
        metadata: c.metadata,
        buffer: await decoder.decodeAudioData(copyAudioBytes(c.bytes)),
      })),
    ),
    timeline = createEventTimeline(decoded.map((c) => c.metadata));
  let context,
    master,
    analyser,
    volume = 0.4,
    paused = true,
    disposed = false,
    playGeneration = 0,
    played = [],
    voices = [];
  function ensure() {
    if (!context) {
      context = new AudioContext();
      master = context.createGain();
      master.gain.value = volume;
      analyser = context.createAnalyser();
      analyser.fftSize = 2048;
      master.connect(analyser);
      analyser.connect(context.destination);
    }
  }
  function startVoice(voice) {
    if (voice.offset >= voice.clip.buffer.duration) return;
    const source = context.createBufferSource(),
      gain = context.createGain();
    source.buffer = voice.clip.buffer;
    gain.gain.value = voice.clip.metadata.kit_volume;
    source.connect(gain);
    gain.connect(master);
    voice.source = source;
    voice.gain = gain;
    voice.at = context.currentTime;
    source.onended = () => {
      if (voice.source === source) {
        voice.source = null;
        voice.offset = voice.clip.buffer.duration;
      }
      source.disconnect();
      gain.disconnect();
    };
    source.start(0, voice.offset);
  }
  function pause() {
    ++playGeneration;
    paused = true;
    for (const v of voices)
      if (v.source) {
        v.offset = Math.min(v.clip.buffer.duration, v.offset + context.currentTime - v.at);
        const source = v.source;
        v.source = null;
        source.stop();
        source.disconnect();
        v.gain.disconnect();
      }
  }
  function clear() {
    pause();
    voices = [];
  }
  return {
    async play() {
      if (disposed) throw Error('effects disposed');
      const mine = ++playGeneration;
      ensure();
      resumeContext(context);
      if (mine !== playGeneration) return;
      paused = false;
      voices = voices.filter((v) => v.offset < v.clip.buffer.duration);
      for (const voice of voices) if (!voice.source) startVoice(voice);
    },
    pause,
    stop: clear,
    sync(s, g) {
      timeline.sync(s, g);
    },
    seek(s, g) {
      clear();
      timeline.sync(s, g);
    },
    frame(s, g) {
      const events = timeline.advance(s, g);
      if (paused || context?.state !== 'running') return;
      for (const event of events) {
        const clip = decoded.find((c) => c.metadata.id === event.id),
          voice = { clip, offset: ((s - event.time_ms + 66667) % 66667) / 1000, source: null };
        voices = voices.filter((v) => v.offset < v.clip.buffer.duration);
        voices.push(voice);
        startVoice(voice);
        played.push({
          id: event.id,
          time_ms: event.time_ms,
          observed_sequence_ms: s,
          global_ms: g,
        });
      }
    },
    setVolume(v) {
      if (!Number.isFinite(v) || v < 0 || v > 1) throw Error('invalid effect volume');
      volume = v;
      if (master) master.gain.setValueAtTime(v, context.currentTime);
    },
    metrics() {
      const samples = new Float32Array(analyser?.fftSize ?? 1);
      analyser?.getFloatTimeDomainData(samples);
      const rms = Math.sqrt(samples.reduce((sum, x) => sum + x * x, 0) / samples.length);
      return {
        rms,
        paused,
        context_state: context?.state ?? 'not-created',
        volume,
        played: [...played],
        active_voices: voices.filter((v) => v.source).length,
        decoded: decoded.map((c) => ({
          id: c.metadata.id,
          duration: c.buffer.duration,
          sample_rate: c.buffer.sampleRate,
        })),
        scope:
          'Selected original cue times and files. Browser timeline, pause/resume and 2D mix policy; no native scheduler, spatial attenuation or output equivalence.',
      };
    },
    async dispose() {
      disposed = true;
      clear();
      if (context) await context.close();
    },
  };
}
