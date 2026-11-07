#version 460
#extension GL_EXT_ray_query : require
layout(set=1,binding=33,std430) buffer ForwardScene {
    vec4 camera;
    vec4 axisX;
    vec4 axisY;
    vec4 axisZ;
    vec4 viewport;
    vec2 jitter;
    uvec2 scene;
    uint enabled;
    uint hits[16];
} state;
layout(set=1,binding=96) uniform sampler2D opaqueRadiance;
layout(set=1,binding=97) uniform sampler2D opaqueDepth;
vec4 tracedReflection(vec4 screen,vec3 cube,float level,vec4 fallback){
    if(state.enabled==0u || !(screen.w>0.) || level>3. || dot(cube,cube)<1.e-8)return fallback;
#ifdef FORWARD_QA
    atomicAdd(state.hits[4*REFLECTION_CLASS+0],1u);
#endif
    vec3 world=normalize(vec3(cube.xy,-cube.z));
    vec3 direction=normalize(vec3(dot(world,state.axisX.xyz),dot(world,state.axisY.xyz),dot(world,state.axisZ.xyz)));
    float z=1./screen.w;
    vec2 unit=(screen.xy-state.jitter-state.viewport.xy)/state.viewport.zw;
    vec3 origin=vec3((2.*unit-1.)*state.camera.xy*z,z);
    float bias=max(.003,.0001*z);
    rayQueryEXT ray;
    rayQueryInitializeEXT(ray,accelerationStructureEXT(state.scene),gl_RayFlagsNoneEXT,255,origin,bias,direction,5000.);
    float cutout=5001.;
    while(rayQueryProceedEXT(ray)){
        if(rayQueryGetIntersectionTypeEXT(ray,false)==gl_RayQueryCandidateIntersectionTriangleEXT)
            cutout=min(cutout,rayQueryGetIntersectionTEXT(ray,false));
    }
    if(rayQueryGetIntersectionTypeEXT(ray,true)!=gl_RayQueryCommittedIntersectionTriangleEXT)return fallback;
    float distanceHit=rayQueryGetIntersectionTEXT(ray,true);
    if(cutout<distanceHit)return fallback;
#ifdef FORWARD_QA
    atomicAdd(state.hits[4*REFLECTION_CLASS+1],1u);
#endif
    vec3 hit=origin+direction*distanceHit;
    if(hit.z<=0.)return fallback;
    vec2 center=state.viewport.xy+(.5+.5*hit.xy/(state.camera.xy*hit.z))*state.viewport.zw+state.jitter;
    ivec2 size=textureSize(opaqueDepth,0),p=ivec2(floor(center));
    if(any(lessThan(p,ivec2(0))) || any(greaterThanEqual(p,size)))return fallback;
    float depth=texelFetch(opaqueDepth,p,0).r;
    if(depth<=0.)return fallback;
    float visible=1./max(1.e-7,depth*state.camera.z+state.camera.w);
    if(abs(visible-hit.z)>max(.015,.0025*hit.z))return fallback;
    // Only reuse a traced opaque sample when the projected intersection agrees with native depth.
    vec3 color=texelFetch(opaqueRadiance,p,0).rgb;
    if(any(isnan(color)) || any(isinf(color)))return fallback;
#ifdef FORWARD_QA
    atomicAdd(state.hits[4*REFLECTION_CLASS+2],1u);
    if(distance(color,fallback.rgb)>.015)atomicAdd(state.hits[4*REFLECTION_CLASS+3],1u);
#endif
    return vec4(mix(color,fallback.rgb,smoothstep(.5,3.,level)),fallback.a);
}
layout(location=0) out vec4 outputColor;
void main(){outputColor=tracedReflection(vec4(1),vec3(1),0.,vec4(1));}
