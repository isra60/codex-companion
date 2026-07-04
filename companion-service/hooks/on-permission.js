#!/usr/bin/env node
const crypto = require('crypto');

function readStdin() {
  return new Promise((resolve) => {
    let data = '';
    process.stdin.setEncoding('utf8');
    process.stdin.on('data', (chunk) => { data += chunk; });
    process.stdin.on('end', () => { resolve(data); });
  });
}

function postToCompanion(path, body) {
  return new Promise((resolve, reject) => {
    const http = require('http');
    const payload = JSON.stringify(body);
    const req = http.request({
      hostname: '127.0.0.1',
      port: Number(process.env.COMPANION_HTTP_PORT || 3120),
      path,
      method: 'POST',
      timeout: 115_000,
      headers: {
        'Content-Type': 'application/json',
        'Content-Length': Buffer.byteLength(payload)
      }
    }, (res) => {
      let data = '';
      res.on('data', (chunk) => { data += chunk; });
      res.on('end', () => {
        try { resolve(JSON.parse(data)); } catch { resolve({ decision: 'deny', reason: data || 'Invalid companion response' }); }
      });
    });
    req.on('timeout', () => {
      req.destroy(new Error('Companion request timed out'));
    });
    req.on('error', reject);
    req.write(payload);
    req.end();
  });
}

async function main() {
  const input = JSON.parse(await readStdin());
  const requestId = crypto.randomUUID();
  const result = await postToCompanion('/decision', {
    type: 'permission_request',
    request_id: requestId,
    session_id: input.session_id,
    tool_name: input.tool_name,
    tool_use_id: input.tool_use_id,
    tool_input: input.tool_input,
    cwd: input.cwd,
    model: input.model,
    timestamp: new Date().toISOString()
  });

  if (result.decision === 'allow') {
    console.log(JSON.stringify({
      hookSpecificOutput: {
        permissionDecision: 'allow',
        additionalContext: 'Approved from Codex Companion device'
      }
    }));
    process.exit(0);
  }

  console.error(result.reason || 'Denied from Codex Companion');
  process.exit(2);
}

main().catch((err) => {
  console.error(`Companion error: ${err.message}. Denying for safety.`);
  process.exit(2);
});
