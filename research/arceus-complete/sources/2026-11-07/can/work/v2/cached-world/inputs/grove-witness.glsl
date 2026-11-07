void main(){
    uint draw=gl_WorkGroupID.x;
    DrawMaterial m=u.draws.draw[draw];uint count=m.end-(draw==0u?0u:u.draws.draw[draw-1u].end);
    GroveInfo grove=u.draws.land.grove;
    bool possible=false;
    for(uint i=0u;i<grove.shapeCount;i++)if(grove.shapes.items[i].count==count){possible=true;break;}
    if(!possible)return;
    for(uint i=gl_LocalInvocationIndex;i<grove.instanceCount;i+=64u){
        TreePlacement p=grove.placements.items[i];TreeShape s=grove.shapes.items[p.key.x];
        if(s.count!=count)continue;
        bool matches=true;
        for(uint k=0u;k<8u;k++){
            uint index=k<4u?s.anchor[k]:((2u*k-7u)*(3u*count))/8u;
            vec4 model=grove.mesh.v[3u*s.first+index];
            vec3 v=u.vertices.v[3u*m.base+index].xyz;
            vec3 world=u.origin.xyz+u.axisX.xyz*v.x+u.axisY.xyz*v.y+u.axisZ.xyz*v.z;
            vec2 uv=u.vertices.v[1500000u+3u*m.base+index].xy;
            if(!(distance(world,treeWorld(p,model.xyz))<.035) ||
               !all(lessThan(abs(uv-unpackHalf2x16(floatBitsToUint(model.w))),vec2(.004)))){matches=false;break;}
        }
        if(matches){
            atomicExchange(grove.activeGroups.v[i],u.frame+1u);
            atomicOr(grove.activeGroups.v[grove.instanceCount+p.key.x],1u);
        }
    }
}
