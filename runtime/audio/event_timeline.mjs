// Selected local sequence0 events. Manual seeks reset the anchor and never replay old cues.
export function createEventTimeline(events, duration = 66667) {
  if (
    !Number.isInteger(duration) ||
    duration <= 0 ||
    events.some((e) => !Number.isInteger(e.time_ms) || e.time_ms < 0 || e.time_ms >= duration)
  )
    throw Error('invalid sound event timeline');
  const ordered = [...events].sort((a, b) => a.time_ms - b.time_ms);
  let previous = null;
  const valid = (s, g) => {
    if (!Number.isInteger(s) || s < 0 || s >= duration || !Number.isInteger(g) || g < 0)
      throw Error('invalid event clocks');
  };
  return {
    sync(sequenceMs, globalMs) {
      valid(sequenceMs, globalMs);
      previous = { s: sequenceMs, g: globalMs };
    },
    advance(sequenceMs, globalMs) {
      valid(sequenceMs, globalMs);
      if (!previous) {
        previous = { s: sequenceMs, g: globalMs };
        return [];
      }
      const old = previous;
      previous = { s: sequenceMs, g: globalMs };
      const elapsed = globalMs - old.g;
      if (elapsed < 0 || elapsed >= duration || (old.s + elapsed) % duration !== sequenceMs)
        return [];
      return ordered
        .filter((e) => {
          let distance = e.time_ms - old.s;
          if (distance <= 0) distance += duration;
          return distance <= elapsed;
        })
        .sort(
          (a, b) =>
            ((a.time_ms - old.s + duration) % duration) -
            ((b.time_ms - old.s + duration) % duration),
        );
    },
  };
}
