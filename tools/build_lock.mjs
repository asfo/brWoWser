import { open, readFile, stat, unlink, mkdir } from 'node:fs/promises';
import { readFileSync, unlinkSync } from 'node:fs';

// One build owns generated WASM outputs at a time. A cancelled process cannot
// leave a permanent lock; a later invocation recovers a dead owner's file.
export async function acquireBuildLock(root, cancelled) {
  const path = new URL('.local/build.lock', root);
  await mkdir(new URL('.local/', root), { recursive: true });
  let announced = 0;
  while (!cancelled()) {
    try {
      const file = await open(path, 'wx');
      await file.writeFile(JSON.stringify({ pid: process.pid }) + '\n');
      await file.close();
      let released = false;
      const release = () => {
        if (released) return;
        released = true;
        try {
          if (JSON.parse(readFileSync(path, 'utf8')).pid === process.pid) unlinkSync(path);
        } catch (error) {
          if (error.code !== 'ENOENT') console.error('Build lock cleanup: ' + error.message);
        }
        process.removeListener('exit', release);
      };
      process.once('exit', release);
      return release;
    } catch (error) {
      if (error.code !== 'EEXIST') throw error;
    }
    let stale = false;
    try {
      const { pid } = JSON.parse(await readFile(path, 'utf8'));
      if (!Number.isInteger(pid) || pid < 1) throw Error('Invalid build owner');
      try {
        process.kill(pid, 0);
      } catch (error) {
        if (error.code === 'ESRCH') stale = true;
        else if (error.code !== 'EPERM') throw error;
      }
    } catch (error) {
      if (error.code === 'ENOENT') continue;
      try {
        stale = Date.now() - (await stat(path)).mtimeMs > 10000;
      } catch (error) {
        if (error.code === 'ENOENT') continue;
        throw error;
      }
    }
    if (stale) {
      try {
        await unlink(path);
      } catch (error) {
        if (error.code !== 'ENOENT') throw error;
      }
      continue;
    }
    if (Date.now() - announced >= 15000) {
      console.log('Another WASM build is running; waiting for its outputs...');
      announced = Date.now();
    }
    await new Promise((resolve) => setTimeout(resolve, 250));
  }
  return null;
}
