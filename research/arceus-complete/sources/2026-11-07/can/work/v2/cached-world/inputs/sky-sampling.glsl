const float invSphere=1./12.5663706144;
uint skyIndex(float u,uint count){return min(uint(clamp(u,0.,1.)*float(count)),count-1u);}
float skyPdf(vec3 world){
    float total=u.draws.skyRows[31];
    if(!(total>0.))return invSphere;
    uint x=skyIndex(fract(atan(world.z,world.x)/6.28318530718+.5),64u);
    uint y=skyIndex(.5-.5*world.y,32u),k=64u*y+x;
    float mass=max(0.,u.draws.skyColumns[k]-(x!=0u?u.draws.skyColumns[k-1u]:0.));
    return invSphere*(.04+.96*2048.*mass/total);
}
vec3 sampleSky(uint seed){
    float total=u.draws.skyRows[31];
    vec2 jitter=vec2(sample01(seed+2u),sample01(seed+3u));
    if(!(total>0.) || sample01(seed+4u)<.04)return skyCellDirection(jitter);
    uint lo=0u,hi=31u;float r=min(sample01(seed)*total,uintBitsToFloat(floatBitsToUint(total)-1u));
    while(lo<hi){uint m=(lo+hi)/2u;if(r<u.draws.skyRows[m])hi=m;else lo=m+1u;}
    uint row=lo;lo=0u;hi=63u;uint first=row*64u;
    float width=u.draws.skyColumns[first+63u];
    r=min(sample01(seed+1u)*width,uintBitsToFloat(floatBitsToUint(width)-1u));
    while(lo<hi){uint m=(lo+hi)/2u;if(r<u.draws.skyColumns[first+m])hi=m;else lo=m+1u;}
    return skyCellDirection((vec2(lo,row)+jitter)/vec2(64,32));
}
float explicitSkyPdf(vec3 world,float diffusePdf,float specPdf,bool pbr){
    return .5*skyPdf(world)+(pbr?.25:.5)*diffusePdf+(pbr?.25*specPdf:0.);
}
vec3 environmentConnection(vec3 origin,vec3 n,vec3 v,float bias,vec3 diffuseThru,vec3 incoming,vec3 f0,float rough,
                           bool pbr,float diffusePr,bool continues,uint seed,vec2 uv,inout uint landHits){
    vec3 l;
    float choice=sample01(seed);
    if(choice<.5)l=fromWorldDirection(sampleSky(seed+7u));
    else if(pbr && choice>=.75){
        vec3 ignored;l=glossy(n,v,rough,f0,seed+21u,ignored);
    }else l=cosineDirection(n,uv);
    float nl=dot(n,l);
    if(!(nl>0.))return vec3(0);
    vec3 world=normalize(toWorldDirection(l));
    float d=nl/3.14159265359,s=pbr?glossyPdf(n,v,l,rough):0.;
    float q=explicitSkyPdf(world,d,s,pbr);
    if(!(q>0.))return vec3(0);
    vec3 h;uint id;vec2 bc;
    if(traceScene(origin,l,bias,5000.,h,id,bc)){
        if((id&landFlag)!=0u)landHits++;
        if((id&propFlag)!=0u){
            propShadow++;
            if((id&treeBit)!=0u || placedProp(id&~propFlag))placedShadow++;
        }
        return vec3(0);
    }
    vec3 weight=diffuseThru*d/(q+(continues?diffusePr*d:0.));
    if(pbr && s>0.){
        vec3 halfVec=normalize(v+l);
        vec3 fresnel=f0+(1.-f0)*pow(1.-clamp(dot(v,halfVec),0.,1.),5.);
        weight+=incoming*fresnel*smithG1(nl,rough*rough*rough*rough)*s/(q+(continues?(1.-diffusePr)*s:0.));
    }
    return weight*max(textureLod(environmentMap,probeDirection(world),0.).rgb,vec3(0));
}
