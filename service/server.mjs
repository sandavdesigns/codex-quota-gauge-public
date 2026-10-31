import http from 'node:http';
import {createHash, timingSafeEqual} from 'node:crypto';
import {readFileSync} from 'node:fs';
import {pathToFileURL} from 'node:url';
import {readCodex, normalize, demo} from './provider.mjs';

export class QuotaCache {
  constructor({source = 'codex', staleAfter = 900} = {}) {
    this.source = source; this.staleAfter = staleAfter;
    this.limits = []; this.sampleTime = null; this.sampleMono = null; this.error = null;
  }
  success(limits, now = Date.now()/1000, mono = performance.now()/1000) {
    this.limits = limits; this.sampleTime = Math.floor(now); this.sampleMono = mono; this.error = null;
  }
  failure() { this.error = 'source_unavailable'; }
  snapshot(now = Date.now()/1000, mono = performance.now()/1000) {
    const age = this.sampleMono == null ? null : Math.max(0, Math.floor(mono-this.sampleMono));
    const expired = this.limits.some(x => x.resets_at != null && x.resets_at <= now);
    return {schema_version:1, source:this.source,
      state: !this.limits.length ? 'missing' : this.error || age >= this.staleAfter || expired ? 'stale' : 'ok',
      server_time: Math.floor(now), updated_at: this.sampleTime, age_seconds:age,
      stale_after_seconds:this.staleAfter, error:this.error,
      limits:this.limits.map(x => ({...x, reset_in_seconds:x.resets_at == null ? null : Math.max(0,Math.floor(x.resets_at-now))}))};
  }
}
const staticRoutes = {
  '/': ['index.html', 'text/html; charset=utf-8'],
  '/index.html': ['index.html', 'text/html; charset=utf-8'],
  '/app.js': ['app.js', 'text/javascript; charset=utf-8'],
  '/style.css': ['style.css', 'text/css; charset=utf-8']
};
const securityHeaders = {
  'Cache-Control':'no-store',
  'X-Content-Type-Options':'nosniff',
  'Referrer-Policy':'no-referrer',
  'Content-Security-Policy':"default-src 'self'; script-src 'self'; style-src 'self'; connect-src 'self'; img-src 'self' data:; object-src 'none'; base-uri 'none'; frame-ancestors 'none'; form-action 'none'"
};
const deviceTokenPattern = /^[A-Za-z0-9_-]{32,128}$/;

export function validateDeviceToken(token) {
  if (typeof token !== 'string' || !deviceTokenPattern.test(token)) {
    throw new Error('Invalid DEVICE_TOKEN_FILE contents');
  }
  return token;
}

export function loadDeviceToken(filePath) {
  if (filePath == null || filePath === '') return undefined;
  if (typeof filePath !== 'string') throw new Error('Invalid DEVICE_TOKEN_FILE');
  try {
    const contents = readFileSync(filePath, 'utf8');
    const token = contents.endsWith('\r\n') ? contents.slice(0, -2)
      : contents.endsWith('\n') ? contents.slice(0, -1) : contents;
    return validateDeviceToken(token);
  } catch {
    // Do not expose the secret path, contents, or low-level filesystem details.
    throw new Error('Unable to load DEVICE_TOKEN_FILE');
  }
}

function tokenMatches(candidate, expectedDigest) {
  if (typeof candidate !== 'string') return false;
  const candidateDigest = createHash('sha256').update(candidate, 'utf8').digest();
  return timingSafeEqual(candidateDigest, expectedDigest);
}

function isAuthorized(req, expectedDigest) {
  const authorizationValues = [];
  for (let index = 0; index < req.rawHeaders.length; index += 2) {
    if (req.rawHeaders[index].toLowerCase() === 'authorization') {
      authorizationValues.push(req.rawHeaders[index + 1]);
    }
  }
  if (authorizationValues.length !== 1) return false;
  const match = /^Bearer ([A-Za-z0-9_-]{32,128})$/.exec(authorizationValues[0]);
  return match !== null && tokenMatches(match[1], expectedDigest);
}

export function makeServer(cache, {deviceToken} = {}) {
  const expectedDigest = deviceToken === undefined ? null
    : createHash('sha256').update(validateDeviceToken(deviceToken), 'utf8').digest();
  // An exact allowlist; request paths are never interpreted as filesystem paths.
  const assets = new Map(Object.entries(staticRoutes).map(([route, [file, type]]) =>
    [route, {body:readFileSync(new URL(`./web/${file}`, import.meta.url)), type}]));
  return http.createServer({requestTimeout:5000, headersTimeout:5000}, (req,res) => {
    const asset = req.method === 'GET' && assets.get(req.url);
    if (asset) {
      res.writeHead(200,{...securityHeaders,'Content-Type':asset.type,'Content-Length':asset.body.length});
      return res.end(asset.body);
    }
    if (req.method !== 'GET' || !['/v1/quota','/v1/device-quota','/healthz'].includes(req.url)) {
      res.writeHead(404,securityHeaders); return res.end();
    }
    if (req.url === '/v1/device-quota' && expectedDigest === null) {
      res.writeHead(404, securityHeaders); return res.end();
    }
    if (req.url === '/v1/device-quota' && !isAuthorized(req, expectedDigest)) {
      res.writeHead(401, {...securityHeaders, 'WWW-Authenticate':'Bearer'});
      return res.end();
    }
    const payload = cache.snapshot();
    const body = JSON.stringify(req.url === '/healthz' ? {state:payload.state, source:payload.source} : payload);
    // Explicit length avoids chunk framing for the ESP32's bounded stream reader.
    res.writeHead(req.url === '/healthz' && payload.state !== 'ok' ? 503 : 200,
      {...securityHeaders,'Content-Type':'application/json; charset=utf-8',
       'Content-Length':Buffer.byteLength(body)});
    res.end(body);
  });
}

function envInt(name, fallback, min, max) {
  const value = Number(process.env[name] ?? fallback);
  if (!Number.isInteger(value) || value < min || value > max) throw new Error(`Invalid ${name}`);
  return value;
}
export function main() {
  const source = process.env.SOURCE ?? 'demo';
  if (!['codex','demo'].includes(source)) throw new Error('Invalid SOURCE');
  const poll = envInt('POLL_SECONDS',300,60,3600);
  const cache = new QuotaCache({source,staleAfter:envInt('STALE_SECONDS',900,60,86400)});
  let stopped = false, timer;
  async function update() {
    try {
      const limits = source === 'demo' ? demo() : normalize(await readCodex({binary:process.env.CODEX_BIN ?? 'codex'}),process.env.LIMIT_ID ?? 'codex');
      cache.success(limits);
    } catch { cache.failure(); console.error('Quota source unavailable; details suppressed. Check CLI login/status interactively.'); }
    if (!stopped) timer = setTimeout(update, poll*1000);
  }
  const deviceToken = loadDeviceToken(process.env.DEVICE_TOKEN_FILE);
  const server = makeServer(cache, {deviceToken});
  server.listen(envInt('PORT',8787,1,65535),process.env.HOST ?? '127.0.0.1',() => {
    console.log(`Quota service started; source=${source}.`);
  });
  update();
  for (const signal of ['SIGTERM','SIGINT']) process.on(signal, () => {
    stopped = true; clearTimeout(timer); server.close();
    setTimeout(() => process.exit(0),1000).unref();
  });
}
if (process.argv[1] && import.meta.url === pathToFileURL(process.argv[1]).href) main();
