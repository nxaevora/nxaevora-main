shared uint ignored;
void main(){
    uint group=gl_WorkGroupID.x,lane=gl_LocalInvocationIndex;
    PropInfo props=u.draws.land.props;
    if(group>=props.groups || lane>=8u)return;
    uint lo=0u,hi=props.triangles;
    while(lo<hi){
        uint mid=(lo+hi)/2u;
        if((props.tags.v[mid]>>16u)<group)lo=mid+1u;else hi=mid;
    }
    uint size=props.checks.items[group].key.x;
    uint id=lo+(size*(2u*lane+1u))/16u;
    if(id>=props.triangles || (props.tags.v[id]>>16u)!=group)return;
    vec3 a=props.mesh.v[3u*id].xyz,b=props.mesh.v[3u*id+1u].xyz,c=props.mesh.v[3u*id+2u].xyz;
    vec3 n=cross(b-a,c-a);
    if(dot(n,n)<1.e-10)return;
    vec3 dir=normalize(n),center=(a+b+c)/3.;
    rayQueryEXT ray;
    rayQueryInitializeEXT(ray,accelerationStructureEXT(u.draws.land.scene),gl_RayFlagsNoOpaqueEXT,255,
        center+.03*dir,.001,-dir,.07);
    bool found=false;
    while(rayQueryProceedEXT(ray)){
        if(rayQueryGetIntersectionInstanceCustomIndexEXT(ray,false)==1 &&
           rayQueryGetIntersectionPrimitiveIndexEXT(ray,false)==id)found=true;
    }
    Words statistics=Words(u.stats);
    atomicAdd(statistics.v[11800],1u);
    if(found)atomicAdd(statistics.v[11801],1u);
    if(props.classCount>0u && props.activeGroups.v[props.groups+(props.tags.v[id]&255u)]!=0u)
        atomicAdd(statistics.v[11802],1u);
}
