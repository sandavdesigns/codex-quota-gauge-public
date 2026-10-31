import {test} from 'node:test';
import assert from 'node:assert/strict';
import http from 'node:http';
import {mkdtemp, rm, writeFile} from 'node:fs/promises';
import {tmpdir} from 'node:os';
import {join} from 'node:path';
import {QuotaCache, loadDeviceToken, makeServer, validateDeviceToken} from '../server.mjs';

const token = 'A'.repeat(43);

async function withServer(fn) {
  let snapshotCalls = 0;
  const cache = {
    snapshot() {
      snapshotCalls += 1;
      return {schema_version:1, state:'ok', source:'demo', limits:[]};
    }
  };
  const server = makeServer(cache, {deviceToken:token});
  await new Promise(resolve => server.listen(0, '127.0.0.1', resolve));
  try {
    await fn(`http://127.0.0.1:${server.address().port}`, () => snapshotCalls);
  } finally {
    await new Promise(resolve => server.close(resolve));
  }
}

function rawRequest(url, {method='GET', headers=[]} = {}) {
  return new Promise((resolve, reject) => {
    const target = new URL(url);
    const request = http.request({
      host:target.hostname, port:target.port, path:target.pathname + target.search,
      method, headers:['Host', target.host, ...headers.flat()]
    }, response => {
      const chunks = [];
      response.on('data', chunk => chunks.push(chunk));
      response.on('end', () => resolve({
        status:response.statusCode,
        headers:response.headers,
        body:Buffer.concat(chunks).toString('utf8')
      }));
    });
    request.on('error', reject);
    request.end();
  });
}

test('token validation accepts safe bounded values and rejects malformed values', () => {
  assert.equal(validateDeviceToken(token), token);
  assert.equal(validateDeviceToken('0123456789abcdef'.repeat(4)), '0123456789abcdef'.repeat(4));
  for (const value of ['', 'A'.repeat(31), 'A'.repeat(129), 'A'.repeat(32) + ' ',
    'A'.repeat(31) + '/', 'A'.repeat(31) + '\n', undefined]) {
    assert.throws(() => validateDeviceToken(value), /Invalid DEVICE_TOKEN_FILE contents/);
  }
});

test('token file is optional, read once, and specified bad files fail safely', async () => {
  assert.equal(loadDeviceToken(undefined), undefined);
  assert.equal(loadDeviceToken(''), undefined);
  const directory = await mkdtemp(join(tmpdir(), 'gauge-auth-'));
  const path = join(directory, 'token');
  try {
    await writeFile(path, token + '\n');
    assert.equal(loadDeviceToken(path), token);
    await writeFile(path, 'short\n');
    assert.throws(() => loadDeviceToken(path), {message:'Unable to load DEVICE_TOKEN_FILE'});
    assert.throws(() => loadDeviceToken(join(directory, 'missing')), {message:'Unable to load DEVICE_TOKEN_FILE'});
  } finally {
    await rm(directory, {recursive:true, force:true});
  }
});

test('device quota bearer auth accepts only one valid Authorization header', async () => {
  await withServer(async (base, snapshotCalls) => {
    const valid = await rawRequest(base + '/v1/device-quota', {headers:[['Authorization', `Bearer ${token}`]]});
    assert.equal(valid.status, 200);
    assert.equal(JSON.parse(valid.body).source, 'demo');
    assert.equal(snapshotCalls(), 1);

    for (const headers of [
      [],
      [['Authorization', `Bearer ${'B'.repeat(43)}`]],
      [['Authorization', token]],
      [['Authorization', `bearer ${token}`]],
      [['Authorization', `Bearer ${token}`], ['Authorization', `Bearer ${token}`]]
    ]) {
      const response = await rawRequest(base + '/v1/device-quota', {headers});
      assert.equal(response.status, 401);
      assert.equal(response.body, '');
      assert.equal(response.headers['www-authenticate'], 'Bearer');
    }
    assert.equal(snapshotCalls(), 1, 'unauthorized requests must not read the quota snapshot');
  });
});

test('query tokens, unknown routes, and non-GET methods do not bypass routing', async () => {
  await withServer(async (base, snapshotCalls) => {
    assert.equal((await rawRequest(base + `/v1/device-quota?token=${token}`)).status, 404);
    assert.equal((await rawRequest(base + `/unknown`, {headers:[['Authorization', `Bearer ${token}`]]})).status, 404);
    assert.equal((await rawRequest(base + '/v1/device-quota', {
      method:'POST', headers:[['Authorization', `Bearer ${token}`]]
    })).status, 404);
    assert.equal(snapshotCalls(), 0);
  });
});

test('LAN quota, health, and dashboard remain available without bearer auth', async () => {
  const cache = new QuotaCache({source:'demo'});
  cache.success([]);
  const server = makeServer(cache, {deviceToken:token});
  await new Promise(resolve => server.listen(0, '127.0.0.1', resolve));
  try {
    const base = `http://127.0.0.1:${server.address().port}`;
    assert.equal((await rawRequest(base + '/v1/quota')).status, 200);
    assert.equal((await rawRequest(base + '/healthz')).status, 503);
    assert.equal((await rawRequest(base + '/')).status, 200);
  } finally {
    await new Promise(resolve => server.close(resolve));
  }
});

test('device quota route does not exist when no token is configured', async () => {
  const cache = new QuotaCache({source:'demo'});
  const server = makeServer(cache);
  await new Promise(resolve => server.listen(0, '127.0.0.1', resolve));
  try {
    const base = `http://127.0.0.1:${server.address().port}`;
    assert.equal((await rawRequest(base + '/v1/device-quota')).status, 404);
    assert.equal((await rawRequest(base + '/v1/quota')).status, 200);
  } finally {
    await new Promise(resolve => server.close(resolve));
  }
});
