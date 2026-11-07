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
    vec4 worldOrigin;
    uvec2 landScene;
    uint enabled;
    uint hits[16];
    uint receiver[12];
    uint paired[16];
} state;
layout(set=1,binding=96) uniform sampler2D opaqueRadiance;
layout(set=1,binding=97) uniform sampler2D opaqueDepth;
// A cutout candidate can be confirmed when the opaque pass contains the same surface.
// A closer depth or an offscreen projection leaves its alpha unknown.
int compareDepth(vec3 point,out ivec2 pixel){
    if(point.z<=0.)return -1;
    vec2 uv=state.viewport.xy+(.5+.5*point.xy/(state.camera.xy*point.z))*state.viewport.zw+state.jitter;
    pixel=ivec2(floor(uv));
    if(any(lessThan(pixel,ivec2(0))) || any(greaterThanEqual(pixel,textureSize(opaqueDepth,0))))return -1;
    float depth=texelFetch(opaqueDepth,pixel,0).r;
    if(depth<=0.)return 0;
    float visible=1./max(1.e-7,depth*state.camera.z+state.camera.w);
    float tolerance=max(.015,.0025*point.z);
    if(abs(visible-point.z)<=tolerance)return 1;
    return visible>point.z?0:-1;
}
vec4 tracedReflection(vec4 screen,vec3 reflected,float level,vec4 fallback){
    if(state.enabled==0u || !(screen.w>0.) || level>3. || dot(reflected,reflected)<1.e-8)return fallback;
#ifdef FORWARD_QA
    atomicAdd(state.receiver[3*REFLECTION_CLASS],1u);
#endif
    ivec2 receiver=ivec2(floor(screen.xy));
    if(any(lessThan(receiver,ivec2(0))) || any(greaterThanEqual(receiver,textureSize(opaqueDepth,0))))return fallback;
    float opaque=texelFetch(opaqueDepth,receiver,0).r;
    if(screen.z+max(.000002,opaque*.0002)<opaque){
#ifdef FORWARD_QA
        atomicAdd(state.receiver[3*REFLECTION_CLASS+1],1u);
#endif
        return fallback;
    }
    float z=1./max(1.e-7,screen.z*state.camera.z+state.camera.w);
#ifdef FORWARD_QA
    if(abs(z-1./screen.w)>max(.015,.0025*z))atomicAdd(state.receiver[3*REFLECTION_CLASS+2],1u);
    atomicAdd(state.hits[4*REFLECTION_CLASS+0],1u);
#endif
    vec3 world=normalize(reflected);
    vec3 direction=normalize(vec3(dot(world,state.axisX.xyz),dot(world,state.axisY.xyz),dot(world,state.axisZ.xyz)));
    vec2 unit=(screen.xy-state.jitter-state.viewport.xy)/state.viewport.zw;
    vec3 origin=vec3((2.*unit-1.)*state.camera.xy*z,z);
    float bias=max(.003,.0001*z);
    rayQueryEXT ray;
    rayQueryInitializeEXT(ray,accelerationStructureEXT(state.scene),gl_RayFlagsNoneEXT,255,origin,bias,direction,5000.);
    float uncertain=5001.;
    while(rayQueryProceedEXT(ray)){
        if(rayQueryGetIntersectionTypeEXT(ray,false)!=gl_RayQueryCandidateIntersectionTriangleEXT)continue;
        float t=rayQueryGetIntersectionTEXT(ray,false);
        ivec2 samplePixel;
        int evidence=compareDepth(origin+direction*t,samplePixel);
        if(evidence==1)rayQueryConfirmIntersectionEXT(ray);
        if(evidence<0)uncertain=min(uncertain,t);
    }
    float distanceHit=5001.;
    if(rayQueryGetIntersectionTypeEXT(ray,true)==gl_RayQueryCommittedIntersectionTriangleEXT)
        distanceHit=rayQueryGetIntersectionTEXT(ray,true);
    if(any(notEqual(state.landScene,uvec2(0))) && distanceHit-.15>bias){
        vec3 worldStart=state.worldOrigin.xyz+state.axisX.xyz*origin.x+state.axisY.xyz*origin.y+state.axisZ.xyz*origin.z;
        rayQueryEXT terrain;
        rayQueryInitializeEXT(terrain,accelerationStructureEXT(state.landScene),gl_RayFlagsNoneEXT,255,
            worldStart,bias,world,min(5000.,distanceHit-.15));
        while(rayQueryProceedEXT(terrain)){
            // Unverified preplaced cutouts can block the terrain, but cannot supply reflected radiance.
            if(rayQueryGetIntersectionTypeEXT(terrain,false)==gl_RayQueryCandidateIntersectionTriangleEXT)
                uncertain=min(uncertain,rayQueryGetIntersectionTEXT(terrain,false));
        }
        if(rayQueryGetIntersectionTypeEXT(terrain,true)==gl_RayQueryCommittedIntersectionTriangleEXT)
            distanceHit=rayQueryGetIntersectionTEXT(terrain,true);
    }
    if(distanceHit>5000. || uncertain<distanceHit)return fallback;
#ifdef FORWARD_QA
    atomicAdd(state.hits[4*REFLECTION_CLASS+1],1u);
#endif
    ivec2 p;
    if(compareDepth(origin+direction*distanceHit,p)!=1)return fallback;
    // Only reuse a traced opaque sample when the projected intersection agrees with native depth.
    vec3 color=texelFetch(opaqueRadiance,p,0).rgb;
    if(any(isnan(color)) || any(isinf(color)))return fallback;
#ifdef FORWARD_QA
    atomicAdd(state.hits[4*REFLECTION_CLASS+2],1u);
    if(distance(color,fallback.rgb)>.015)atomicAdd(state.hits[4*REFLECTION_CLASS+3],1u);
#endif
    return vec4(mix(color,fallback.rgb,smoothstep(.5,3.,level)),fallback.a);
}
#ifdef FORWARD_PAIR
bool comparePair(){return (state.enabled&2u)!=0u;}
void recordPair(vec4 before,vec4 after,vec4 screen){
    if(!comparePair() || any(isnan(before)) || any(isnan(after)))return;
    ivec2 p=ivec2(floor(screen.xy));
    if(any(lessThan(p,ivec2(0))) || any(greaterThanEqual(p,textureSize(opaqueDepth,0))))return;
    float depth=texelFetch(opaqueDepth,p,0).r;
    if(screen.z+max(.000002,depth*.0002)<depth)return;
    float delta=max(max(abs(after.r-before.r),abs(after.g-before.g)),abs(after.b-before.b));
    atomicAdd(state.paired[4*REFLECTION_CLASS],1u);
    if(delta>.001){
        atomicAdd(state.paired[4*REFLECTION_CLASS+1],1u);
        if(after.a>.001)atomicAdd(state.paired[4*REFLECTION_CLASS+2],1u);
        atomicMax(state.paired[4*REFLECTION_CLASS+3],floatBitsToUint(delta));
    }
}
#endif
layout(location=0) out vec4 outputColor;
void main(){
    outputColor=tracedReflection(vec4(1),vec3(1),0.,vec4(1));
#ifdef FORWARD_PAIR
    if(comparePair())recordPair(vec4(0),outputColor,gl_FragCoord);
#endif
}
