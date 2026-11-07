from pathlib import Path
v=Path('work/v2')
p=v/'props.inc';s=p.read_text().replace('h.version<=7','h.version<=8').replace('h.version==7)ok=fread(&lightCount','h.version>=7)ok=fread(&lightCount')
s=s.replace('    if(!ok){fclose(f);return false;}','''    u32 detailWords=0;
    if(ok && h.version==8)ok=fread(&detailWords,sizeof detailWords,1,f)==1 &&
        detailWords>=4u*emitCount && detailWords<=16u*1024u*1024u;
    if(!ok){fclose(f);return false;}''')
s=s.replace('u64 total=cdf+(lightCount?8ull*lightCount+4:0);','u64 detail=cdf+(lightCount?8ull*lightCount+4:0),total=detail+4ull*detailWords;')
marker='        if(ok)ok=fgetc(f)==EOF;'
assert s.count(marker)==1
s=s.replace(marker,'''        if(ok && detailWords){
            const u32 *table=(const u32*)(data+detail);
            u64 cursor=4ull*emitCount;
            for(u32 i=0;ok && i<emitCount;i++){
                const u32 *entry=table+4*i;u32 width=entry[1],height=entry[2];
                ok=entry[0]==cursor && width>0 && width<=4096 && height>0 && height<=4096 && entry[3]==0;
                cursor+=2ull*width*height;
            }
            ok=ok && cursor==detailWords;
            const u16 *pixels=(const u16*)(table+4u*emitCount);
            for(u64 i=0;ok && i<2ull*(detailWords-4u*emitCount);i++)if(pixels[i]>0x3c00u)ok=false;
            if(ok){
                u32 *groups=(u32*)(data+emit);
                for(u32 i=0;i<h.groups;i++)if(groups[4*i])groups[4*i]|=0x80000000u;
            }
        }
'''+marker)
p.write_text(s)
p=v/'retained-emission.glsl';s=p.read_text()
s=s.replace('vec3 retainedEmission(uint id,vec2 bary,out bool available)','vec3 retainedEmissionMip(uint id,vec2 bary,bool coarse,out bool available)')
s=s.replace('    if(bank==0u','    bool detailed=(bank&0x80000000u)!=0u && !coarse;bank&=0x7fffffffu;\n    if(bank==0u')
s=s.replace('    vec2 pos=fract(uv)*128.-.5,f=fract(pos);vec3 rgb=vec3(0);','    uvec2 extent=uvec2(128u);')
s=s.replace('    for(int y=0;y<2;y++)','''    if(detailed){
        uint table=retainedDistributionOffset(p)+2u*p.lightTriangles+1u,entry=table+4u*(bank-1u);
        start=table+data.v[entry];extent=uvec2(data.v[entry+1u],data.v[entry+2u]);
    }
    vec2 pos=fract(uv)*vec2(extent)-.5,f=fract(pos);vec3 rgb=vec3(0);
    for(int y=0;y<2;y++)''')
s=s.replace('ivec2 xy=(ivec2(floor(pos))+ivec2(x,y))&127;','ivec2 xy=(ivec2(floor(pos))+ivec2(x,y)+ivec2(extent))%ivec2(extent);')
s=s.replace('uint(xy.y*128+xy.x)','(uint(xy.y)*extent.x+uint(xy.x))')
p.write_text(s+'\nvec3 retainedEmission(uint id,vec2 bary,out bool available){return retainedEmissionMip(id,bary,false,available);}\n')
p=v/'emission-witness.glsl';s=p.read_text().replace('        if(any(greaterThan(max(archived,ground),vec3(.004)))){','''        bool hasCoarse;vec3 coarse=retainedEmissionMip(tri,w.yz,true,hasCoarse);
        if(any(greaterThan(max(max(archived,coarse),ground),vec3(.004)))){
            atomicAdd(stats.v[11974],uint(round(4096.*dot(abs(ground-coarse),vec3(1./3.)))));''')
p.write_text(s)
p=v/'contact.h';s=p.read_text();mk='        LOG("world-pt: archive emitter groups='
s=s.replace(mk,r'''        LOG("world-pt: archive emitter coarse error=%u\n",contact.mappedCounters[11974]);
'''+mk);p.write_text(s)
