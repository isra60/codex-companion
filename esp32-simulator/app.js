(function () {
  const DEFAULT_DECISION_SECONDS = 110;
  const MAX_LOG_ENTRIES = 50;

  const device = document.getElementById('device');
  const authPanel = document.getElementById('authPanel');
  const tokenInput = document.getElementById('tokenInput');
  const tokenButton = document.getElementById('tokenButton');
  const authError = document.getElementById('authError');
  const mainScreen = document.getElementById('mainScreen');
  const permissionScreen = document.getElementById('permissionScreen');
  const summaryScreen = document.getElementById('summaryScreen');
  const headerLed = document.getElementById('headerLed');
  const clientCount = document.getElementById('clientCount');
  const statusIcon = document.getElementById('statusIcon');
  const statusText = document.getElementById('statusText');
  const projectName = document.getElementById('projectName');
  const modelName = document.getElementById('modelName');
  const eventCard = document.getElementById('eventCard');
  const eventTitle = document.getElementById('eventTitle');
  const eventDetail = document.getElementById('eventDetail');
  const eventTime = document.getElementById('eventTime');
  const logList = document.getElementById('logList');
  const permissionTool = document.getElementById('permissionTool');
  const permissionCommand = document.getElementById('permissionCommand');
  const permissionPath = document.getElementById('permissionPath');
  const permissionTimeout = document.getElementById('permissionTimeout');
  const allowButton = document.getElementById('allowButton');
  const denyButton = document.getElementById('denyButton');
  const summaryDuration = document.getElementById('summaryDuration');
  const summaryEvents = document.getElementById('summaryEvents');
  const summaryFiles = document.getElementById('summaryFiles');
  const summaryCommands = document.getElementById('summaryCommands');
  const muteToggle = document.getElementById('muteToggle');

  let ws = null;
  let reconnectTimer = null;
  let heartbeatTimer = null;
  let permissionTimer = null;
  let permissionQueue = [];
  let eventLog = [];
  let authed = false;
  let soundEnabled = localStorage.getItem('codexCompanionSound') !== 'false';

  const params = new URLSearchParams(window.location.search);
  const wsUrl = params.get('ws') || `ws://${window.location.hostname}:3121`;
  const tokenFromUrl = params.get('token');
  const storedToken = localStorage.getItem('codexCompanionToken');
  tokenInput.value = tokenFromUrl || storedToken || '';

  tokenButton.addEventListener('click', () => {
    const token = tokenInput.value.trim();
    if (!token) {
      authError.textContent = 'Token required';
      return;
    }
    localStorage.setItem('codexCompanionToken', token);
    authError.textContent = '';
    connect();
  });

  tokenInput.addEventListener('keydown', (event) => {
    if (event.key === 'Enter') {
      tokenButton.click();
    }
  });

  allowButton.addEventListener('click', () => sendAction('allow'));
  denyButton.addEventListener('click', () => sendAction('deny'));

  muteToggle.addEventListener('click', () => {
    soundEnabled = !soundEnabled;
    localStorage.setItem('codexCompanionSound', String(soundEnabled));
    muteToggle.textContent = soundEnabled ? '🔔' : '🔕';
    muteToggle.title = soundEnabled ? 'Notifications on' : 'Notifications muted';
  });

  // Init mute icon
  muteToggle.textContent = soundEnabled ? '🔔' : '🔕';
  muteToggle.title = soundEnabled ? 'Notifications on' : 'Notifications muted';

  if (tokenInput.value) {
    connect();
  } else {
    renderState({ state: 'disconnected' });
  }

  function connect() {
    cleanupSocket();
    authed = false;
    renderState({ state: 'disconnected' });
    ws = new WebSocket(wsUrl);

    ws.addEventListener('open', () => {
      const token = tokenInput.value.trim();
      ws.send(JSON.stringify({ type: 'auth', token }));
    });

    ws.addEventListener('message', (event) => {
      let message;
      try {
        message = JSON.parse(event.data);
      } catch {
        return;
      }
      handleMessage(message);
    });

    ws.addEventListener('close', () => {
      cleanupTimers();
      renderState({ state: 'disconnected' });
      if (authed) {
        reconnectTimer = window.setTimeout(connect, 3000);
      } else {
        authPanel.classList.remove('hidden');
      }
    });

    ws.addEventListener('error', () => {
      authError.textContent = 'Connection failed';
    });
  }

  function handleMessage(message) {
    if (message.type === 'auth') {
      if (message.ok) {
        authed = true;
        authPanel.classList.add('hidden');
        authError.textContent = '';
        startHeartbeat();
      } else {
        authed = false;
        authError.textContent = 'Invalid token';
      }
      return;
    }

    if (message.type === 'state') {
      renderState(message);
      return;
    }

    if (message.type === 'event') {
      renderEvent(message);
      addToLog(message);
      return;
    }

    if (message.type === 'permission') {
      queuePermission(message);
      addToLog({
        type: 'event',
        event: 'permission_required',
        tool_name: message.tool_name,
        detail: message.message,
        timestamp: message.timestamp
      });
      return;
    }

    if (message.type === 'permission_dismissed') {
      dismissPermission(message.request_id);
      return;
    }

    if (message.type === 'summary') {
      renderSummary(message);
      return;
    }
  }

  function renderState(data) {
    const state = data.state || 'idle';
    const labels = {
      disconnected: 'Disconnected',
      idle: 'Waiting for session',
      session_active: 'Session started',
      working: 'Codex working',
      permission_required: 'Permission required',
      done: 'Session complete',
      error: 'Error'
    };

    statusIcon.className = `status-icon ${state}`;
    statusText.textContent = labels[state] || state;
    headerLed.style.color = colorForState(state);
    headerLed.style.background = colorForState(state);
    clientCount.textContent = String(data.connected_clients ?? 0);

    if (data.session) {
      projectName.textContent = projectLabel(data.session.cwd);
      modelName.textContent = data.session.model || '-';
    }

    if (state !== 'permission_required') {
      device.classList.remove('permission-pulse');
    }
    if (state !== 'done') {
      summaryScreen.hidden = true;
      mainScreen.hidden = false;
    }
  }

  function renderEvent(data) {
    eventTitle.textContent = data.tool_name || labelEvent(data.event);
    eventDetail.textContent = data.detail || data.action || '';
    eventTime.textContent = formatTime(data.timestamp);
    eventCard.style.animation = 'none';
    eventCard.offsetHeight;
    eventCard.style.animation = '';
  }

  function queuePermission(data) {
    // Avoid duplicates in queue
    if (permissionQueue.some((p) => p.request_id === data.request_id)) {
      return;
    }
    permissionQueue.push(data);
    updatePermissionBadge();
    if (permissionQueue.length === 1) {
      showCurrentPermission();
    }
  }

  function showCurrentPermission() {
    if (permissionQueue.length === 0) {
      permissionScreen.hidden = true;
      mainScreen.hidden = false;
      device.classList.remove('permission-pulse');
      updatePermissionBadge();
      return;
    }

    const data = permissionQueue[0];
    mainScreen.hidden = true;
    summaryScreen.hidden = true;
    permissionScreen.hidden = false;
    device.classList.add('permission-pulse');
    permissionTool.textContent = data.tool_name || 'Tool';
    permissionCommand.textContent = data.command || data.message || data.file_path || 'No detail';
    permissionPath.textContent = data.file_path || data.cwd || '';
    startPermissionCountdown(data.timeout_s || DEFAULT_DECISION_SECONDS);
    playNotificationSound();
  }

  function dismissPermission(requestId) {
    const idx = permissionQueue.findIndex((p) => p.request_id === requestId);
    if (idx === -1) return;
    const wasFirst = idx === 0;
    permissionQueue.splice(idx, 1);
    updatePermissionBadge();
    if (wasFirst) {
      stopPermissionCountdown();
      showCurrentPermission();
    }
  }

  function renderSummary(data) {
    stopPermissionCountdown();
    permissionQueue = [];
    updatePermissionBadge();
    mainScreen.hidden = true;
    permissionScreen.hidden = true;
    summaryScreen.hidden = false;
    device.classList.remove('permission-pulse');
    summaryDuration.textContent = `${data.duration_s || 0}s`;
    summaryEvents.textContent = String(data.events_count || 0);
    renderList(summaryFiles, data.files_modified);
    renderList(summaryCommands, data.commands_run);
  }

  function sendAction(action) {
    if (permissionQueue.length === 0 || !ws || ws.readyState !== WebSocket.OPEN) {
      return;
    }
    const current = permissionQueue[0];
    ws.send(JSON.stringify({
      type: 'action',
      request_id: current.request_id,
      action
    }));
    stopPermissionCountdown();
    permissionQueue.shift();
    updatePermissionBadge();
    device.classList.remove('permission-pulse');
    device.classList.remove('flash-allow', 'flash-deny');
    device.offsetHeight;
    device.classList.add(action === 'allow' ? 'flash-allow' : 'flash-deny');
    showCurrentPermission();
  }

  function startPermissionCountdown(seconds) {
    stopPermissionCountdown();
    let remaining = seconds || DEFAULT_DECISION_SECONDS;
    renderCountdown(remaining);
    permissionTimer = window.setInterval(() => {
      remaining -= 1;
      renderCountdown(Math.max(0, remaining));
      if (remaining <= 0) {
        stopPermissionCountdown();
      }
    }, 1000);
  }

  function stopPermissionCountdown() {
    if (permissionTimer) {
      window.clearInterval(permissionTimer);
      permissionTimer = null;
    }
  }

  function renderCountdown(seconds) {
    const minutes = Math.floor(seconds / 60);
    const rest = String(seconds % 60).padStart(2, '0');
    permissionTimeout.textContent = `Timeout: ${minutes}:${rest}`;
  }

  function updatePermissionBadge() {
    const badge = document.getElementById('permissionBadge');
    if (permissionQueue.length > 0) {
      badge.textContent = String(permissionQueue.length);
      badge.hidden = false;
    } else {
      badge.hidden = true;
    }
  }

  function playNotificationSound() {
    if (!soundEnabled) return;
    try {
      const ctx = new (window.AudioContext || window.webkitAudioContext)();
      const osc = ctx.createOscillator();
      const gain = ctx.createGain();
      osc.connect(gain);
      gain.connect(ctx.destination);
      osc.type = 'sine';
      // Dual tone notification beep
      osc.frequency.setValueAtTime(880, ctx.currentTime);
      osc.frequency.setValueAtTime(660, ctx.currentTime + 0.1);
      osc.frequency.setValueAtTime(880, ctx.currentTime + 0.2);
      gain.gain.setValueAtTime(0.15, ctx.currentTime);
      gain.gain.exponentialRampToValueAtTime(0.001, ctx.currentTime + 0.35);
      osc.start(ctx.currentTime);
      osc.stop(ctx.currentTime + 0.35);
    } catch (e) {
      // AudioContext fails on silent page unless user has interacted, which is fine
    }
  }

  function addToLog(data) {
    eventLog.push(data);
    if (eventLog.length > MAX_LOG_ENTRIES) {
      eventLog.shift();
    }
    logList.innerHTML = eventLog.map((entry) => {
      const text = escapeHtml(`${labelEvent(entry.event)} ${entry.tool_name || ''} ${entry.detail || entry.action || ''}`.trim());
      return `<div class="log-row"><span>${formatTime(entry.timestamp)}</span><span>${text}</span></div>`;
    }).join('');
    logList.scrollTop = logList.scrollHeight;
  }

  function startHeartbeat() {
    if (heartbeatTimer) {
      window.clearInterval(heartbeatTimer);
    }
    heartbeatTimer = window.setInterval(() => {
      if (ws && ws.readyState === WebSocket.OPEN) {
        ws.send(JSON.stringify({ type: 'ping' }));
      }
    }, 30_000);
  }

  function cleanupSocket() {
    if (reconnectTimer) {
      window.clearTimeout(reconnectTimer);
      reconnectTimer = null;
    }
    if (ws) {
      ws.close();
      ws = null;
    }
    cleanupTimers();
  }

  function cleanupTimers() {
    if (heartbeatTimer) {
      window.clearInterval(heartbeatTimer);
      heartbeatTimer = null;
    }
  }

  function renderList(container, items) {
    const safeItems = Array.isArray(items) && items.length ? items : ['-'];
    container.innerHTML = safeItems.map((item) => `<li>${escapeHtml(String(item))}</li>`).join('');
  }

  function colorForState(state) {
    const map = {
      disconnected: '#555555',
      idle: '#4488ff',
      session_active: '#aa66ff',
      working: '#ffb800',
      permission_required: '#ffb800',
      done: '#00ff87',
      error: '#ff4444'
    };
    return map[state] || '#4488ff';
  }

  // Parses folder name from full path
  function projectLabel(cwd) {
    if (!cwd) return '-';
    const normalized = String(cwd).replace(/\\/g, '/');
    return normalized.split('/').filter(Boolean).pop() || normalized;
  }

  function labelEvent(eventName) {
    const labels = {
      session_start: 'SessionStart',
      user_prompt: 'UserPrompt',
      tool_done: 'ToolUse',
      permission_required: 'Permission',
      permission_resolved: 'Decision',
      stop: 'Stop'
    };
    return labels[eventName] || eventName || 'Event';
  }

  function formatTime(timestamp) {
    const date = timestamp ? new Date(timestamp) : new Date();
    if (Number.isNaN(date.getTime())) {
      return '--:--:--';
    }
    return date.toLocaleTimeString([], {
      hour: '2-digit',
      minute: '2-digit',
      second: '2-digit'
    });
  }

  function escapeHtml(value) {
    return value
      .replaceAll('&', '&amp;')
      .replaceAll('<', '&lt;')
      .replaceAll('>', '&gt;')
      .replaceAll('"', '&quot;')
      .replaceAll("'", '&#039;');
  }
})();
