from pathlib import Path
p=Path('work/v2/contact.comp')
s=p.read_text().replace('uint fittedError;','uint fittedError;uint terrainPrimary;uint terrainSecondary;uint terrainShadow;')
s=s.replace('layout(buffer_reference,std430,buffer_reference_align=16) buffer DrawTable {DrawMaterial draw[2048];Tint tint[];};', '''struct LandInfo {uvec2 scene;Vertices vertices;uint side;uint enabled;vec2 reserved;};
layout(buffer_reference,std430,buffer_reference_align=16) buffer DrawTable {DrawMaterial draw[2048];Tint tint[2048];LandInfo land;};''')
a=s.index('bool traceScene(');b=s.index('bool observedMaterial',a)
s=s[:a]+'''const uint landFlag=0x80000000u;
vec3 toWorldDirection(vec3 v){return u.axisX.xyz*v.x+u.axisY.xyz*v.y+u.axisZ.xyz*v.z;}
vec3 fromWorldDirection(vec3 v){return vec3(dot(v,u.axisX.xyz),dot(v,u.axisY.xyz),dot(v,u.axisZ.xyz));}
bool intersection(uvec2 scene,vec3 origin,vec3 direction,float nearT,float farT,out float distanceHit,out uint primitive,out vec2 bary){
    rayQueryEXT ray;
    rayQueryInitializeEXT(ray,accelerationStructureEXT(scene),gl_RayFlagsOpaqueEXT,255,origin,nearT,direction,farT);
    while(rayQueryProceedEXT(ray)){}
    if(rayQueryGetIntersectionTypeEXT(ray,true)!=gl_RayQueryCommittedIntersectionTriangleEXT)return false;
    distanceHit=rayQueryGetIntersectionTEXT(ray,true);
    bary=rayQueryGetIntersectionBarycentricsEXT(ray,true);
    primitive=uint(rayQueryGetIntersectionPrimitiveIndexEXT(ray,true));
    return true;
}
bool traceScene(vec3 origin,vec3 direction,float nearT,float farT,out vec3 hit,out uint primitive,out vec2 bary){
    float distanceHit;
    bool found=intersection(u.scene,origin,direction,nearT,farT,distanceHit,primitive,bary);
    // Give the guest's full-detail triangles precedence where the two terrain surfaces coincide.
    float limit=found?distanceHit-.15:farT;
    if(u.draws.land.enabled!=0u && limit>nearT){
        uint landPrim;vec2 landBary;float landDistance;
        if(intersection(u.draws.land.scene,u.origin.xyz+toWorldDirection(origin),toWorldDirection(direction),
                        nearT,limit,landDistance,landPrim,landBary)){
            primitive=landFlag|landPrim;bary=landBary;distanceHit=landDistance;found=true;
        }
    }
    if(found)hit=origin+direction*distanceHit;
    return found;
}
bool anyHit(uvec2 scene,vec3 origin,vec3 direction,float bias){
    rayQueryEXT ray;
    rayQueryInitializeEXT(ray,accelerationStructureEXT(scene),gl_RayFlagsOpaqueEXT|gl_RayFlagsTerminateOnFirstHitEXT,255,
        origin,bias,direction,5000.);
    while(rayQueryProceedEXT(ray)){}
    return rayQueryGetIntersectionTypeEXT(ray,true)!=gl_RayQueryCommittedIntersectionNoneEXT;
}
bool shadowed(vec3 origin,vec3 direction,float bias,inout uint landShadows){
    if(anyHit(u.scene,origin,direction,bias))return true;
    if(u.draws.land.enabled==0u)return false;
    // Offset the test above the reduced terrain to avoid self-occlusion at grazing angles.
    if(anyHit(u.draws.land.scene,u.origin.xyz+toWorldDirection(origin)+vec3(0,.07,0),toWorldDirection(direction),max(.07,bias))){
        landShadows++;return true;
    }
    return false;
}
vec3 faceNormal(uint primitive){
    if((primitive&landFlag)==0u){
        uint j=3u*primitive;vec3 a=u.vertices.v[j].xyz;
        return normalize(cross(u.vertices.v[j+1].xyz-a,u.vertices.v[j+2].xyz-a));
    }
    uint tri=primitive&~landFlag,side=u.draws.land.side,cell=tri/2u;
    uint a=cell/(side-1u)*side+cell%(side-1u),b=a+1u,c=a+side,d=c+1u;
    Vertices v=u.draws.land.vertices;
    vec3 n=tri%2u==0u?cross(v.v[c].xyz-v.v[a].xyz,v.v[b].xyz-v.v[a].xyz):
                        cross(v.v[c].xyz-v.v[b].xyz,v.v[d].xyz-v.v[b].xyz);
    return normalize(fromWorldDirection(n));
}
'''+s[b:]
s=s.replace('    uint first=1500000u+3u*primitive;', '    if((primitive&landFlag)!=0u){draw=2047u;value=vec3(.36);return false;}\n    uint first=1500000u+3u*primitive;')
s=s.replace('    bool depthMatch=false;', '    uint groundPrimary=0,groundSecondary=0,groundShadows=0;\n    bool depthMatch=false;')
s=s.replace('        uint j=3u*primitive;vec3 a=u.vertices.v[j].xyz;\n        vec3 normal=normalize(cross(u.vertices.v[j+1].xyz-a,u.vertices.v[j+2].xyz-a));','''        if((primitive&landFlag)!=0u){if(depth==0u)groundPrimary++;else groundSecondary++;}
        vec3 normal=faceNormal(primitive);''')
s=s.replace('shadowed(origin,lightDir,nearT)','shadowed(origin,lightDir,nearT,groundShadows)').replace('atomicAdd(c.drawColor,drawColor);','atomicAdd(c.drawColor,drawColor);atomicAdd(c.terrainPrimary,groundPrimary);atomicAdd(c.terrainSecondary,groundSecondary);atomicAdd(c.terrainShadow,groundShadows);')
p.write_text(s)
q=Path('work/v2/stream.inc');q.write_text(q.read_text().replace('drawCount==worldDrawLimit','drawCount>=worldDrawLimit-1'))
