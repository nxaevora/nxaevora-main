const uint treeBit=0x20000000u;
uint treeInstance(uint id){return (id&~treeBit)>>14u;}
uint treePrimitive(uint id){return id&16383u;}
bool treeSurface(uint instance,uint primitive){
    GroveInfo g=u.draws.land.grove;
    return instance<g.instanceCount && primitive<g.shapes.items[g.placements.items[instance].key.x].count;
}
vec4 treeTexel(uint instance,uint id,vec2 bary){
    GroveInfo g=u.draws.land.grove;
    uint model=g.placements.items[instance].key.x;
    TreeShape shape=g.shapes.items[model];uint v=3u*(shape.first+id);
    vec2 uv=unpackHalf2x16(floatBitsToUint(g.mesh.v[v].w))*(1.-bary.x-bary.y)+
            unpackHalf2x16(floatBitsToUint(g.mesh.v[v+1u].w))*bary.x+
            unpackHalf2x16(floatBitsToUint(g.mesh.v[v+2u].w))*bary.y;
    return samplePropAtlas(g.colors,model,uv);
}
bool acceptsInstance(uint custom,uint id,vec2 bary){
    if(custom<2u)return custom==1u && acceptsProp(id,bary);
    uint instance=custom-2u;
    if(!treeSurface(instance,id))return false;
    GroveInfo g=u.draws.land.grove;
    uint model=g.placements.items[instance].key.x;
    if(g.activeGroups.v[g.instanceCount+model]==0u || g.activeGroups.v[instance]==u.frame+1u)return false;
    uint cut=g.shapes.items[model].cut;
    return cut==0u || treeTexel(instance,id,bary).a>=float(cut)/255.;
}
vec3 treeFace(uint id){
    GroveInfo g=u.draws.land.grove;
    TreePlacement p=g.placements.items[treeInstance(id)];
    uint v=3u*(g.shapes.items[p.key.x].first+treePrimitive(id));
    vec3 a=g.mesh.v[v].xyz;
    vec3 ab=g.mesh.v[v+1u].xyz-a,ac=g.mesh.v[v+2u].xyz-a;
    vec3 b=vec3(dot(p.x.xyz,ab),dot(p.y.xyz,ab),dot(p.z.xyz,ab));
    vec3 c=vec3(dot(p.x.xyz,ac),dot(p.y.xyz,ac),dot(p.z.xyz,ac));
    return normalize(fromWorldDirection(cross(b,c)));
}
