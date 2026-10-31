import { listen, send } from '../../engine/workers/environment.mjs';
import { openWorldUI } from '../../runtime/ui/world_session.mjs';
let session,
  queue = Promise.resolve();
listen((message) => {
  queue = queue.then(async () => {
    try {
      if (message.kind === 'open') {
        if (session) throw Error('World UI session already opened');
        session = await openWorldUI(message.args);
      } else if (message.kind === 'resize') {
        if (!session) throw Error('World UI session is not ready');
        session.resize(message.width, message.height);
      } else if (message.kind === 'close') {
        session?.dispose();
        session = null;
        send({ id: message.id, result: null });
        return;
      } else throw Error('Unsupported world UI operation');
      send({ id: message.id, result: session.snapshot() });
    } catch (error) {
      send({ id: message.id, error: error.message });
    }
  });
});
