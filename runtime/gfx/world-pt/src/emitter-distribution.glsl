// One workgroup builds a distribution over captured emissive draws. A small uniform
// component in the sampler covers luminous texels missed by these area/power estimates.
shared float blockSums[64];
shared uint blockCounts[64];
void main(){
    uint lane=gl_LocalInvocationIndex;
    PropInfo p=u.draws.land.props;
    if(p.lightTriangles!=0u){
        Words data=Words(p.emissionData);
        for(uint j=lane;j<p.groups;j+=64u){
            uint stamp=p.activeGroups.v[j];
            if(stamp!=0u && stamp!=u.frame+1u && u.frame+1u-stamp<=240u && data.v[4u*j]!=0u && data.v[4u*j+2u]!=0u)
                atomicExchange(data.v[retainedDistributionOffset(p)+2u*p.lightTriangles],u.frame+1u);
        }
    }
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
            sum+=u.draws.emitterCdf[draw];
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
