from pathlib import Path
base=Path('work/v2')
p=base/'emitter-distribution.glsl';s=p.read_text();i=s.index('            vec3 mean=');k=s.index('\n        }',i);p.write_text(s[:i]+'            sum+=u.draws.emitterCdf[draw];'+s[k:])
p=base/'stream.inc';p.write_text(p.read_text().replace('+sizeof(VkDeviceAddress),VK_BUFFER_USAGE_STORAGE_BUFFER_BIT','+sizeof(VkDeviceAddress)+worldMaxVertices/3*sizeof(float),VK_BUFFER_USAGE_STORAGE_BUFFER_BIT'))
p=base/'contact.comp';s=p.read_text().replace('Words particleGrid;};','Words particleGrid;float triangleCdf[];};').replace('#if defined(EMITTER_DISTRIBUTION)','#if defined(EMITTER_TRIANGLES)\n#include "emitter-triangles.glsl"\n#elif defined(EMITTER_DISTRIBUTION)').replace('emitterProbability(draw,count,emitterCount)','emitterProbability(draw,primitive,count,emitterCount)');p.write_text(s)
p=base/'contact.h';s=p.read_text().replace('calibrationPipeline,emitterPipeline','calibrationPipeline,trianglePowerPipeline,emitterPipeline').replace('return contact.scene && contact_pipeline(emitterCdf','return contact.scene && contact_pipeline(trianglePower_words,sizeof trianglePower_words,&contact.trianglePowerPipeline) && contact_pipeline(emitterCdf').replace('    vkCmdBindPipeline(cb,VK_PIPELINE_BIND_POINT_COMPUTE,contact.emitterPipeline);','''    vkCmdBindPipeline(cb,VK_PIPELINE_BIND_POINT_COMPUTE,contact.trianglePowerPipeline);
    vkCmdDispatch(cb,drawCount,1,1);
    contact_barrier(cb,compute,VK_ACCESS_SHADER_WRITE_BIT,compute,VK_ACCESS_SHADER_READ_BIT|VK_ACCESS_SHADER_WRITE_BIT);
    vkCmdBindPipeline(cb,VK_PIPELINE_BIND_POINT_COMPUTE,contact.emitterPipeline);''');p.write_text(s)
p=base/'build.py';p.write_text(p.read_text().replace("('emitterCdf',['-DEMITTER_DISTRIBUTION']),","('trianglePower',['-DEMITTER_TRIANGLES']),('emitterCdf',['-DEMITTER_DISTRIBUTION']),"))
p=base/'emitter-sampling.glsl';s=p.read_text().replace('emitterProbability(uint draw,uint count','emitterProbability(uint draw,uint primitive,uint count').replace('    return .05*uniformPdf+.95*drawPdf/float(count);','''    uint base=u.draws.draw[draw].base;
    float within=u.draws.triangleCdf[base+count-1u];
    float prior=primitive==base?0.:u.draws.triangleCdf[primitive-1u];
    float triPdf=within>0.?max(0.,u.draws.triangleCdf[primitive]-prior)/within:0.;
    return .05*uniformPdf+.95*drawPdf*triPdf;''')
s=s.replace('    uint offset=weighted?min(uint(sample01(seed+4u)*float(count)),count-1u):pick-first;\n    uint tri=material.base+offset;','''    uint tri;
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
    }else tri=material.base+pick-first;''').replace('emitterProbability(low,count,total)','emitterProbability(low,tri,count,total)')
p.write_text(s)
