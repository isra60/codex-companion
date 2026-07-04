import http from 'node:http';
import { WebSocket } from 'ws';

const HTTP_PORT = Number(process.env.COMPANION_HTTP_PORT || 3120);
const WS_PORT = Number(process.env.COMPANION_WS_PORT || 3121);
const TOKEN = process.env.COMPANION_AUTH_TOKEN || process.argv[2];

if (!TOKEN) {
  console.error('Usage: COMPANION_AUTH_TOKEN=<token> npm run smoke');
  console.error('Or: npm run smoke -- <token>');
  process.exit(2);
}

const sessionId = `smoke-${Date.now()}`;
const sleep = (ms) => new Promise((resolve) => setTimeout(resolve, ms));

function postJson(path, body) {
  return new Promise((resolve, reject) => {
    const payload = JSON.stringify(body);
    const req = http.request({
      hostname: '127.0.0.1',
      port: HTTP_PORT,
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
        try {
          resolve(JSON.parse(data));
        } catch {
          resolve({ raw: data });
        }
      });
    });
    req.on('error', reject);
    req.write(payload);
    req.end();
  });
}

function connectDashboardClient() {
  return new Promise((resolve, reject) => {
    const ws = new WebSocket(`ws://127.0.0.1:${WS_PORT}`);
    const timeout = setTimeout(() => reject(new Error('WebSocket auth timed out')), 5000);

    ws.on('open', () => {
      ws.send(JSON.stringify({ type: 'auth', token: TOKEN }));
    });

    ws.on('message', (raw) => {
      const message = JSON.parse(raw.toString());
      if (message.type === 'auth' && message.ok) {
        clearTimeout(timeout);
        resolve(ws);
      }
    });

    ws.on('error', reject);
  });
}

async function main() {
  const ws = await connectDashboardClient();
  ws.on('message', (raw) => {
    const message = JSON.parse(raw.toString());
    if (message.type === 'permission') {
      console.log(`permission: ${message.tool_name} ${message.command || message.message}`);
      setTimeout(() => {
        ws.send(JSON.stringify({
          type: 'action',
          request_id: message.request_id,
          action: 'allow'
        }));
      }, 1500);
    }
  });

  console.log(`session: ${sessionId}`);
  await postJson('/event', {
    type: 'session_start',
    session_id: sessionId,
    cwd: 'C:/demo/codex-companion',
    model: 'smoke-test',
    permission_mode: 'default',
    timestamp: new Date().toISOString()
  });
  await sleep(700);

  await postJson('/event', {
    type: 'user_prompt',
    session_id: sessionId,
    prompt_text: 'Run Codex Companion smoke test',
    timestamp: new Date().toISOString()
  });
  await sleep(700);

  await postJson('/event', {
    type: 'post_tool_use',
    session_id: sessionId,
    tool_name: 'Bash',
    tool_input: { command: 'npm test' },
    timestamp: new Date().toISOString()
  });
  await sleep(700);

  const decision = await postJson('/decision', {
    type: 'permission_request',
    request_id: `req-${Date.now()}`,
    session_id: sessionId,
    tool_name: 'Bash',
    tool_input: { command: 'git status --short' },
    timestamp: new Date().toISOString()
  });
  console.log(`decision: ${decision.decision}`);
  await sleep(700);

  await postJson('/event', {
    type: 'stop',
    session_id: sessionId,
    timestamp: new Date().toISOString()
  });

  await sleep(500);
  ws.close();
}

main().catch((error) => {
  console.error(error.message);
  process.exit(1);
});
