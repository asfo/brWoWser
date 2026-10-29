import { execFileSync, spawn } from 'node:child_process';
import { realpathSync } from 'node:fs';
import { fileURLToPath } from 'node:url';

const root = realpathSync(fileURLToPath(new URL('../', import.meta.url)));
const ports = [8787, 8788];

function listeners(port) {
  try {
    return execFileSync('lsof', ['-t', '-iTCP:' + port, '-sTCP:LISTEN'], {
      encoding: 'utf8',
      stdio: ['ignore', 'pipe', 'pipe'],
    })
      .trim()
      .split(/\s+/)
      .filter(Boolean)
      .map(Number);
  } catch (error) {
    if (error.status === 1) return [];
    throw Error('Cannot inspect local development port ' + port + '.');
  }
}

try {
  const owners = [...new Set(ports.flatMap(listeners))];
  for (const pid of owners) {
    const options = { encoding: 'utf8', stdio: ['ignore', 'pipe', 'pipe'] };
    const command = execFileSync('ps', ['-p', String(pid), '-o', 'args='], options);
    const directory = execFileSync('lsof', ['-a', '-p', String(pid), '-d', 'cwd', '-Fn'], options);
    const cwd = directory.match(/^n(.+)$/m)?.[1];
    const projectServer =
      command.includes('scripts/network_bridge.mjs') ||
      command.includes('app/client/server.mjs') ||
      command.includes('validation/login-integration/check_browser.mjs');
    if (!projectServer || !cwd || realpathSync(cwd) !== root)
      throw Error('A development port belongs to another process. Stop it before restarting.');
  }
  for (const pid of owners) {
    try {
      process.kill(pid, 'SIGTERM');
    } catch (error) {
      if (error.code !== 'ESRCH') throw error;
    }
  }
  for (let attempt = 0; ports.some((port) => listeners(port).length); attempt++) {
    if (attempt >= 30) throw Error('Previous development servers have not stopped yet.');
    await new Promise((resolve) => setTimeout(resolve, 100));
  }
  console.log('Starting the current web client and bridge.');
  const child = spawn(process.execPath, ['scripts/dev.mjs'], { cwd: root, stdio: 'inherit' });
  process.once('SIGINT', () => child.kill('SIGINT'));
  process.once('SIGTERM', () => child.kill('SIGTERM'));
  child.once('error', (error) => {
    console.error(error.message);
    process.exitCode = 1;
  });
  child.once('exit', (code) => {
    process.exitCode = code ?? 1;
  });
} catch (error) {
  console.error(error.message);
  process.exitCode = 1;
}
