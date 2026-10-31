#!/usr/bin/env bash
# CI-only integration: disposable credentials, loopback ports, no account login.
set -euo pipefail
scratch=$(mktemp -d)
export DEVICE_TOKEN_PATH="$scratch/device-token.txt"
export HTTP_PORT=18879 LAN_IP=127.0.0.1 SOURCE=demo
proxy_name="gauge-proxy-test-$$"
cleanup() {
  docker rm -f "$proxy_name" >/dev/null 2>&1 || true
  docker compose -f compose.yaml -f compose.remote.yaml down --volumes >/dev/null 2>&1 || true
  rm -rf "$scratch"
}
trap cleanup EXIT
node -e 'require("fs").writeFileSync(process.env.DEVICE_TOKEN_PATH, require("crypto").randomBytes(32).toString("base64url"), {mode:0o644})'
docker compose -f compose.yaml -f compose.remote.yaml config --quiet
docker compose -f compose.yaml -f compose.remote.yaml up -d --no-build
container=$(docker compose ps -q gauge)
{
  printf 'events {}\nhttp { server { listen 8080; server_name gauge.example.org;\n'
  sed 's/192.168.1.50:8787/127.0.0.1:8787/g' deploy/npm-quota-location.conf
  printf 'location / { proxy_pass http://127.0.0.1:8787; }\n} }\n'
} > "$scratch/nginx.conf"
docker run -d --name "$proxy_name" --network "container:$container" \
  -v "$scratch/nginx.conf:/etc/nginx/nginx.conf:ro" nginx:stable-alpine >/dev/null
docker exec "$proxy_name" nginx -t
for attempt in $(seq 1 20); do
  if docker compose exec -T gauge node -e 'fetch("http://127.0.0.1:8080/v1/quota").then(r=>process.exit(r.status===401?0:1)).catch(()=>process.exit(1))'; then break; fi
  sleep 1
done
docker compose exec -T gauge node --input-type=module <<'JS'
import {readFileSync} from 'node:fs';
import assert from 'node:assert/strict';
const token = readFileSync('/run/secrets/gauge_device_token','utf8').trim();
const base = 'http://127.0.0.1:8080';
for (const path of ['/', '/healthz', '/v1/device-quota', '/app.js', '/v1/quota/']) {
  assert.equal((await fetch(base+path)).status,404,path);
}
assert.equal((await fetch(base+'/v1/quota')).status,401);
assert.equal((await fetch(base+'/v1/quota',{headers:{Authorization:'Bearer '+ 'a'.repeat(43)}})).status,401);
assert.equal((await fetch(base+'/v1/quota?token=forbidden')).status,400);
assert.equal((await fetch(base+'/v1/quota',{method:'POST'})).status,405);
const response=await fetch(base+'/v1/quota',{headers:{Authorization:'Bearer '+token}});
assert.equal(response.status,200);
assert.equal((await response.json()).source,'demo');
assert.equal((await fetch('http://127.0.0.1:8787/v1/quota')).status,200);
console.log('Remote route protection and LAN compatibility passed; token not logged.');
JS
