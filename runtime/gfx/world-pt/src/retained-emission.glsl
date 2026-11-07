vec3 retainedEmission(uint id,vec2 bary,out bool available){
    available=false;
    PropInfo p=u.draws.land.props;
    if(p.emissionBanks==0u || id>=p.triangles)return vec3(0);
    Words data=Words(p.emissionData);
    uint group=p.tags.v[id]>>16u,entry=4u*group,bank=data.v[entry],stamp=p.activeGroups.v[group];
    if(bank==0u || bank>p.emissionBanks || stamp==0u || u.frame+1u-stamp>240u)return vec3(0);
    float strength=uintBitsToFloat(data.v[entry+2u]);
    if(!(strength>0. && strength<=64.))return vec3(0);
    vec2 uv=vec2(0);vec3 w=vec3(1.-bary.x-bary.y,bary);
    for(uint i=0u;i<3u;i++)uv+=w[i]*unpackHalf2x16(floatBitsToUint(p.mesh.v[3u*id+i].w));
    vec2 pos=fract(uv)*128.-.5,f=fract(pos);vec3 rgb=vec3(0);
    uint start=p.groups*4u+(bank-1u)*32768u;
    for(int y=0;y<2;y++)for(int x=0;x<2;x++){
        ivec2 xy=(ivec2(floor(pos))+ivec2(x,y))&127;
        uint k=start+2u*uint(xy.y*128+xy.x);
        rgb+=vec3(unpackHalf2x16(data.v[k]),unpackHalf2x16(data.v[k+1u]).x)*
             ((x==0?1.-f.x:f.x)*(y==0?1.-f.y:f.y));
    }
    available=true;
    return strength*rgb;
}
