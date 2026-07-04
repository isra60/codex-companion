import express from 'express';
import http from 'node:http';
import path from 'node:path';

export function createHttpServer({
  port,
  host,
  dashboardDir,
  sessionState,
  eventLog
}) {
  const app = express();
  const server = http.createServer(app);

  app.use(express.json({ limit: '2mb' }));

  app.get('/health', (_req, res) => {
    res.json({
      status: 'ok',
      connected_clients: sessionState.getConnectedClientCount(),
      active_session: sessionState.currentSession?.session_id || null,
      pending_decisions: sessionState.pendingDecisions.size
    });
  });

  app.post('/event', async (req, res) => {
    const body = req.body;
    if (!body || typeof body.type !== 'string' || typeof body.session_id !== 'string') {
      return res.status(400).json({ ok: false, error: 'type and session_id are required' });
    }

    sessionState.handleEvent(body);
    try {
      await eventLog.append(body.session_id, body);
    } catch (error) {
      console.warn(`Failed to append event log: ${error.message}`);
    }

    return res.json({ ok: true });
  });

  app.post('/decision', async (req, res) => {
    const body = req.body;
    if (!body || body.type !== 'permission_request' || !body.request_id || !body.tool_name) {
      return res.status(400).json({
        decision: 'deny',
        reason: 'type, request_id, and tool_name are required'
      });
    }

    try {
      await eventLog.append(body.session_id || 'unknown-session', body);
      const result = await sessionState.registerDecision(body.request_id, body);
      await eventLog.append(body.session_id || 'unknown-session', {
        type: 'permission_resolved',
        request_id: body.request_id,
        tool_name: body.tool_name,
        decision: result.decision,
        reason: result.reason || null,
        timestamp: new Date().toISOString()
      });
      return res.json(result);
    } catch (error) {
      return res.json({
        decision: 'deny',
        reason: error.message || 'Companion decision failed'
      });
    }
  });

  app.get('/', (_req, res) => {
    res.redirect('/dashboard');
  });
  app.get('/dashboard', (_req, res) => {
    res.sendFile(path.join(dashboardDir, 'index.html'));
  });
  app.use('/dashboard', express.static(dashboardDir));

  return {
    start() {
      return new Promise((resolve, reject) => {
        server.once('error', reject);
        server.listen(port, host, () => {
          server.off('error', reject);
          resolve();
        });
      });
    },
    stop() {
      return new Promise((resolve) => server.close(() => resolve()));
    }
  };
}
