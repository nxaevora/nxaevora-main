from pathlib import Path
base=Path('work/v2')
p=base/'terrain.inc';s=p.read_text().replace('#include "grove.inc"','#include "grove.inc"\n#include "terrain-splat.inc"').replace('GroveInfo grove;} LandInfo;','GroveInfo grove;SplatInfo splat;} LandInfo;').replace('sizeof(LandInfo)==208','sizeof(LandInfo)==224').replace('    memcpy(land.gpu.grid,','    if(splat_load(&splats[id],id,(u32)(request>>8)))land.gpu.splat=splats[id].gpu;\n    memcpy(land.gpu.grid,');p.write_text(s)
p=base/'material.inc';s=p.read_text().replace('    if(shader)switch(shader->hash){','    if(shader && shader->hash==0xea4d86b0272a581full)draw->slot|=0x80000000u;\n    if(shader)switch(shader->hash){').replace('draws[j].slot=0;','draws[j].slot&=0x80000000u;');p.write_text(s)
p=base/'contact.h';s=p.read_text().replace('!(draws[j].slot>>8)','!((draws[j].slot>>8)&255)').replace('if(draws[j].slot>>8)','if((draws[j].slot>>8)&255)');p.write_text(s)
p=base/'contact.comp';s=p.read_text().replace('struct LandInfo {','struct SplatInfo {Words data;uint side;uint banks;};\nstruct LandInfo {').replace('GroveInfo grove;};','GroveInfo grove;SplatInfo splat;};').replace('bool sourceMaterial(uint primitive,','#include "terrain-splat.glsl"\nbool sourceMaterial(uint primitive,')
k=s.index('        if(dot(normal,direction)>0.)')
s=s[:k]+'''        if(!fromBuffers && isTerrainMaterial(primitive) && u.draws.land.splat.side!=0u &&
           (depth!=0u || (any(notEqual(u.stats,uvec2(0))) && all(equal(pixel%32,ivec2(9,11)))))){
            vec3 detail;float nativeRough;
            vec3 position=u.origin.xyz+toWorldDirection(hit);
            vec3 slope=normalize(toWorldDirection(normal));
            float footprint=max(.0001,2.*u.camera.y*abs(hit.z)/u.draws.land.viewport.w);
            if(nativeTerrainMaterial(position,slope,footprint,detail,nativeRough)){
                if(depth!=0u){
                    vec3 sampled,emission;
                    if(!observedMaterial(hit,size,sampled,emission))base=detail;
                    rough=nativeRough;metal=0.;pbr=true;
                    if(any(notEqual(u.stats,uvec2(0))) && all(equal(pixel%8,ivec2(0))))atomicAdd(Words(u.stats).v[11905],1u);
                }else if(hasInput(6u) && abs(texelFetch(albedoMap,pixel,0).a-66./255.)<.001){
                    vec3 actual=texelFetch(albedoMap,pixel,0).rgb;
                    Words stats=Words(u.stats);
                    atomicAdd(stats.v[11903],1u);atomicAdd(stats.v[11904],uint(round(65535.*dot(abs(actual-detail),vec3(1./3.)))));
                }
            }
        }
'''+s[k:]
p.write_text(s)
p=base/'emitter-sampling.glsl';s=p.read_text().replace('(material.slot>>8u)!=0u','((material.slot>>8u)&255u)!=0u');p.write_text(s)
p=base/'contact.h';s=p.read_text().replace('depthConfirmedOpaque=%u','depthConfirmedOpaque=%u splatUsed=%u splatSamples=%u splatErrorSum=%u').replace('contact.mappedCounters[16375]);','contact.mappedCounters[16375],contact.mappedCounters[11905],contact.mappedCounters[11903],contact.mappedCounters[11904]);');p.write_text(s)
