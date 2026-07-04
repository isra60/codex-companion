#!/usr/bin/env node
function readStdin() {
  return new Promise((resolve) => {
    let data = '';
    process.stdin.setEncoding('utf8');
    process.stdin.on('data', (chunk) => { data += chunk; });
    process.stdin.on('end', () => { resolve(data); });
  });
}

function postToCompanion(path, body) {
  return new Promise((resolve) => {
    const http = require('http');
    const payload = JSON.stringify(body);
    const req = http.request({
      hostname: '127.0.0.1',
      port: Number(process.env.COMPANION_HTTP_PORT || 3120),
      path,
      method: 'POST',
      headers: {
        'Content-Type': 'application/json',
        'Content-Length': Buffer.byteLength(payload)
      }
    }, (res) => {
      let data = '';
      res.on('data', (chunk) => { data += chunk; });
      res.on('end', () => {
        try { resolve(JSON.parse(data)); } catch { resolve({ raw: data }); }
      });
    });
    req.on('error', (err) => {
      console.error(`Companion not reachable: ${err.message}`);
      resolve({ ok: false });
    });
    req.write(payload);
    req.end();
  });
}

async function main() {
  const input = JSON.parse(await readStdin());
  await postToCompanion('/event', {
    type: 'session_start',
    session_id: input.session_id,
    cwd: input.cwd,
    model: input.model,
    permission_mode: input.permission_mode,
    timestamp: new Date().toISOString()
  });
  process.exit(0);
}

main().catch(() => process.exit(0));
