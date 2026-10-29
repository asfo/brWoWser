import { spawn } from 'node:child_process';
import { once } from 'node:events';

const children = new Set();
let stopping = false;
function stop(code = 0) {
  if (stopping) return;
  stopping = true;
  process.exitCode = code;
  for (const child of children) child.kill('SIGTERM');
}
process.once('SIGINT', () => stop());
process.once('SIGTERM', () => stop());
function start(script, args = []) {
  const child = spawn(process.execPath, [script, ...args], { stdio: 'inherit' });
  children.add(child);
  child.once('exit', () => children.delete(child));
  child.once('error', (error) => {
    console.error(error.message);
    stop(1);
  });
  return child;
}
async function bridgeReady() {
  let response;
  try {
    response = await fetch('http://127.0.0.1:8788/health', { signal: AbortSignal.timeout(500) });
  } catch {
    return false;
  }
  const status = await response.json();
  if (
    !response.ok ||
    status.status !== 'BRIDGE_READY' ||
    !status.routes?.includes('/auth') ||
    status.protocol !== 2
  )
    throw Error('The local bridge on port 8788 needs restarting with npm run bridge');
  const expectedHost = process.env.BRWOWSER_AUTH_HOST || '127.0.0.1';
  const expectedPort = Number(process.env.BRWOWSER_AUTH_PORT || 3724);
  if (status.upstream?.host !== expectedHost || status.upstream?.port !== expectedPort)
    throw Error('Bridge upstream differs from this configuration. Run npm run dev:restart.');
  return true;
}
try {
  const build = start('tools/build.mjs');
  const [code] = await once(build, 'exit');
  if (code !== 0) throw Error('WASM build failed');
  if (!stopping) {
    if (!(await bridgeReady())) {
      const bridge = start('scripts/network_bridge.mjs');
      bridge.once('exit', (code) => {
        if (!stopping) stop(code || 1);
      });
      let ready = false;
      for (let attempt = 0; attempt < 40 && !stopping; attempt++) {
        if (await bridgeReady()) {
          ready = true;
          break;
        }
        await new Promise((resolve) => setTimeout(resolve, 100));
      }
      if (!ready) throw Error('Local authentication bridge did not start');
    } else console.log('Using the existing local authentication bridge.');
    if (!stopping) {
      const web = start('app/client/server.mjs', [
        '--startup',
        '--local-data',
        '--serve',
        ...(process.argv.includes('--fresh') ? ['--fresh'] : []),
      ]);
      web.once('exit', (code) => stop(code || 0));
    }
  }
} catch (error) {
  console.error(error.message);
  stop(1);
}
