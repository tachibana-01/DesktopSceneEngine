import { statSync } from 'node:fs';
import { join } from 'node:path';

function isFile(path) {
  return statSync(path, { throwIfNoEntry: false })?.isFile();
}

function findChromeExecutable() {
  if (process.env.CHROME_PATH) {
    if (!isFile(process.env.CHROME_PATH)) throw new Error('CHROME_PATH must point to a browser executable.');
    return process.env.CHROME_PATH;
  }

  const roots = [process.env.ProgramW6432, process.env.ProgramFiles,
    process.env['ProgramFiles(x86)'], process.env.LOCALAPPDATA];
  for (const root of roots) {
    if (!root) continue;
    const executable = join(root, 'Google', 'Chrome', 'Application', 'chrome.exe');
    if (isFile(executable)) return executable;
  }

  throw new Error('Chrome was not found. Set CHROME_PATH to the browser executable.');
}

export const chromeExecutable = findChromeExecutable();
