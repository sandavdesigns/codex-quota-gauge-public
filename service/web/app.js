const POLL_MS = 30_000;
const TIMEOUT_MS = 8_000;
const CIRCUMFERENCE = 2 * Math.PI * 43;

export function isFiniteNumber(value) { return typeof value === 'number' && Number.isFinite(value); }
export function formatDuration(seconds) {
  if (!isFiniteNumber(seconds) || seconds < 0) return 'unbekannt';
  const value = Math.floor(seconds); const days = Math.floor(value / 86400); const hours = Math.floor(value % 86400 / 3600); const minutes = Math.floor(value % 3600 / 60);
  if (days) return `${days} T ${hours} Std.`;
  if (hours) return `${hours} Std. ${minutes} Min.`;
  return `${minutes} Min.`;
}
export function validateQuota(payload) {
  if (!payload || typeof payload !== 'object' || payload.schema_version !== 1 || !['ok', 'missing', 'stale'].includes(payload.state) || !['codex', 'demo'].includes(payload.source) || !Array.isArray(payload.limits) || payload.limits.length > 2) return null;
  if (!isFiniteNumber(payload.stale_after_seconds) || payload.stale_after_seconds < 60 || payload.stale_after_seconds > 86400) return null;
  if (payload.age_seconds != null && (!isFiniteNumber(payload.age_seconds) || payload.age_seconds < 0)) return null;
  if (payload.updated_at != null && (!isFiniteNumber(payload.updated_at) || payload.updated_at < 0)) return null;
  const staleSeconds = payload.stale_after_seconds;
  const ageSeconds = payload.age_seconds ?? null;
  const updatedAt = payload.updated_at ?? null;
  if (!payload.limits.every(limit => limit && typeof limit === 'object' && typeof limit.label === 'string' && limit.label.length > 0 && limit.label.length <= 28 && isFiniteNumber(limit.remaining_percent) && limit.remaining_percent >= 0 && limit.remaining_percent <= 100 && (limit.reset_in_seconds == null || (isFiniteNumber(limit.reset_in_seconds) && limit.reset_in_seconds >= 0)))) return null;
  if (payload.state === 'ok' && (ageSeconds == null || !payload.limits.length)) return null;
  const limits = payload.limits.slice(0, 2).map(limit => ({ label:limit.label, remaining:limit.remaining_percent, resetIn:limit.reset_in_seconds == null ? null : limit.reset_in_seconds }));
  return {state:payload.state, source:payload.source, staleSeconds, ageSeconds, updatedAt, limits};
}
export function deriveState(sample, elapsedSeconds = 0, connectionLost = false) {
  if (!sample) return {kind:'missing', age:null, message:connectionLost ? 'Keine Verbindung zum Quota-Dienst.' : 'Warte auf die erste Messung.'};
  const elapsed = Math.max(0, elapsedSeconds);
  const age = sample.ageSeconds == null ? null : sample.ageSeconds + elapsed;
  const resetReached = sample.limits.some(limit => limit.resetIn != null && limit.resetIn - elapsed <= 0);
  const stale = sample.state === 'stale' || (sample.staleSeconds != null && age != null && age >= sample.staleSeconds) || resetReached || connectionLost;
  if (sample.state === 'missing' || !sample.limits.length) return {kind:'missing', age, message:'Der Dienst hat noch keine verwertbaren Limits gemeldet.'};
  if (stale) return {kind:'stale', age, message:connectionLost ? 'Verbindung unterbrochen – zuletzt bekannte Werte werden angezeigt.' : 'Messung ist nicht mehr aktuell – zuletzt bekannte Werte werden angezeigt.'};
  return {kind:'ok', age, message:'Messung aktuell.'};
}
export function shouldApplyResponse(requestId, latestRequestId) { return requestId === latestRequestId; }
export function presentation(sample, elapsedSeconds, connectionLost) {
  const status = deriveState(sample, elapsedSeconds, connectionLost);
  const limits = sample?.limits.map(limit => ({...limit, resetIn:limit.resetIn == null ? null : Math.max(0, limit.resetIn - elapsedSeconds)})) ?? [];
  return {...status, limits, source:sample?.source ?? null, updatedAt:sample?.updatedAt ?? null};
}

function escapeText(target, value) { target.textContent = value; }
function ringNode(limit) {
  const card = document.createElement('article'); card.className = 'ring-card';
  const svg = document.createElementNS('http://www.w3.org/2000/svg','svg'); svg.setAttribute('class','ring'); svg.setAttribute('viewBox','0 0 100 100'); svg.setAttribute('role','img'); svg.setAttribute('aria-label',`${limit.label}: ${Math.round(limit.remaining)} Prozent verfügbar`);
  const level = limit.remaining < 20 ? ' critical' : limit.remaining < 45 ? ' warning' : '';
  for (const [klass, offset] of [['track', '0'], ['value' + level, String(CIRCUMFERENCE * (1 - limit.remaining / 100))]]) { const circle=document.createElementNS(svg.namespaceURI,'circle'); circle.setAttribute('class',klass); circle.setAttribute('cx','50'); circle.setAttribute('cy','50'); circle.setAttribute('r','43'); circle.setAttribute('stroke-dasharray',String(CIRCUMFERENCE)); circle.setAttribute('stroke-dashoffset',offset); svg.append(circle); }
  const percent=document.createElementNS(svg.namespaceURI,'text'); percent.setAttribute('class','ring-percent'); percent.setAttribute('x','50'); percent.setAttribute('y','48'); percent.textContent=`${Math.round(limit.remaining)} %`; svg.append(percent);
  const label=document.createElementNS(svg.namespaceURI,'text'); label.setAttribute('class','ring-label'); label.setAttribute('x','50'); label.setAttribute('y','72'); label.textContent=limit.label; svg.append(label);
  const caption=document.createElement('p'); caption.className='ring-caption'; const strong=document.createElement('strong'); strong.textContent=limit.label; caption.append(strong, document.createTextNode(limit.resetIn == null ? 'Resetzeit unbekannt' : `Reset in ${formatDuration(limit.resetIn)}`)); card.append(svg,caption); return card;
}
function render(view) {
  const grid=document.querySelector('#ring-grid'); const state=document.querySelector('#state-badge'); const demo=document.querySelector('#demo-badge');
  grid.replaceChildren(); grid.setAttribute('aria-busy','false'); state.className=`badge ${view.kind}`; escapeText(state,view.kind === 'ok' ? 'Aktuell' : view.kind === 'stale' ? 'Nicht aktuell' : 'Keine Daten'); demo.hidden=view.source !== 'demo';
  if (view.limits.length) view.limits.forEach(limit=>grid.append(ringNode(limit))); else { const empty=document.createElement('article'); empty.className='ring-card empty'; const title=document.createElement('h3'); title.textContent='Noch keine Quota-Daten'; const body=document.createElement('p'); body.textContent='Nach erfolgreicher Codex-Anmeldung im Container erscheinen die verfügbaren Limits hier.'; empty.append(title,body); grid.append(empty); }
  escapeText(document.querySelector('#measurement-age'),view.age == null ? '—' : formatDuration(view.age)); escapeText(document.querySelector('#last-measurement'),view.updatedAt == null ? 'Noch keine Messung' : new Date(view.updatedAt * 1000).toLocaleString('de-DE'));
  const connection=document.querySelector('#connection-note'); connection.className=`connection-note ${view.kind === 'ok' ? '' : view.kind === 'stale' ? 'warning' : 'error'}`; escapeText(connection,view.message);
  escapeText(document.querySelector('#source-note'),view.source === 'demo' ? 'Demodaten: keine echten Codex-Limits.' : view.source === 'codex' ? 'Quelle: lokal eingeloggte Codex-CLI im Container.' : 'Quelle wird abgerufen …');
}
function start() {
  let sample=null, sampleAt=0, connectionLost=false, latestRequest=0, controller=null, lastFingerprint='';
  const elapsed=()=>sample ? (performance.now()-sampleAt)/1000 : 0;
  const update=()=>{ const view=presentation(sample,elapsed(),connectionLost); const fingerprint=JSON.stringify([view.kind,view.source,view.updatedAt,Math.floor((view.age ?? 0)/60),view.limits.map(limit=>[limit.remaining,limit.resetIn == null ? null : Math.floor(limit.resetIn/60)])]); if (fingerprint !== lastFingerprint) { lastFingerprint=fingerprint; render(view); } };
  async function refresh() { const requestId=++latestRequest; controller?.abort(); controller=new AbortController(); const timeout=setTimeout(()=>controller.abort(),TIMEOUT_MS); try { const response=await fetch('/v1/quota',{headers:{Accept:'application/json'},signal:controller.signal,cache:'no-store'}); if(!response.ok) throw new Error('quota_http'); const parsed=validateQuota(await response.json()); if(!parsed) throw new Error('quota_payload'); if(shouldApplyResponse(requestId,latestRequest)) { sample=parsed; sampleAt=performance.now(); connectionLost=false; update(); } } catch { if(shouldApplyResponse(requestId,latestRequest)) { connectionLost=true; update(); } } finally { clearTimeout(timeout); } }
  update(); refresh(); setInterval(refresh,POLL_MS); setInterval(update,1000);
}
if (typeof document !== 'undefined') start();
