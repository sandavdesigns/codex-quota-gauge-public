import {spawn} from 'node:child_process';
import {createInterface} from 'node:readline';

// Only documented app-server RPCs. Never copy/read auth.json or expose raw RPC responses.
export function readCodex({binary = 'codex', timeoutMs = 25000} = {}) {
  return new Promise((resolve, reject) => {
    const child = spawn(binary, ['app-server', '--listen', 'stdio://'], {
      stdio: ['pipe', 'pipe', 'ignore'], env: {...process.env, NO_COLOR: '1'}
    });
    const lines = createInterface({input: child.stdout});
    let done = false, total = 0;
    const finish = (error, result) => {
      if (done) return;
      done = true; clearTimeout(timer); lines.close();
      child.stdin.destroy(); child.kill('SIGTERM');
      const killTimer = setTimeout(() => child.kill('SIGKILL'), 1000); killTimer.unref();
      child.once('exit', () => clearTimeout(killTimer));
      if (error) reject(new Error(error)); else resolve(result);
    };
    const timer = setTimeout(() => finish('provider_timeout'), timeoutMs);
    const send = message => child.stdin.write(JSON.stringify(message) + '\n');
    child.stdin.on('error', () => finish('provider_unavailable'));
    child.on('error', () => finish('provider_unavailable'));
    child.on('exit', () => finish('provider_unavailable'));
    child.stdout.on('data', chunk => {
      total += chunk.length;
      if (total > 2 * 1024 * 1024) finish('provider_response_too_large');
    });
    lines.on('line', line => {
      if (done) return;
      let msg; try { msg = JSON.parse(line); } catch { return finish('provider_protocol'); }
      if (msg.id === 1) {
        if (msg.error || !msg.result) return finish('provider_initialize');
        send({method: 'initialized'});
        send({id: 2, method: 'account/rateLimits/read'});
      } else if (msg.id === 2) {
        if (msg.error || !msg.result) return finish('provider_auth_or_rpc');
        finish(null, msg.result);
      } else if (msg.id !== undefined && msg.method) {
        // No automatic login, token handling, approval or tool execution.
        send({id: msg.id, error: {code: -32601, message: 'Unsupported request'}});
      }
    });
    send({id: 1, method: 'initialize', params: {clientInfo: {
      name: 'codex_quota_gauge', title: 'Local quota gauge', version: '1.0.0'
    }}});
  });
}

function number(value) { return typeof value === 'number' && Number.isFinite(value); }
export function normalizeResetCredits(raw) {
  const availableCount = raw?.rateLimitResetCredits?.availableCount;
  return Number.isSafeInteger(availableCount) && availableCount >= 0 ? availableCount : null;
}
export function normalize(raw, bucketId = 'codex') {
  if (!raw || typeof raw !== 'object') throw new Error('provider_protocol');
  let bucket;
  // Never silently pick a different model/bucket when the requested bucket is absent.
  if (raw.rateLimitsByLimitId != null) bucket = raw.rateLimitsByLimitId[bucketId];
  else if (raw.rateLimits && (raw.rateLimits.limitId === bucketId ||
    (bucketId === 'codex' && raw.rateLimits.limitId == null))) bucket = raw.rateLimits;
  if (!bucket) return [];
  const result = [];
  for (const key of ['primary', 'secondary']) {
    const window = bucket[key];
    if (window == null) continue;
    const used = window.usedPercent;
    if (!number(used) || used < 0) throw new Error('provider_invalid_percent');
    // Usage may exceed 100%; remaining is clamped, never negative.
    const minutes = window.windowDurationMins;
    if (minutes != null && (!Number.isSafeInteger(minutes) || minutes <= 0)) throw new Error('provider_invalid_window');
    const reset = window.resetsAt;
    if (reset != null && (!Number.isSafeInteger(reset) || reset < 0)) throw new Error('provider_invalid_reset');
    const label = minutes === 10080 ? 'WOCHE' : minutes === 300 ? '5 STUNDEN' :
      minutes == null ? key.toUpperCase() : `${minutes} MIN`;
    result.push({id: key, label, remaining_percent: Math.max(0, Math.min(100, 100 - used)),
      window_minutes: minutes ?? null, resets_at: reset ?? null});
  }
  return result;
}

export function demo(now = Math.floor(Date.now()/1000)) {
  return [{id:'primary', label:'5 STUNDEN', remaining_percent:72, window_minutes:300, resets_at:now+8540},
    {id:'secondary', label:'WOCHE', remaining_percent:38, window_minutes:10080, resets_at:now+238000}];
}
