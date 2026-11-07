const fs=require('node:fs'),path=require('node:path'),cp=require('node:child_process');
const repo='C:/Users/brett/Documents/Codex/2026-11-06/files-pasted-by-the-user-project/work/nxaevora-main';
const workspace='C:/Users/brett/Documents/Codex/2026-11-06/recompiled-game-folder-c-users-brett';
const fixture=path.resolve('work/checker-fixture');
fs.mkdirSync(fixture+'/tools',{recursive:true});
for(const name of ['AGENTS.md','CLAUDE.md'])fs.copyFileSync(repo+'/'+name,fixture+'/'+name);
fs.cpSync(repo+'/docs/project-memory',fixture+'/docs/project-memory',{recursive:true});
fs.copyFileSync(repo+'/tools/check-project-memory.cjs',fixture+'/tools/check-project-memory.cjs');
function run(expected,label){const r=cp.spawnSync(process.execPath,[fixture+'/tools/check-project-memory.cjs'],{encoding:'utf8'});if(r.status!==expected)throw Error(label+': '+r.stdout+r.stderr);console.log(label+': passed');}
run(0,'Valid copied memory');
const state=fixture+'/docs/project-memory/STATE.md',original=fs.readFileSync(state,'utf8');
fs.appendFileSync(state,'\n[broken](missing.md)\n');run(1,'Broken link rejected');fs.writeFileSync(state,original);
fs.appendFileSync(state,'\n'+('excess '.repeat(1600)));run(1,'Oversized startup rejected');fs.writeFileSync(state,original);
const task=fixture+'/docs/project-memory/tasks/2026-11-06-lighting-box.md',body=fs.readFileSync(task,'utf8');
fs.writeFileSync(task,body.replace('Status: ongoing','Status: invented'));run(1,'Invalid task status rejected');fs.writeFileSync(task,body);
for(const p of ['outputs/arceus-shadow-test-results.txt','outputs/controller-test-launch-notes.txt','work/shadow-test-review/copy-verification.json','work/pass10-captures/screenshot_20261106_181800_buffers_4_attachments','work/native-shadow-candidate/ArceusNativeShadow-runtime.exe']){if(!fs.existsSync(workspace+'/'+p))throw Error('Missing evidence '+p);}
console.log('Key external evidence and resume capture exist.');
