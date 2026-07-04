import os from 'node:os';
import { Bonjour } from 'bonjour-service';

export function createMdnsAdvertiser({ httpPort, wsPort }) {
  const bonjour = new Bonjour();
  const hostname = os.hostname();
  const service = bonjour.publish({
    name: 'codex-companion',
    type: 'codex-companion',
    protocol: 'tcp',
    port: wsPort,
    txt: {
      version: '1.0.0',
      dashboard: `http://${hostname}:${httpPort}/dashboard`
    }
  });

  return {
    stop() {
      return new Promise((resolve) => {
        service.stop(() => bonjour.destroy(resolve));
      });
    }
  };
}
