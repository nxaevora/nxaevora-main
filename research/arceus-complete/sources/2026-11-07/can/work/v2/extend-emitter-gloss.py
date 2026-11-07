from pathlib import Path
base=Path('work/v2')
p=base/'contact.comp';s=p.read_text()
s=s.replace('vec3 glossy(vec3 n,vec3 v,float rough,vec3 f0,uint seed,out vec3 weight){','''// GGX visible-normal reflection density in solid angle and Smith G1 for the outgoing ray.
float smithG1(float cosine,float a2){
    return 2.*cosine/max(1e-8,cosine+sqrt(a2+(1.-a2)*cosine*cosine));
}
float glossyPdf(vec3 n,vec3 v,vec3 l,float rough){
    float nv=dot(n,v),nl=dot(n,l);
    if(nv<=0. || nl<=0.)return 0.;
    vec3 h=normalize(v+l);
    float nh=clamp(dot(n,h),0.,1.),a2=rough*rough*rough*rough;
    float d=(1.-nh*nh)+nh*nh*a2;
    return a2/(3.14159265*d*d)*smithG1(nv,a2)/(4.*nv);
}
vec3 glossy(vec3 n,vec3 v,float rough,vec3 f0,uint seed,out vec3 weight){''').replace('weight=fresnel*(2.*nl/max(1e-6,nl+sqrt(alpha*alpha+(1.-alpha*alpha)*nl*nl)));','weight=fresnel*smithG1(nl,alpha*alpha);')
s=s.replace('previousDiffusePdf','previousLobePdf').replace('            float alternate=depth+1u==maxSurfaces?0.:diffuseProbability;','''            bool continues=depth+1u<maxSurfaces;
            vec3 specularThroughput=pbr?incoming:vec3(0);''').replace('sampleEmitter(origin,normal,nearT,throughput,alternate,seed','sampleEmitter(origin,normal,-direction,nearT,throughput,specularThroughput,f0,rough,diffuseProbability,continues,seed')
s=s.replace('        previousGloss=false;','        vec3 outgoing=-direction;\n        previousGloss=false;').replace('previousLobePdf=previousGloss?0.:diffuseProbability*max(0.,dot(normal,direction))/3.14159265;','previousLobePdf=previousGloss?(1.-diffuseProbability)*glossyPdf(normal,outgoing,direction,rough):\n            diffuseProbability*max(0.,dot(normal,direction))/3.14159265;')
p.write_text(s)
p=base/'emitter-sampling.glsl';s=p.read_text().replace('vec3 origin,vec3 normal,float bias,vec3 diffuseThroughput,float diffuseProbability,','vec3 origin,vec3 normal,vec3 outgoing,float bias,vec3 diffuseThroughput,vec3 specularThroughput,\n                   vec3 f0,float rough,float diffuseProbability,bool continues,').replace('lessThanEqual(diffuseThroughput,vec3(0))','lessThanEqual(diffuseThroughput+specularThroughput,vec3(0))')
s=s.replace('    float bsdfPdf=diffuseProbability*cosine/3.14159265;\n    return diffuseThroughput*glow*(cosine/3.14159265)/(density+bsdfPdf);','''    float diffusePdf=cosine/3.14159265;
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
    return glow*result;''')
assert 'return glow*result' in s;p.write_text(s)
