import assert from 'node:assert/strict';
import { mkdirSync, readFileSync, writeFileSync } from 'node:fs';
import { basename } from 'node:path';
import { fileURLToPath } from 'node:url';
import { transformSync } from 'rolldown/utils';

// Compile the actual production JSX and its JSX children for focused SSR checks.
// Plain typed contract/controller modules remain the actual source imports.
export function launcherRenderModule(source, cache) {
  mkdirSync(cache, { recursive: true });
  const output = new URL(basename(fileURLToPath(source)).replace(/\.tsx$/, '.mjs'), cache);
  const transformed = transformSync(fileURLToPath(source), readFileSync(source, 'utf8'));
  assert.equal(transformed.errors.length, 0);
  const code = transformed.code.replace(/import ['"]\.\/[^'"]+\.css['"];?/g, '')
    .replace(/from (['"])(\.\/[^'"]+)\1/g, (_, _quote, path) => {
      const dependency = new URL(path, source);
      return 'from ' + JSON.stringify(path.endsWith('.tsx') ? launcherRenderModule(dependency, cache).href : dependency.href);
    });
  writeFileSync(output, code);
  return output;
}
