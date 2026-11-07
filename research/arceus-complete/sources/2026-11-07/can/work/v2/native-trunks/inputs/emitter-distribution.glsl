// One workgroup builds a distribution over captured emissive draws. A small uniform
// component in the sampler covers luminous texels missed by these area/power estimates.
shared float blockSums[64];
shared uint blockCounts[64];
void main(){
    uint lane=gl_LocalInvocationIndex;
    uint n=uint(u.axisX.w);
    uint begin=lane*32u,end=min(begin+32u,n);
    float sum=0.;uint eligible=0u;
    for(uint draw=begin;draw<end;draw++){
        DrawMaterial m=u.draws.draw[draw];
        uint first=draw==0u?0u:u.draws.draw[draw-1u].end;
        uint count=m.end-first;
        Tint t=u.draws.tint[draw];
        bool mapped=m.emission!=0u && m.emission<textureCapacity;
        bool measured=t.emission.w>=3u && t.source.w!=0u;
        if(count!=0u && (mapped || measured)){
            eligible+=count;
            vec3 mean=measured?vec3(t.emission.rgb)/(1024.*float(t.emission.w)):vec3(0);
            float estimate=0.;
            for(uint i=0u;i<16u;i++){
                uint v=3u*(m.base+min(count-1u,((2u*i+1u)*count)/32u));
                vec3 a=u.vertices.v[v].xyz;
                float area=.5*length(cross(u.vertices.v[v+1u].xyz-a,u.vertices.v[v+2u].xyz-a));
                uint uvBase=1500000u+v;
                vec2 uv=(u.vertices.v[uvBase].xy+u.vertices.v[uvBase+1u].xy+u.vertices.v[uvBase+2u].xy)/3.;
                if(!(area>0.) || isinf(area) || any(isnan(uv)) || any(isinf(uv)))continue;
                vec3 radiance=mean;
                if(mapped)radiance=max(radiance,m.emissionScale*textureLod(drawTextures[nonuniformEXT(m.emission)],uv,4.).rgb);
                estimate+=area*dot(radiance,vec3(.2126,.7152,.0722));
            }
            sum+=min(1.e20,max(0.,estimate*float(count)/16.));
        }
        u.draws.emitterCdf[draw]=sum;u.draws.emitterEnd[draw]=eligible;
    }
    blockSums[lane]=sum;blockCounts[lane]=eligible;
    barrier();
    // Fixed addition order ensures identical endpoints across draw blocks.
    float prefix=0.;uint firstEligible=0u;
    for(uint i=0u;i<lane;i++){prefix+=blockSums[i];firstEligible+=blockCounts[i];}
    for(uint draw=begin;draw<end;draw++){u.draws.emitterCdf[draw]+=prefix;u.draws.emitterEnd[draw]+=firstEligible;}
    if(lane==0u && any(notEqual(u.stats,uvec2(0)))){
        float total=0.;uint triangles=0u;
        for(uint i=0u;i<64u;i++){total+=blockSums[i];triangles+=blockCounts[i];}
        Words(u.stats).v[16379]=triangles;
        Words(u.stats).v[16381]=floatBitsToUint(total);
    }
}
