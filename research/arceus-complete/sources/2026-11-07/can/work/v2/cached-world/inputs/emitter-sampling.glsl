float emitterProbability(uint draw,uint primitive,uint count,uint total){
    if(count==0u || total==0u)return 0.;
    float sum=u.draws.emitterCdf[uint(u.axisX.w)-1u];
    float uniformPdf=1./float(total);
    if(!(sum>0.))return uniformPdf;
    float first=draw==0u?0.:u.draws.emitterCdf[draw-1u];
    float drawPdf=max(0.,u.draws.emitterCdf[draw]-first)/sum;
    uint base=u.draws.draw[draw].base;
    float within=u.draws.triangleCdf[base+count-1u];
    float prior=primitive==base?0.:u.draws.triangleCdf[primitive-1u];
    float triPdf=within>0.?max(0.,u.draws.triangleCdf[primitive]-prior)/within:0.;
    return .05*uniformPdf+.95*drawPdf*triPdf;
}

float emitterPdf(uint primitive,vec3 direction,float distanceSquared,float triangleProbability){
    if(triangleProbability<=0. || primitive>=u.count)return 0.;
    uint vertex=3u*primitive;
    vec3 a=u.vertices.v[vertex].xyz;
    vec3 twiceArea=cross(u.vertices.v[vertex+1u].xyz-a,u.vertices.v[vertex+2u].xyz-a);
    float projected=abs(dot(twiceArea,direction));
    if(!(projected>1e-10))return 0.;
    return 2.*distanceSquared*triangleProbability/projected;
}

vec3 sampleEmitter(vec3 origin,vec3 normal,vec3 outgoing,float bias,vec3 diffuseThroughput,vec3 specularThroughput,
                   vec3 f0,float rough,float diffuseProbability,bool continues,
                   uint seed,ivec2 size,uint total,out bool illuminated){
    illuminated=false;
    if(total==0u || all(lessThanEqual(diffuseThroughput+specularThroughput,vec3(0))))return vec3(0);
    float sum=u.draws.emitterCdf[uint(u.axisX.w)-1u];
    bool weighted=sum>0. && sample01(seed+3u)>=.05;
    float target=weighted?min(sample01(seed)*sum,uintBitsToFloat(floatBitsToUint(sum)-1u)):0.;
    uint pick=min(uint(sample01(seed)*float(total)),total-1u);
    uint low=0u,high=uint(u.axisX.w)-1u;
    while(low<high){
        uint mid=(low+high)/2u;
        if(weighted?target<u.draws.emitterCdf[mid]:pick<u.draws.emitterEnd[mid])high=mid;
        else low=mid+1u;
    }
    DrawMaterial material=u.draws.draw[low];
    uint first=low==0u?0u:u.draws.emitterEnd[low-1u];
    uint count=u.draws.emitterEnd[low]-first;
    if(count==0u)return vec3(0);
    uint tri;
    if(weighted){
        uint a=material.base,b=a+count-1u;
        float within=u.draws.triangleCdf[b];
        if(!(within>0.))return vec3(0);
        float q=min(sample01(seed+4u)*within,uintBitsToFloat(floatBitsToUint(within)-1u));
        while(a<b){
            uint mid=(a+b)/2u;
            if(q<u.draws.triangleCdf[mid])b=mid;else a=mid+1u;
        }
        tri=a;
    }else tri=material.base+pick-first;
    if(tri>=u.count)return vec3(0);
    float r=sqrt(sample01(seed+1u)),s=sample01(seed+2u);
    vec3 weight=vec3(1.-r,r*(1.-s),r*s);
    if(((material.slot>>8u)&255u)!=0u && !acceptsTriangle(tri,weight.yz))return vec3(0);
    uint vertex=tri*3u;
    vec3 point=u.vertices.v[vertex].xyz*weight.x+u.vertices.v[vertex+1u].xyz*weight.y+u.vertices.v[vertex+2u].xyz*weight.z;
    vec3 delta=point-origin;
    float dist=length(delta);
    if(!(dist>bias*3. && dist<5000.))return vec3(0);
    vec3 wi=delta/dist;
    float cosine=dot(normal,wi);
    if(!(cosine>0.))return vec3(0);
    float density=emitterPdf(tri,wi,dist*dist,emitterProbability(low,tri,count,total));
    if(!(density>0.))return vec3(0);
    vec3 observed,glow;
    ivec2 observedPixel;
    if(!observedMaterial(point,size,faceNormal(tri),observed,glow,observedPixel)){
        Tint tint=u.draws.tint[low];
        if(material.emission==0u || material.emission>=textureCapacity){
            if(tint.emission.w<3u || tint.source.w*5u<tint.emission.w*4u)return vec3(0);
            glow=vec3(tint.emission.rgb)/(1024.*float(tint.emission.w));
        }else{
            uint base=1500000u+vertex;
            vec2 uv=u.vertices.v[base].xy*weight.x+u.vertices.v[base+1u].xy*weight.y+u.vertices.v[base+2u].xy*weight.z;
            if(any(isnan(uv)) || any(isinf(uv)))return vec3(0);
            glow=material.emissionScale*textureLod(drawTextures[nonuniformEXT(material.emission)],uv,1.).rgb;
        }
    }
    if(!any(greaterThan(glow,vec3(0))))return vec3(0);
    vec3 blocker;uint id;vec2 bary;
    if(traceScene(origin,wi,bias,dist-max(bias,dist*1e-5),blocker,id,bary))return vec3(0);
    illuminated=true;
    float diffusePdf=cosine/3.14159265;
    vec3 result=diffuseThroughput*diffusePdf/(density+(continues?diffuseProbability*diffusePdf:0.));
    if(any(greaterThan(specularThroughput,vec3(0)))){
        float specPdf=glossyPdf(normal,outgoing,wi,rough);
        if(specPdf>0.){
            vec3 h=normalize(outgoing+wi);
            vec3 fresnel=f0+(1.-f0)*pow(1.-clamp(dot(outgoing,h),0.,1.),5.);
            result+=specularThroughput*fresnel*smithG1(cosine,rough*rough*rough*rough)*
                specPdf/(density+(continues?(1.-diffuseProbability)*specPdf:0.));
        }
    }
    return glow*result;
}

