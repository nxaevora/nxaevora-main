from pathlib import Path
w=Path.cwd()/'work'
pt=w/'pt'
shader=(w/'rt/contact.comp').read_text().replace('uint hits;','uint hits; uint secondHits; uint litSecondHits;',1).replace('uvec2 statsAddress;','uvec2 statsAddress; uint frame;')
shader=shader.replace('#else\nvec3 closestDerivative','#else\n'+(pt/'indirect.glsl').read_text()+'\nvec3 closestDerivative')
shader=shader.replace('    if(any(notEqual(rt.statsAddress','    uint secondHits,litSecondHits;\n    vec3 indirect=indirectLight(p,pos,n,color.rgb,secondHits,litSecondHits);\n    if(any(notEqual(rt.statsAddress')
shader=shader.replace('atomicAdd(stats.hits,hits);','atomicAdd(stats.hits,hits); atomicAdd(stats.secondHits,secondHits); atomicAdd(stats.litSecondHits,litSecondHits);').replace('occlusion*.25),color.a','occlusion*.25)+.24*indirect,color.a')
shader=shader.replace('a=vec3(0); b=a; c=a;', 'a=vec3(uintBitsToFloat(0x7fc00000u),0,0); b=a; c=a;')
(pt/'contact.comp').write_text(shader)
host=(w/'rt/contact.h').read_text().replace('&contact.counters,8,','&contact.counters,16,').replace('contact.counters.memory,0,8','contact.counters.memory,0,16').replace('mappedCounters,0,8','mappedCounters,0,16')
host=host.replace('u64 vertices,scene,stats;', 'u64 vertices,scene,stats;u32 frame;').replace('.stats=contact.counters.address}', '.stats=contact.counters.address,.frame=g_vk_present}').replace('sampled GPU rays=%u hits=%u','sampled AO rays=%u hits=%u secondBounceHits=%u litSecondBounces=%u').replace('contact.mappedCounters[1]);','contact.mappedCounters[1],contact.mappedCounters[2],contact.mappedCounters[3]);').replace('contact AO initialization','two-bounce diffuse path tracing initialization')
(pt/'contact.h').write_text(host)
build=(w/'build-rt.py').read_text().replace("here/'work/rt'","here/'work/pt'").replace('village-rt-runtime','village-pt-runtime').replace('v3-RT-DLSS','v3-PT-DLSS').replace('current-village-RT-DLSS','current-village-PT-DLSS')
exec(compile(build,str(w/'build-rt.py'),'exec'))

