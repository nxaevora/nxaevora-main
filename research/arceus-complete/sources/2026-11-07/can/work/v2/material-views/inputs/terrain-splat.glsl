bool isTerrainMaterial(uint primitive){
    if((primitive&landFlag)!=0u)return true;
    if(primitive>=u.count)return false;
    uint draw=uint(u.vertices.v[1500000u+3u*primitive].w);
    return draw<uint(u.axisX.w) && (u.draws.draw[draw].slot&0x80000000u)!=0u;
}
vec4 splatWeights(Words data,uint bank,vec2 unit){
    uint side=u.draws.land.splat.side;
    uint base=32u*u.draws.land.splat.banks+bank*side*side;
    vec2 pos=clamp(unit,vec2(0),vec2(1))*float(side-1u);
    uvec2 p=min(uvec2(pos),uvec2(side-2u));vec2 f=pos-vec2(p);
    uint i=base+p.y*side+p.x;
    return mix(mix(unpackUnorm4x8(data.v[i]),unpackUnorm4x8(data.v[i+1u]),f.x),
        mix(unpackUnorm4x8(data.v[i+side]),unpackUnorm4x8(data.v[i+side+1u]),f.x),f.y);
}
vec3 decodeTerrainRgb(uint packed){
    return vec3(uvec3(packed,packed>>10u,packed>>20u)&1023u)/1023.;
}
vec3 sampleTerrainLevel(Words data,uint base,vec2 uv,uint mip){
    uint side=128u>>mip;
    uint offset=(16384u-side*side)*4u/3u;
    vec2 pos=fract(uv)*float(side)-.5;ivec2 p=ivec2(floor(pos));vec2 f=fract(pos);
    uvec2 a=uvec2(p)&(side-1u),b=(a+1u)&(side-1u);
    return mix(mix(decodeTerrainRgb(data.v[base+offset+a.y*side+a.x]),decodeTerrainRgb(data.v[base+offset+a.y*side+b.x]),f.x),
        mix(decodeTerrainRgb(data.v[base+offset+b.y*side+a.x]),decodeTerrainRgb(data.v[base+offset+b.y*side+b.x]),f.x),f.y);
}
bool nativeTerrainMaterial(vec3 world,vec3 slope,float footprint,out vec3 color,out float roughness){
    SplatInfo s=u.draws.land.splat;
    if(s.side==0u)return false;
    vec2 unit=(world.xz-u.draws.land.grid.xy)*u.draws.land.grid.zw/512.;
    if(any(lessThan(unit,vec2(0))) || any(greaterThan(unit,vec2(1))))return false;
    vec2 local=world.xz-u.draws.land.grid.xy;
    color=vec3(0);roughness=0.;float sum=0.,unknown=0.;
    uint atlas=32u*s.banks+s.side*s.side*s.banks;
    for(uint bank=0u;bank<s.banks;bank++){
        vec4 w=splatWeights(s.data,bank,unit);
        for(uint j=0u;j<4u;j++){
            float weight=w[j];
            if(weight<=.001)continue;
            uint slot=4u*bank+j,meta=slot*8u;
            if(s.data.v[meta+5u]==0u){unknown+=weight;continue;}
            vec2 scale=vec2(uintBitsToFloat(s.data.v[meta]),uintBitsToFloat(s.data.v[meta+1u]));
            vec2 offset=vec2(uintBitsToFloat(s.data.v[meta+2u]),uintBitsToFloat(s.data.v[meta+3u]));
            uint level=uint(clamp(floor(log2(max(.001,128.*footprint*max(scale.x,scale.y)))+.5),0.,7.));
            uint base=atlas+slot*21845u;
            vec3 rgb=sampleTerrainLevel(s.data,base,local*scale+offset,level);
            if(slot==0u){
                vec3 n2=slope*slope;
                rgb=rgb*n2.y+sampleTerrainLevel(s.data,base,vec2(world.y,local.y)*scale+offset,level)*n2.x+
                    sampleTerrainLevel(s.data,base,vec2(local.x,world.y)*scale+offset,level)*n2.z;
            }
            color+=weight*rgb;
            roughness+=weight*uintBitsToFloat(s.data.v[meta+4u]);sum+=weight;
        }
    }
    if(!(sum>.01) || unknown>.01*sum)return false;
    color/=sum;roughness=clamp(roughness/sum,.04,1.);return true;
}
