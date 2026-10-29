// Media policies are a browser boundary. Loading/rendering never waits for a
// gesture; blocked audible playback resumes on the first real interaction.
const pending = new Set(),
  mediaPending = new WeakMap();
let installed = false;
function install() {
  if (installed || typeof window === 'undefined') return;
  installed = true;
  const unlock = () => {
    for (const resume of [...pending]) resume();
  };
  window.addEventListener('pointerdown', unlock, { capture: true });
  window.addEventListener('keydown', unlock, { capture: true });
}
export async function playMedia(element, context) {
  install();
  cancelPendingMedia(element);
  if (context?.state !== 'running') context?.resume().catch(() => {});
  try {
    await element.play();
    return { deferred: false };
  } catch (error) {
    if (error.name !== 'NotAllowedError') throw error;
    const resume = () => {
      pending.delete(resume);
      mediaPending.delete(element);
      if (context?.state !== 'running') context?.resume().catch(() => {});
      element.play().catch((e) => {
        if (e.name === 'NotAllowedError') {
          pending.add(resume);
          mediaPending.set(element, resume);
        }
      });
    };
    pending.add(resume);
    mediaPending.set(element, resume);
    return { deferred: true };
  }
}
export async function playMovieMedia(video) {
  install();
  cancelPendingMedia(video);
  try {
    await video.play();
    return;
  } catch (error) {
    if (error.name !== 'NotAllowedError') throw error;
  }
  video.muted = true;
  await video.play();
  const unmute = () => {
    pending.delete(unmute);
    mediaPending.delete(video);
    video.muted = false;
  };
  pending.add(unmute);
  mediaPending.set(video, unmute);
}

export function resumeContext(context) {
  install();
  if (context.state === 'running' || context.state === 'closed') return;
  context.resume().catch(() => {});
  const resume = () => {
    pending.delete(resume);
    if (context.state !== 'closed') context.resume().catch(() => {});
  };
  pending.add(resume);
}

export function cancelPendingMedia(element) {
  const resume = mediaPending.get(element);
  if (resume) {
    pending.delete(resume);
    mediaPending.delete(element);
  }
}
