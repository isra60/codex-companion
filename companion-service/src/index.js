import crypto from 'node:crypto';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

import { createHttpServer } from './http-server.js';
import { createMdnsAdvertiser } from './mdns-advertiser.js';
import { SessionState } from './session-state.js';
import { createWebSocketServer } from './websocket-server.js';
import { EventLog } from './event-log.js';

const HTTP_PORT = Number(process.env.COMPANION_HTTP_PORT || 3120);
const WS_PORT = Number(process.env.COMPANION_WS_PORT || 3121);
const DECISION_TIMEOUT_MS = Number(process.env.COMPANION_DECISION_TIMEOUT_MS || 110_000);
const HOST = process.env.COMPANION_HOST || '0.0.0.0';

const __filename = fileURLToPath(import.meta.url);
const __dirname = path.dirname(__filename);
const projectRoot = path.resolve(__dirname, '..', '..');
const dashboardDir = path.join(projectRoot, 'esp32-simulator');
const logsDir = path.join(projectRoot, 'companion-service', 'logs');
const authToken = process.env.COMPANION_AUTH_TOKEN || crypto.randomBytes(18).toString('base64url');

const sessionState = new SessionState({
  decisionTimeoutMs: DECISION_TIMEOUT_MS
});
const eventLog = new EventLog(logsDir);

const wsServer = createWebSocketServer({
  port: WS_PORT,
  host: HOST,
  sessionState,
  authToken
});
sessionState.setBroadcast(wsServer.broadcast);
sessionState.setClientCountProvider(wsServer.getConnectedClientCount);

const REQUIRE_HTTP_AUTH = process.env.COMPANION_REQUIRE_HTTP_AUTH === 'true';

const httpServer = createHttpServer({
  port: HTTP_PORT,
  host: HOST,
  dashboardDir,
  sessionState,
  eventLog,
  authToken,
  requireHttpAuth: REQUIRE_HTTP_AUTH,
  getDevices: wsServer.getDevices
});

let mdnsAdvertiser = null;
try {
  mdnsAdvertiser = createMdnsAdvertiser({
    httpPort: HTTP_PORT,
    wsPort: WS_PORT
  });
} catch (error) {
  console.warn(`mDNS advertiser did not start: ${error.message}`);
}

await httpServer.start();
console.log(`Companion service running. HTTP :${HTTP_PORT}, WS :${WS_PORT}, Dashboard: http://localhost:${HTTP_PORT}/dashboard`);
console.log(`Auth token: ${authToken}`);
console.log('Set COMPANION_AUTH_TOKEN to reuse a stable token.');

function shutdown(signal) {
  console.log(`Received ${signal}; shutting down companion service.`);
  Promise.allSettled([
    httpServer.stop(),
    wsServer.stop(),
    mdnsAdvertiser?.stop?.()
  ]).finally(() => process.exit(0));
}

process.on('SIGINT', () => shutdown('SIGINT'));
process.on('SIGTERM', () => shutdown('SIGTERM'));
