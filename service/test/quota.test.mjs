import {test} from 'node:test';
import assert from 'node:assert/strict';
import {mkdtemp,writeFile,rm} from 'node:fs/promises';
import {tmpdir} from 'node:os';
import {join} from 'node:path';
import {readCodex,normalize,demo} from '../provider.mjs';
import {QuotaCache,makeServer} from '../server.mjs';
const win=(used=25)=>({usedPercent:used,windowDurationMins:10080,resetsAt:2000});
test('uses selected map bucket, omits absent second window',()=>{
 assert.deepEqual(normalize({rateLimitsByLimitId:{codex:{primary:win(),secondary:null}}}),
 [{id:'primary',label:'WOCHE',remaining_percent:75,window_minutes:10080,resets_at:2000}]);
});
test('map authoritative; does not substitute another bucket',()=>{
 assert.deepEqual(normalize({rateLimitsByLimitId:{other:{primary:win()}},rateLimits:{primary:win()}}),[]);
});
test('legacy fallback and percent-only source',()=>{
 assert.equal(normalize({rateLimits:{primary:{usedPercent:0}}})[0].remaining_percent,100);
 assert.equal(normalize({rateLimits:{primary:{usedPercent:100}}})[0].remaining_percent,0);
 assert.equal(normalize({rateLimits:{primary:{usedPercent:105}}})[0].remaining_percent,0);
});
test('reject missing, invalid, negative and string percentages',()=>{
 for(const usedPercent of [null,undefined,-1,'5',NaN,Infinity])
 assert.throws(()=>normalize({rateLimits:{primary:{usedPercent}}}));
});
test('two windows, unknown duration labeled honestly',()=>{
 const r=normalize({rateLimits:{primary:win(),secondary:{usedPercent:50,windowDurationMins:17}}});
 assert.equal(r.length,2);assert.equal(r[1].label,'17 MIN');assert.equal(r[1].resets_at,null);
});
test('missing at startup and stale after age, failure or elapsed reset',()=>{
 const c=new QuotaCache(); assert.equal(c.snapshot(1000,100).state,'missing');
 c.success(normalize({rateLimits:{primary:win()}}),1000,100);
 assert.equal(c.snapshot(1001,101).state,'ok');
 assert.equal(c.snapshot(1900,1000).state,'stale');
 assert.equal(c.snapshot(2001,101).state,'stale');
 c.failure();assert.equal(c.snapshot(1002,102).state,'stale');
 assert.equal(c.snapshot(1002,102).age_seconds,2);
 c.success([],1003,103);assert.equal(c.snapshot(1003,103).state,'missing');
});
test('monotonic age survives wall clock change; no inferred refill',()=>{
 const c=new QuotaCache();c.success(normalize({rateLimits:{primary:win()}}),1000,100);
 assert.equal(c.snapshot(500,1000).state,'stale');
 assert.equal(c.snapshot(500,1000).limits[0].remaining_percent,75);
});
test('demo explicitly marked and JSON/health HTTP integration',async()=>{
 const c=new QuotaCache({source:'demo'});c.success(demo());const s=makeServer(c);
 await new Promise(r=>s.listen(0,'127.0.0.1',r));
 try{
 const url=`http://127.0.0.1:${s.address().port}`;
 const response=await fetch(url+'/v1/quota');assert.equal(response.headers.get('cache-control'),'no-store');
 assert.equal(response.headers.get('transfer-encoding'),null);
 const text=await response.text();assert.equal(Number(response.headers.get('content-length')),Buffer.byteLength(text));
 const body=JSON.parse(text);assert.equal(body.source,'demo');assert.equal(body.limits.length,2);
 assert.equal((await fetch(url+'/healthz')).status,200);
 c.failure();assert.equal((await fetch(url+'/healthz')).status,503);
 assert.equal((await fetch(url+'/unknown')).status,404);
 const home=await fetch(url+'/');assert.equal(home.status,200);
 assert.match(home.headers.get('content-type'),/text\/html/);
 assert.match(home.headers.get('content-security-policy'),/frame-ancestors 'none'/);
 assert.match(await home.text(),/Codex Quota Gauge/);
 const js=await fetch(url+'/app.js');assert.equal(js.status,200);
 assert.match(js.headers.get('content-type'),/javascript/);
 assert.equal((await fetch(url+'/style.css')).status,200);
 assert.equal((await fetch(url+'/provider.mjs')).status,404);
 assert.equal((await fetch(url+'/.env')).status,404);
 assert.equal((await fetch(url+'/v1/quota',{method:'POST'})).status,404);
 }finally{await new Promise(r=>s.close(r));}
});
async function fake(script,fn){
 const d=await mkdtemp(join(tmpdir(),'quota-test-'));const path=join(d,'fake-codex');
 await writeFile(path,'#!/usr/bin/env node\n'+script,{mode:0o700});
 try{await fn(path);}finally{await rm(d,{recursive:true,force:true});}
}
test('RPC initialization, notification handling, no inference',async()=>{
 await fake(`const rl=require('node:readline').createInterface({input:process.stdin});
rl.on('line',line=>{const m=JSON.parse(line);
if(m.method==='initialize')console.log(JSON.stringify({id:m.id,result:{}}));
else if(m.method==='account/rateLimits/read'){
console.log(JSON.stringify({method:'account/updated',params:{}}));
console.log(JSON.stringify({id:m.id,result:{rateLimits:{primary:{usedPercent:12}}}}));}
else if(m.method!=='initialized')process.exit(4);
});`,async binary=>assert.equal(normalize(await readCodex({binary}))[0].remaining_percent,88));
});
test('RPC failure and hung child have bounded, redacted errors',async()=>{
 await fake(`process.stdin.on('data',()=>console.log(JSON.stringify({id:1,error:{message:'SECRET'}})));`,
 async binary=>await assert.rejects(readCodex({binary}),{message:'provider_initialize'}));
 await fake(`setInterval(()=>{},1000);`,async binary=>
 await assert.rejects(readCodex({binary,timeoutMs:100}),{message:'provider_timeout'}));
});
test('unavailable CLI rejects safely',async()=>{
 await assert.rejects(readCodex({binary:'/does/not/exist'}),{message:'provider_unavailable'});
});
