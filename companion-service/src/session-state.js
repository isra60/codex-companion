const MAX_HISTORY = 100;

export class SessionState {
  constructor({ decisionTimeoutMs }) {
    this.state = 'idle';
    this.currentSession = null;
    this.lastEvent = null;
    this.pendingDecisions = new Map();
    this.eventHistory = [];
    this.decisionTimeoutMs = decisionTimeoutMs;
    this.broadcast = () => {};
    this.getClientCount = () => 0;
  }

  setBroadcast(broadcastFn) {
    this.broadcast = broadcastFn;
  }

  setClientCountProvider(provider) {
    this.getClientCount = provider;
  }

  getConnectedClientCount() {
    return this.getClientCount();
  }

  handleConnectionCountChange(count) {
    if (count === 0 && !this.currentSession && this.pendingDecisions.size === 0) {
      this.state = 'disconnected';
    } else if (this.state === 'disconnected') {
      this.state = this.currentSession ? 'session_active' : 'idle';
    }
    this.broadcast(this.getStateMessage());
  }

  getStateMessage() {
    return {
      type: 'state',
      state: this.state,
      session: this.currentSession,
      connected_clients: this.getConnectedClientCount()
    };
  }

  getPendingDecisionMessages() {
    return [...this.pendingDecisions.values()].map((entry) => this.buildPermissionMessage(entry.data));
  }

  handleEvent(eventData) {
    const event = {
      ...eventData,
      timestamp: eventData.timestamp || new Date().toISOString()
    };

    this.lastEvent = event;

    if (event.type === 'session_start') {
      this.currentSession = {
        session_id: event.session_id,
        model: event.model || null,
        cwd: event.cwd || null,
        permission_mode: event.permission_mode || null,
        started_at: event.timestamp
      };
      this.eventHistory = [];
      this.addHistory(event);
      this.state = 'session_active';
      this.broadcast(this.getStateMessage());
      this.broadcast({ type: 'event', event: 'session_start', detail: event.cwd || '', timestamp: event.timestamp });
      return;
    }

    if (event.type === 'user_prompt') {
      this.addHistory(event);
      this.state = 'working';
      this.broadcast(this.getStateMessage());
      this.broadcast({ type: 'event', event: 'user_prompt', detail: event.prompt_text || '', timestamp: event.timestamp });
      return;
    }

    if (event.type === 'post_tool_use') {
      this.addHistory(event);
      this.state = this.pendingDecisions.size > 0 ? 'permission_required' : 'working';
      this.broadcast(this.getStateMessage());
      this.broadcast({
        type: 'event',
        event: 'tool_done',
        tool_name: event.tool_name,
        detail: formatToolDetail(event),
        timestamp: event.timestamp
      });
      return;
    }

    if (event.type === 'stop') {
      this.addHistory(event);
      this.state = 'done';
      this.broadcast(this.getStateMessage());
      this.broadcast(this.buildSummary(event.timestamp));
      return;
    }

    this.addHistory(event);
    this.broadcast({
      type: 'event',
      event: event.type,
      detail: formatToolDetail(event),
      timestamp: event.timestamp
    });
  }

  registerDecision(requestId, data) {
    const timestamped = {
      ...data,
      timestamp: data.timestamp || new Date().toISOString()
    };
    this.addHistory(timestamped);

    return new Promise((resolve) => {
      const timer = setTimeout(() => {
        this.pendingDecisions.delete(requestId);
        const result = {
          decision: 'deny',
          reason: `Timed out after ${Math.round(this.decisionTimeoutMs / 1000)}s`
        };
        this.updateStateAfterDecision();
        this.broadcast({
          type: 'event',
          event: 'permission_resolved',
          request_id: requestId,
          action: 'deny',
          reason: 'timeout',
          timestamp: new Date().toISOString()
        });
        this.broadcast({
          type: 'permission_dismissed',
          request_id: requestId,
          action: 'deny',
          reason: 'timeout',
          timestamp: new Date().toISOString()
        });
        resolve(result);
      }, this.decisionTimeoutMs);

      this.pendingDecisions.set(requestId, {
        data: timestamped,
        resolve,
        timer
      });

      this.state = 'permission_required';
      this.broadcast(this.getStateMessage());
      this.broadcast(this.buildPermissionMessage(timestamped));
    });
  }

  resolveDecision(requestId, action) {
    if (action !== 'allow' && action !== 'deny') {
      console.warn(`Ignoring invalid decision action: ${action}`);
      return;
    }

    const pending = this.pendingDecisions.get(requestId);
    if (!pending) {
      console.warn(`No pending decision found for request_id ${requestId}`);
      return;
    }

    clearTimeout(pending.timer);
    this.pendingDecisions.delete(requestId);
    const result = action === 'allow'
      ? { decision: 'allow' }
      : { decision: 'deny', reason: 'Denied from Codex Companion' };

    pending.resolve(result);
    this.updateStateAfterDecision();
    this.broadcast({
      type: 'event',
      event: 'permission_resolved',
      request_id: requestId,
      action,
      timestamp: new Date().toISOString()
    });
    this.broadcast({
      type: 'permission_dismissed',
      request_id: requestId,
      action,
      timestamp: new Date().toISOString()
    });
  }

  updateStateAfterDecision() {
    this.state = this.pendingDecisions.size > 0 ? 'permission_required' : 'working';
    this.broadcast(this.getStateMessage());
  }

  addHistory(event) {
    this.eventHistory.push(event);
    if (this.eventHistory.length > MAX_HISTORY) {
      this.eventHistory.shift();
    }
  }

  buildPermissionMessage(data) {
    return {
      type: 'permission',
      request_id: data.request_id,
      tool_name: data.tool_name,
      command: extractCommand(data.tool_input),
      file_path: extractFilePath(data.tool_input),
      cwd: data.cwd || null,
      message: buildHumanMessage(data),
      timeout_s: Math.round(this.decisionTimeoutMs / 1000),
      timestamp: data.timestamp
    };
  }

  buildSummary(timestamp) {
    const files = new Set();
    const commands = new Set();

    for (const event of this.eventHistory) {
      const filePath = extractFilePath(event.tool_input);
      const command = extractCommand(event.tool_input);
      if (filePath) files.add(filePath);
      if (command) commands.add(command);
    }

    const startedAt = this.currentSession?.started_at ? Date.parse(this.currentSession.started_at) : NaN;
    const finishedAt = timestamp ? Date.parse(timestamp) : Date.now();
    const duration = Number.isFinite(startedAt) ? Math.max(0, Math.round((finishedAt - startedAt) / 1000)) : 0;

    return {
      type: 'summary',
      session_id: this.currentSession?.session_id || null,
      events_count: this.eventHistory.length,
      files_modified: [...files],
      commands_run: [...commands],
      duration_s: duration
    };
  }
}

export function formatToolDetail(eventData) {
  if (eventData.tool_name === 'Bash') {
    return eventData.tool_input?.command || '';
  }
  if (['Write', 'Edit', 'MultiEdit', 'apply_patch'].includes(eventData.tool_name)) {
    return extractFilePath(eventData.tool_input) || '';
  }
  if (eventData.prompt_text) {
    return eventData.prompt_text;
  }
  return safeJson(eventData.tool_input).slice(0, 160);
}

export function extractCommand(toolInput) {
  return toolInput?.command || null;
}

export function extractFilePath(toolInput) {
  return toolInput?.file_path || toolInput?.filename || toolInput?.path || null;
}

export function buildHumanMessage(data) {
  if (data.tool_name === 'Bash') {
    return `Ejecutar comando: ${data.tool_input?.command || '(sin comando)'}`;
  }
  if (['Write', 'Edit', 'MultiEdit', 'apply_patch'].includes(data.tool_name)) {
    return `Modificar fichero: ${extractFilePath(data.tool_input) || '(sin ruta)'}`;
  }
  return `Usar herramienta: ${data.tool_name}`;
}

function safeJson(value) {
  try {
    return JSON.stringify(value ?? {});
  } catch {
    return String(value ?? '');
  }
}
