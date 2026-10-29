import { playMedia, cancelPendingMedia } from '../media/autoplay.mjs';
// Browser boundary, not a reconstruction of FMOD mixing or default fades.
export function createMediaPlayer(element, { defaultFadeSeconds = 0 } = {}) {
  if (!Number.isFinite(defaultFadeSeconds) || defaultFadeSeconds < 0 || defaultFadeSeconds > 60)
    throw Error('invalid default fade');
  let context,
    source,
    gain,
    analyser,
    volume = 0.4,
    closed = false,
    generation = 0;
  const fade = (v) => {
    const seconds = v === -1 ? defaultFadeSeconds : v;
    if (!Number.isFinite(seconds) || seconds < 0 || seconds > 60) throw Error('invalid fade');
    return seconds;
  };
  const ensure = () => {
    if (closed) throw Error('closed audio player');
    if (context) return;
    context = new AudioContext();
    source = context.createMediaElementSource(element);
    gain = context.createGain();
    gain.gain.value = volume;
    analyser = context.createAnalyser();
    source.connect(gain);
    gain.connect(analyser);
    analyser.connect(context.destination);
  };
  return {
    async play({ fadeSeconds = 0 } = {}) {
      const seconds = fade(fadeSeconds);
      ensure();
      const mine = ++generation;
      if (context.state !== 'running') context.resume().catch(() => {});
      if (mine !== generation) return { cancelled: true };
      const now = context.currentTime;
      gain.gain.cancelScheduledValues(now);
      gain.gain.setValueAtTime(seconds ? 0 : volume, now);
      await playMedia(element, context);
      if (mine !== generation) return { cancelled: true };
      if (seconds) gain.gain.linearRampToValueAtTime(volume, context.currentTime + seconds);
      return { cancelled: false };
    },
    async stop({ fadeSeconds = 0 } = {}) {
      cancelPendingMedia(element);
      const seconds = fade(fadeSeconds),
        mine = ++generation;
      if (closed) throw Error('closed audio player');
      if (seconds && context && !element.paused) {
        const end = context.currentTime + seconds;
        gain.gain.cancelScheduledValues(context.currentTime);
        gain.gain.setValueAtTime(gain.gain.value, context.currentTime);
        gain.gain.linearRampToValueAtTime(0, end);
        await new Promise((resolve) => {
          const check = () => {
            if (mine !== generation || context.currentTime >= end) resolve();
            else setTimeout(check, 20);
          };
          check();
        });
      }
      if (mine !== generation) return { cancelled: true };
      element.pause();
      element.currentTime = 0;
      return { cancelled: false };
    },
    setVolume(value) {
      if (!Number.isFinite(value) || value < 0 || value > 1) throw Error('invalid volume');
      volume = value;
      if (gain) {
        gain.gain.cancelScheduledValues(context.currentTime);
        gain.gain.setValueAtTime(value, context.currentTime);
      }
    },
    metrics() {
      const samples = new Float32Array(analyser?.fftSize || 1);
      analyser?.getFloatTimeDomainData(samples);
      let energy = 0,
        peak = 0;
      for (const sample of samples) {
        energy += sample * sample;
        peak = Math.max(peak, Math.abs(sample));
      }
      return {
        paused: element.paused,
        current_time: element.currentTime,
        loop: element.loop,
        context_state: context?.state || 'not-created',
        volume,
        rms: Math.sqrt(energy / samples.length),
        peak,
        default_fade_seconds: defaultFadeSeconds,
      };
    },
    async dispose() {
      cancelPendingMedia(element);
      closed = true;
      ++generation;
      element.pause();
      source?.disconnect();
      gain?.disconnect();
      analyser?.disconnect();
      if (context) await context.close();
    },
  };
}
