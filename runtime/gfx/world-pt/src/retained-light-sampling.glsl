bool retainedLightsAvailable(PropInfo p){
    if(p.lightTriangles==0u)return false;
    return Words(p.emissionData).v[retainedDistributionOffset(p)+2u*p.lightTriangles]==u.frame+1u;
}
float retainedLightPdf(PropInfo p,uint tri,uint entry,vec3 wi,float distance2){
    Words data=Words(p.emissionData);uint base=retainedDistributionOffset(p);
    if(entry>=p.lightTriangles || data.v[base+2u*entry]!=tri)return 0.;
    float prior=entry==0u?0.:uintBitsToFloat(data.v[base+2u*entry-1u]);
    float relative=max(0.,uintBitsToFloat(data.v[base+2u*entry+1u])-prior);
    float probability=.05/float(p.lightTriangles)+.95*relative;
    vec3 a=p.mesh.v[3u*tri].xyz;
    vec3 area=cross(p.mesh.v[3u*tri+1u].xyz-a,p.mesh.v[3u*tri+2u].xyz-a);
    float projected=abs(dot(area,toWorldDirection(wi)));
    return projected>1.e-10?2.*distance2*probability/projected:0.;
}
float retainedHitPdf(PropInfo p,uint tri,vec3 wi,float distance2){
    uint group=p.tags.v[tri]>>16u,base=Words(p.emissionData).v[4u*group+3u];
    uint stamp=p.activeGroups.v[group];
    if(base==0u || stamp==0u || stamp==u.frame+1u || u.frame+1u-stamp>240u || Words(p.emissionData).v[4u*group+2u]==0u)return 0.;
    uint first=(uint(p.checks.items[group].position[1].w)-1u)/3u;
    return retainedLightPdf(p,tri,base-1u+tri-first,wi,distance2);
}
vec3 connectRetainedLight(PropInfo p,vec3 origin,vec3 normal,vec3 outgoing,float bias,
                          vec3 diffuseThroughput,vec3 specularThroughput,vec3 f0,float rough,
                          float diffuseProbability,bool continues,uint seed,out bool visible){
    visible=false;
    uint count=p.lightTriangles,entry=min(uint(sample01(seed)*float(count)),count-1u);
    Words data=Words(p.emissionData);uint base=retainedDistributionOffset(p);
    if(sample01(seed+1u)>=.05){
        uint low=0u,high=count-1u;float target=sample01(seed+2u);
        while(low<high){uint mid=(low+high)/2u;if(target<uintBitsToFloat(data.v[base+2u*mid+1u]))high=mid;else low=mid+1u;}
        entry=low;
    }
    uint tri=data.v[base+2u*entry];
    if(p.activeGroups.v[p.tags.v[tri]>>16u]==u.frame+1u)return vec3(0);
    float r=sqrt(sample01(seed+3u)),s=sample01(seed+4u);vec3 w=vec3(1.-r,r*(1.-s),r*s);
    if(!acceptsProp(tri,w.yz))return vec3(0);
    bool available;vec3 glow=retainedEmission(tri,w.yz,available);
    if(!available || !any(greaterThan(glow,vec3(0))))return vec3(0);
    vec3 point=p.mesh.v[3u*tri].xyz*w.x+p.mesh.v[3u*tri+1u].xyz*w.y+p.mesh.v[3u*tri+2u].xyz*w.z;
    vec3 delta=fromWorldDirection(point-u.origin.xyz)-origin;
    float dist=length(delta);if(!(dist>bias*3. && dist<5000.))return vec3(0);
    vec3 wi=delta/dist;float cosine=dot(normal,wi);
    if(!(cosine>0.))return vec3(0);
    float density=retainedLightPdf(p,tri,entry,wi,dist*dist);
    if(!(density>0.))return vec3(0);
    vec3 blocker;uint id;vec2 bary;
    if(traceScene(origin,wi,bias,dist-max(bias,dist*1.e-5),blocker,id,bary))return vec3(0);
    visible=true;
    return glow*emitterBrdf(diffuseThroughput,specularThroughput,normal,outgoing,wi,f0,rough,cosine,density,diffuseProbability,continues);
}
