void main(){
    uint instance=gl_WorkGroupID.x,lane=gl_LocalInvocationIndex;
    GroveInfo grove=u.draws.land.grove;
    if(instance>=grove.instanceCount || lane>=8u)return;
    TreePlacement p=grove.placements.items[instance];TreeShape shape=grove.shapes.items[p.key.x];
    uint id=(shape.count*(2u*lane+1u))/16u;
    uint start=3u*(shape.first+id);
    vec3 a=treeWorld(p,grove.mesh.v[start].xyz);
    vec3 b=treeWorld(p,grove.mesh.v[start+1u].xyz);
    vec3 c=treeWorld(p,grove.mesh.v[start+2u].xyz);
    vec3 n=cross(b-a,c-a);
    if(dot(n,n)<1.e-8)return;
    vec3 dir=normalize(n),center=(a+b+c)/3.;
    rayQueryEXT ray;
    rayQueryInitializeEXT(ray,accelerationStructureEXT(u.draws.land.scene),gl_RayFlagsNoOpaqueEXT,255,
        center+.03*dir,.001,-dir,.07);
    bool found=false;
    while(rayQueryProceedEXT(ray)){
        if(rayQueryGetIntersectionInstanceCustomIndexEXT(ray,false)==instance+2u &&
           rayQueryGetIntersectionPrimitiveIndexEXT(ray,false)==id)found=true;
    }
    Words stats=Words(u.stats);
    atomicAdd(stats.v[11800],1u);
    if(found)atomicAdd(stats.v[11801],1u);
    if(p.key.y==1u){
        atomicAdd(stats.v[11803],1u);if(found)atomicAdd(stats.v[11804],1u);
    }
    if(grove.activeGroups.v[grove.instanceCount+p.key.x]!=0u)atomicAdd(stats.v[11802],1u);
}
