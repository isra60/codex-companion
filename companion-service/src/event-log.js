import fs from 'node:fs/promises';
import path from 'node:path';

export class EventLog {
  constructor(baseDir) {
    this.baseDir = baseDir;
  }

  async append(sessionId, eventData) {
    await fs.mkdir(this.baseDir, { recursive: true });
    const file = path.join(this.baseDir, `${sanitizeSessionId(sessionId)}.jsonl`);
    const line = JSON.stringify({
      ...eventData,
      logged_at: new Date().toISOString()
    });
    await fs.appendFile(file, `${line}\n`, 'utf8');
  }

  async getSessionLog(sessionId) {
    const file = path.join(this.baseDir, `${sanitizeSessionId(sessionId)}.jsonl`);
    const content = await fs.readFile(file, 'utf8');
    return content
      .split(/\r?\n/)
      .filter(Boolean)
      .map((line) => JSON.parse(line));
  }
}

function sanitizeSessionId(sessionId) {
  return String(sessionId || 'unknown-session').replace(/[^a-zA-Z0-9_.-]/g, '_');
}
