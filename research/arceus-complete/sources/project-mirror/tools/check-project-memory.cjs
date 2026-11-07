// Dependency-free structural check. Run: node tools/check-project-memory.cjs
const fs = require('node:fs');
const path = require('node:path');
const root = path.resolve(__dirname, '..');
const memory = path.join(root, 'docs/project-memory');
const errors = [];
const read = p => fs.readFileSync(p, 'utf8');
const words = s => s.trim().split(/\s+/u).filter(Boolean).length;
const required = ['AGENTS.md', 'CLAUDE.md', ...['START.md', 'STATE.md', 'INDEX.md', 'ENVIRONMENT.md', 'WORKFLOW.md'].map(p => 'docs/project-memory/' + p)];
for (const file of required) if (!fs.existsSync(path.join(root, file))) errors.push('Missing: ' + file);
function walk(dir) {
  if (!fs.existsSync(dir)) return [];
  return fs.readdirSync(dir, {withFileTypes:true}).flatMap(e => e.isDirectory() ? walk(path.join(dir,e.name)) : e.name.endsWith('.md') ? [path.join(dir,e.name)] : []);
}
const docs = [...walk(memory), ...['AGENTS.md','CLAUDE.md'].map(p=>path.join(root,p))].filter(p=>fs.existsSync(p));
for (const file of docs) {
  const body = read(file);
  for (const match of body.matchAll(/\[[^\]]*\]\(([^)]+)\)/gu)) {
    const target = match[1].replace(/^<|>$/gu, '').split('#')[0];
    if (!target || /^[a-z]+:/iu.test(target)) continue;
    if (!fs.existsSync(path.resolve(path.dirname(file), decodeURIComponent(target)))) errors.push('Broken link in '+path.relative(root,file)+': '+target);
  }
  if (path.dirname(file) === path.join(memory,'tasks')) {
    for (const key of ['Status','Owner','Updated','Scope']) if (!new RegExp('^'+key+': .+','m').test(body)) errors.push('Missing '+key+' in '+path.basename(file));
    if (!/^Status: (planned|ongoing|blocked|completed)\s*$/m.test(body)) errors.push('Invalid task status: '+path.basename(file));
    if (!/^## .*?(Next action|handoff)/im.test(body)) errors.push('No resume section: '+path.basename(file));
  }
}
const startup = ['AGENTS.md','docs/project-memory/START.md','docs/project-memory/STATE.md'];
if (startup.every(p=>fs.existsSync(path.join(root,p)))) {
  const sizes = Object.fromEntries(startup.map(p=>[p,words(read(path.join(root,p)))]));
  const total = Object.values(sizes).reduce((a,b)=>a+b,0);
  console.log(JSON.stringify({startupWords:sizes,totalStartupWords:total,markdownFiles:docs.length},null,2));
  if (total>1500) errors.push('Startup exceeds 1500 words: '+total);
  if (sizes['docs/project-memory/STATE.md']>500) errors.push('STATE exceeds 500 words');
}
if (errors.length) { console.error(errors.join('\n')); process.exitCode=1; }
else console.log('Project memory structure OK (truth and external artifacts require agent verification).');
