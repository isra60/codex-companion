import { WebSocketServer, WebSocket } from 'ws';

export function createWebSocketServer({
  port,
  host,
  sessionState,
  authToken
}) {
  const wss = new WebSocketServer({ port, host });
  const clients = new Set();
  const devices = new Map();

  function send(ws, message) {
    if (ws.readyState === WebSocket.OPEN) {
      ws.send(JSON.stringify(message));
    }
  }

  function broadcast(message) {
    for (const ws of clients) {
      send(ws, message);
    }
  }

  function getDeviceList() {
    return [...devices.values()].sort((a, b) => b.last_seen.localeCompare(a.last_seen));
  }

  function broadcastDevices() {
    broadcast({
      type: 'devices',
      devices: getDeviceList()
    });
  }

  function broadcastConnectionState() {
    sessionState.handleConnectionCountChange(clients.size);
  }

  wss.on('connection', (ws) => {
    ws.isAuthed = false;

    const authTimer = setTimeout(() => {
      if (!ws.isAuthed) {
        ws.close(1008, 'auth required');
      }
    }, 10_000);

    ws.on('message', (raw) => {
      let message;
      try {
        message = JSON.parse(raw.toString());
      } catch {
        send(ws, { type: 'error', message: 'invalid json' });
        return;
      }

      if (!ws.isAuthed) {
        if (message.type === 'auth' && message.token === authToken) {
          clearTimeout(authTimer);
          ws.isAuthed = true;
          clients.add(ws);
          send(ws, { type: 'auth', ok: true });
          send(ws, sessionState.getStateMessage());
          send(ws, { type: 'devices', devices: getDeviceList() });
          for (const decision of sessionState.getPendingDecisionMessages()) {
            send(ws, decision);
          }
          broadcastConnectionState();
          return;
        }

        send(ws, { type: 'auth', ok: false });
        ws.close(1008, 'invalid auth token');
        return;
      }

      if (message.type === 'action') {
        sessionState.resolveDecision(message.request_id, message.action);
        return;
      }

      if (message.type === 'device_hello') {
        const id = String(message.device_id || ws._socket?.remoteAddress || 'unknown-device');
        ws.deviceId = id;
        devices.set(id, {
          id,
          name: String(message.name || 'ESP32 Companion'),
          firmware: String(message.firmware || '-'),
          protocol_version: Number(message.protocol_version || 1),
          ip: String(message.ip || normalizeRemoteAddress(ws._socket?.remoteAddress) || '-'),
          status: 'online',
          detail: String(message.detail || 'Connected'),
          connected: true,
          connected_at: new Date().toISOString(),
          last_seen: new Date().toISOString()
        });
        broadcastDevices();
        return;
      }

      if (message.type === 'device_status' && ws.deviceId) {
        const existing = devices.get(ws.deviceId);
        if (existing) {
          devices.set(ws.deviceId, {
            ...existing,
            status: String(message.status || existing.status),
            detail: String(message.detail || existing.detail),
            last_seen: new Date().toISOString()
          });
          broadcastDevices();
        }
        return;
      }

      if (message.type === 'ping') {
        if (ws.deviceId) {
          const existing = devices.get(ws.deviceId);
          if (existing) {
            devices.set(ws.deviceId, {
              ...existing,
              last_seen: new Date().toISOString()
            });
            broadcastDevices();
          }
        }
        send(ws, { type: 'pong' });
      }
    });

    ws.on('close', () => {
      clearTimeout(authTimer);
      clients.delete(ws);
      if (ws.deviceId && devices.has(ws.deviceId)) {
        const existing = devices.get(ws.deviceId);
        devices.set(ws.deviceId, {
          ...existing,
          status: 'offline',
          detail: 'Disconnected',
          connected: false,
          last_seen: new Date().toISOString()
        });
        broadcastDevices();
      }
      broadcastConnectionState();
    });
  });

  wss.on('listening', () => {
    console.log(`WebSocket listening on ${host}:${port}`);
  });

  return {
    broadcast,
    getConnectedClientCount() {
      return clients.size;
    },
    getDevices() {
      return getDeviceList();
    },
    stop() {
      return new Promise((resolve) => wss.close(() => resolve()));
    }
  };
}

function normalizeRemoteAddress(address) {
  if (!address) return null;
  return String(address).replace(/^::ffff:/, '');
}
