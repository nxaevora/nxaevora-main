from pathlib import Path
p=Path('work/v2')
def edit(file,old,new,num=1):
 f=p/file;s=f.read_text();assert s.count(old)==num,(file,old,s.count(old));f.write_text(s.replace(old,new))
edit('build.py',"('contact',[])", "('calibrate',['-DCALIBRATE']),('contact',[])")
edit('stream.inc','worldDrawLimit*sizeof(WorldDrawMaterial)','worldDrawLimit*(sizeof(WorldDrawMaterial)+32)')
edit('contact.h','VkPipeline meshPipeline, shadePipeline;','VkPipeline meshPipeline, shadePipeline,calibrationPipeline;')
edit('contact.h','return contact.scene && contact_pipeline','return contact.scene && contact_pipeline(calibrate_words,sizeof calibrate_words,&contact.calibrationPipeline) && contact_pipeline')
edit('contact.h','vkCmdUpdateBuffer(cb,drawTable.handle','vkCmdFillBuffer(cb,drawTable.handle,worldDrawLimit*sizeof(WorldDrawMaterial),worldDrawLimit*32,0);\n    vkCmdUpdateBuffer(cb,drawTable.handle')
edit('contact.h','    vkCmdBindPipeline(cb,VK_PIPELINE_BIND_POINT_COMPUTE,contact.shadePipeline);','''    vkCmdBindPipeline(cb,VK_PIPELINE_BIND_POINT_COMPUTE,contact.calibrationPipeline);
    vkCmdDispatch(cb,30,17,1);
    contact_barrier(cb,compute,VK_ACCESS_SHADER_WRITE_BIT,compute,VK_ACCESS_SHADER_READ_BIT);
    vkCmdBindPipeline(cb,VK_PIPELINE_BIND_POINT_COMPUTE,contact.shadePipeline);''')
edit('contact.h','contact.counters,32','contact.counters,64')
edit('contact.h','contact.counters.memory,0,32','contact.counters.memory,0,64')
edit('contact.h','mappedCounters,0,32','mappedCounters,0,64')
edit('contact.h','textureSlots=%u\\n','textureSlots=%u drawColor=%u eval=%u grayError=%u rawError=%u fittedError=%u\\n')
edit('contact.h','contact.mappedCounters[5],textureCount);','contact.mappedCounters[5],textureCount,contact.mappedCounters[6],contact.mappedCounters[7],contact.mappedCounters[8],contact.mappedCounters[9],contact.mappedCounters[10]);')
edit('contact.comp','uint unresolved;','uint unresolved;uint drawColor;uint eval;uint grayError;uint rawError;uint fittedError;')
edit('contact.comp','layout(buffer_reference,std430,buffer_reference_align=16) readonly buffer DrawTable {DrawMaterial draw[];};','struct Tint {uvec4 target;uvec4 source;};\nlayout(buffer_reference,std430,buffer_reference_align=16) buffer DrawTable {DrawMaterial draw[2048];Tint tint[];};')
edit('contact.comp','float(material.slot),0.','float(material.slot),float(lo)')
f=p/'contact.comp';s=f.read_text();a=s.index('vec3 materialAt(');b=s.index('void main(){',a)
s=s[:a]+'''bool observedMaterial(vec3 hit,ivec2 size,out vec3 color){
    if(hit.z<=0.)return false;
    vec2 uv=.5+.5*hit.xy/(u.camera.xy*hit.z);
    if(any(lessThan(uv,vec2(0))) || any(greaterThanEqual(uv,vec2(1))))return false;
    ivec2 p=ivec2(uv*vec2(size));
    float depth=texelFetch(depthMap,p,0).r;
    float expected=1./max(1e-7,depth*u.camera.z+u.camera.w);
    if(depth<=0. || abs(expected-hit.z)>=max(.01,.025*hit.z))return false;
    color=clamp(texelFetch(albedoMap,p,0).rgb,vec3(0),vec3(.95));
    return true;
}
bool sourceMaterial(uint primitive,vec2 bary,out uint draw,out vec3 value){
    uint first=1500000u+3u*primitive;
    vec4 a=u.vertices.v[first];uint slot=uint(a.z);draw=uint(a.w);
    if(slot==0u || slot>=128u)return false;
    vec2 uv=a.xy*(1.-bary.x-bary.y)+u.vertices.v[first+1].xy*bary.x+u.vertices.v[first+2].xy*bary.y;
    if(any(isnan(uv)) || any(isinf(uv)))return false;
    value=clamp(textureLod(drawTextures[nonuniformEXT(slot)],uv,1.).rgb,vec3(0),vec3(.95));
    return true;
}
vec3 predictedMaterial(uint draw,bool textured,vec3 raw,out bool measured){
    Tint t=u.draws.tint[draw];
    measured=t.target.w>=3u;
    if(!measured)return raw;
    vec3 mean=vec3(t.target.rgb)/(255.*float(t.target.w));
    if(!textured)return mean;
    vec3 sourceMean=vec3(t.source.rgb)/(255.*float(t.target.w));
    return clamp(mean+(raw-sourceMean)*clamp((mean+.02)/(sourceMean+.02),vec3(.2),vec3(3.)),vec3(0),vec3(.95));
}
vec3 materialAt(vec3 hit,ivec2 size,uint primitive,vec2 bary,inout uint textured,inout uint unresolved,inout uint drawColor){
    vec3 color;
    if(observedMaterial(hit,size,color))return color;
    vec3 raw=vec3(.36);uint draw;
    bool hasTexture=sourceMaterial(primitive,bary,draw,raw);
    if(!hasTexture)raw=vec3(.36);
    bool measured;
    vec3 fitted=predictedMaterial(draw,hasTexture,raw,measured);
    if(hasTexture)textured++;
    else if(measured)drawColor++;
    else unresolved++;
    return fitted;
}
#ifdef CALIBRATE
void main(){
    ivec2 pixel=ivec2(gl_GlobalInvocationID.xy)*8+ivec2(2,3),size=textureSize(albedoMap,0);
    if(any(greaterThanEqual(pixel,size)))return;
    vec3 direction=normalize(vec3(((vec2(pixel)+.5)/vec2(size)*2.-1.)*u.camera.xy,1));
    vec3 hit,observed;uint primitive;vec2 bary;
    if(!traceScene(vec3(0),direction,.01,5000.,hit,primitive,bary) || !observedMaterial(hit,size,observed))return;
    vec3 raw=vec3(0);uint draw;
    if(!sourceMaterial(primitive,bary,draw,raw))raw=vec3(0);
    uvec3 a=uvec3(round(255.*observed)),b=uvec3(round(255.*raw));
    atomicAdd(u.draws.tint[draw].target.x,a.x);atomicAdd(u.draws.tint[draw].target.y,a.y);atomicAdd(u.draws.tint[draw].target.z,a.z);
    atomicAdd(u.draws.tint[draw].source.x,b.x);atomicAdd(u.draws.tint[draw].source.y,b.y);atomicAdd(u.draws.tint[draw].source.z,b.z);
    atomicAdd(u.draws.tint[draw].target.w,1);
}
#else
'''+s[b:]+ '\n#endif\n'
s=s.replace('textured=0,unresolved=0;','textured=0,unresolved=0,drawColor=0;').replace('bary,textured,unresolved)','bary,textured,unresolved,drawColor)')
s=s.replace('        primaryHit=true;', '''        if(depth==0u && depthMatch && any(notEqual(u.stats,uvec2(0))) && all(equal(pixel%32,ivec2(11,13)))){
            vec3 raw,observed;uint draw;
            bool hasTexture=sourceMaterial(primitive,bary,draw,raw);
            if(!hasTexture)raw=vec3(.36);
            bool measured;vec3 predicted=predictedMaterial(draw,hasTexture,raw,measured);
            if(measured && observedMaterial(hit,size,observed)){
                Counts c=Counts(u.stats);
                atomicAdd(c.eval,1);
                atomicAdd(c.grayError,uint(round(dot(abs(observed-.36),vec3(85.)))));
                atomicAdd(c.rawError,uint(round(dot(abs(observed-raw),vec3(85.)))));
                atomicAdd(c.fittedError,uint(round(dot(abs(observed-predicted),vec3(85.)))));
            }
        }
        primaryHit=true;''')
s=s.replace('atomicAdd(c.unresolved,unresolved);','atomicAdd(c.unresolved,unresolved);atomicAdd(c.drawColor,drawColor);')
f.write_text(s)
