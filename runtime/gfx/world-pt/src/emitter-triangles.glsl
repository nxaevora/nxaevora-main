// Prefix sums over each draw's BLAS-ordered triangles. World-space area and a
// four-point quadrature on the bound emission map approximate emitted flux.
shared float sums[64];
void main(){
    uint draw=gl_WorkGroupID.x,lane=gl_LocalInvocationIndex;
    DrawMaterial m=u.draws.draw[draw];
    Tint t=u.draws.tint[draw];
    bool mapped=m.emission!=0u && m.emission<textureCapacity;
    bool measured=t.emission.w>=3u && t.source.w!=0u;
    if(!mapped && !measured){
        if(lane==0u)u.draws.emitterCdf[draw]=0.;
        return;
    }
    uint count=m.end-(draw==0u?0u:u.draws.draw[draw-1u].end);
    uint first=m.base+(count*lane)/64u,end=m.base+(count*(lane+1u))/64u;
    vec3 mean=measured?vec3(t.emission.rgb)/(1024.*float(t.emission.w)):vec3(0);
    float sum=0.;
    for(uint i=first;i<end;i++){
        uint v=3u*i;
        vec3 a=u.vertices.v[v].xyz;
        float area=.5*length(cross(u.vertices.v[v+1u].xyz-a,u.vertices.v[v+2u].xyz-a));
        vec3 radiance=mean;
        if(mapped){
            uint j=1500000u+v;
            vec2 uv0=u.vertices.v[j].xy,uv1=u.vertices.v[j+1u].xy,uv2=u.vertices.v[j+2u].xy;
            vec2 center=(uv0+uv1+uv2)/3.;
            if(!any(isnan(center)) && !any(isinf(center))){
                radiance=.25*m.emissionScale*(
                    textureLod(drawTextures[nonuniformEXT(m.emission)],center,1.).rgb+
                    textureLod(drawTextures[nonuniformEXT(m.emission)],mix(center,uv0,.65),1.).rgb+
                    textureLod(drawTextures[nonuniformEXT(m.emission)],mix(center,uv1,.65),1.).rgb+
                    textureLod(drawTextures[nonuniformEXT(m.emission)],mix(center,uv2,.65),1.).rgb);
            }
        }
        float power=area*dot(radiance,vec3(.2126,.7152,.0722));
        if(power>0. && !isinf(power))sum+=min(power,1.e15);
        u.draws.triangleCdf[i]=sum;
    }
    sums[lane]=sum;
    barrier();
    float prefix=0.;
    for(uint i=0u;i<lane;i++)prefix+=sums[i];
    for(uint i=first;i<end;i++)u.draws.triangleCdf[i]+=prefix;
    if(lane==63u)u.draws.emitterCdf[draw]=prefix+sum;
}
