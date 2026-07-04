import { WebSocketServer, WebSocket } from 'ws';

export function createWebSocketServer({
  port,
  host,
  sessionState,
  authToken
}) {
  const wss = new WebSocketServer({ port, host });
  const clients = new Set();

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

      if (message.type === 'ping') {
        send(ws, { type: 'pong' });
      }
    });

    ws.on('close', () => {
      clearTimeout(authTimer);
      clients.delete(ws);
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
    stop() {
      return new Promise((resolve) => wss.close(() => resolve()));
    }
  };
}
