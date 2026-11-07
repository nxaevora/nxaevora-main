vec3 retainedEmissionMip(uint id,vec2 bary,bool coarse,out bool available){
    available=false;
    PropInfo p=u.draws.land.props;
    if(p.emissionBanks==0u || id>=p.triangles)return vec3(0);
    Words data=Words(p.emissionData);
    uint group=p.tags.v[id]>>16u,entry=4u*group,bank=data.v[entry],stamp=p.activeGroups.v[group];
    bool detailed=(bank&0x80000000u)!=0u && !coarse;bank&=0x7fffffffu;
    if(bank==0u || bank>p.emissionBanks || stamp==0u || u.frame+1u-stamp>240u)return vec3(0);
    float strength=uintBitsToFloat(data.v[entry+2u]);
    if(!(strength>0. && strength<=64.))return vec3(0);
    vec2 uv=vec2(0);vec3 w=vec3(1.-bary.x-bary.y,bary);
    for(uint i=0u;i<3u;i++)uv+=w[i]*unpackHalf2x16(floatBitsToUint(p.mesh.v[3u*id+i].w));
    uvec2 extent=uvec2(128u);
    uint start=p.groups*4u+(bank-1u)*32768u;
    if(detailed){
        uint table=retainedDistributionOffset(p)+2u*p.lightTriangles+1u,entry=table+4u*(bank-1u);
        start=table+data.v[entry];extent=uvec2(data.v[entry+1u],data.v[entry+2u]);
    }
    vec2 pos=fract(uv)*vec2(extent)-.5,f=fract(pos);vec3 rgb=vec3(0);
    for(int y=0;y<2;y++)for(int x=0;x<2;x++){
        ivec2 xy=(ivec2(floor(pos))+ivec2(x,y)+ivec2(extent))%ivec2(extent);
        uint k=start+2u*(uint(xy.y)*extent.x+uint(xy.x));
        rgb+=vec3(unpackHalf2x16(data.v[k]),unpackHalf2x16(data.v[k+1u]).x)*
             ((x==0?1.-f.x:f.x)*(y==0?1.-f.y:f.y));
    }
    available=true;
    return strength*rgb;
}

vec3 retainedEmission(uint id,vec2 bary,out bool available){return retainedEmissionMip(id,bary,false,available);}
