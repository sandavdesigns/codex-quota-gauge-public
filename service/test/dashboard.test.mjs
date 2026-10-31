import {test} from 'node:test';
import assert from 'node:assert/strict';
import {deriveState,formatDuration,presentation,shouldApplyResponse,validateQuota} from '../web/app.js';

const quota = (overrides = {}) => validateQuota({schema_version:1,state:'ok',source:'codex',stale_after_seconds:90,age_seconds:4,updated_at:1_700_000_000,limits:[{label:'5 STUNDEN',remaining_percent:72,reset_in_seconds:200}],...overrides});
test('validates representative payloads and refuses invented numeric values',()=>{
  assert.equal(quota().limits[0].remaining,72);
  assert.deepEqual(validateQuota({schema_version:1,state:'missing',source:'codex',stale_after_seconds:90,age_seconds:null,updated_at:null,limits:[]}).limits,[]);
  assert.equal(quota({limits:[{label:'x',remaining_percent:'72'}]}),null);
  assert.equal(quota({schema_version:2}),null);
  assert.equal(quota({age_seconds:null}),null);
  assert.equal(quota({limits:[]}),null);
  assert.equal(quota({stale_after_seconds:59}),null);
  assert.equal(quota({limits:[{label:'x'.repeat(29),remaining_percent:72}]}),null);
});
test('pending and initial failures remain missing without fake limits',()=>{
  assert.deepEqual(deriveState(null,0,false),{kind:'missing',age:null,message:'Warte auf die erste Messung.'});
  assert.equal(deriveState(null,0,true).kind,'missing');
  assert.deepEqual(presentation(null,10,true).limits,[]);
});
test('age is locally monotonic and becomes stale exactly at staleSeconds',()=>{
  const sample=quota({age_seconds:50,stale_after_seconds:60});
  assert.equal(deriveState(sample,9).kind,'ok');
  const state=deriveState(sample,10); assert.equal(state.kind,'stale'); assert.equal(state.age,60);
});
test('reset countdown reaches stale while unknown reset stays unknown',()=>{
  const known=quota({limits:[{label:'WOCHE',remaining_percent:30,reset_in_seconds:3}]});
  assert.equal(deriveState(known,3).kind,'stale');
  const unknown=quota({limits:[{label:'WOCHE',remaining_percent:30,reset_in_seconds:null}]});
  assert.equal(presentation(unknown,99).limits[0].resetIn,null);
  assert.equal(formatDuration(null),'unbekannt');
});
test('demo is retained and visibly identifiable even if stale or disconnected',()=>{
  const demo=quota({source:'demo',state:'stale'}); const view=presentation(demo,0,true);
  assert.equal(view.source,'demo'); assert.equal(view.kind,'stale');
});
test('a connection failure retains known values but marks them stale',()=>{
  const view=presentation(quota(),5,true);
  assert.equal(view.kind,'stale'); assert.equal(view.limits[0].remaining,72);
  assert.match(view.message,/Verbindung unterbrochen/);
});
test('late fetch responses cannot overwrite a newer request',()=>{
  assert.equal(shouldApplyResponse(2,2),true);
  assert.equal(shouldApplyResponse(1,2),false);
});
