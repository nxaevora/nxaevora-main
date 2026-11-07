from pathlib import Path
base=Path('work/v2')
p=base/'terrain.inc';s=p.read_text().replace('#include "props.inc"','#include "props.inc"\n#include "grove.inc"').replace('grid[4],viewport[4];} LandInfo;','grid[4],viewport[4];GroveInfo grove;} LandInfo;').replace('sizeof(LandInfo)==160','sizeof(LandInfo)==208').replace('2*sizeof(VkAccelerationStructureInstanceKHR),input','(2+groveInstanceLimit)*sizeof(VkAccelerationStructureInstanceKHR),input').replace('VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR,2);','VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR,2+groveInstanceLimit);')
s=s.replace('    VkAccelerationStructureInstanceKHR instances[2]', '''    GroveArea *grove=&groves[id];bool haveGrove=grove_load(grove,id,(u32)(request>>8));
    VkAccelerationStructureInstanceKHR instances[2]''').replace('    land.top.range.primitiveCount=haveProps?2:1;', '''    u32 ordinary=haveProps?2u:1u;
    land.top.range.primitiveCount=ordinary+(haveGrove?grove->info.instanceCount:0u);''')
s=s.replace('    vkCmdUpdateBuffer(cb,land.instance.handle,0,land.top.range.primitiveCount*sizeof *instances,instances);','''    vkCmdUpdateBuffer(cb,land.instance.handle,0,ordinary*sizeof *instances,instances);
    if(haveGrove){
        for(u32 i=0;i<grove->info.instanceCount;){
            u32 n=grove->info.instanceCount-i;
            if(n>1024)n=1024;
            vkCmdUpdateBuffer(cb,land.instance.handle,(ordinary+i)*sizeof *instances,n*sizeof *instances,grove->instances+i);
            i+=n;
        }
        vkCmdFillBuffer(cb,grove->data.handle,grove->activeOffset,4ull*(grove->info.instanceCount+grove->info.shapeCount),0);
    }''').replace('    contact_barrier(cb,build,write,build,read);','''    if(haveGrove && !grove->built){
        for(u32 i=0;i<grove->info.shapeCount;i++)contact_build(cb,&grove->trees[i]);
        grove->built=true;
    }
    contact_barrier(cb,build,write,build,read);''')
s=s.replace('    memcpy(land.gpu.grid,','''    if(haveGrove){
        land.gpu.grove=grove->info;
        LOG("world-pt: loaded %u native tree instances from %u unique meshes in ha_area%02u\\n",grove->info.instanceCount,grove->info.shapeCount,id);
    }
    memcpy(land.gpu.grid,''')
p.write_text(s)
p=base/'build.py';s=p.read_text().replace("('verifyObjects',['-DVERIFY_OBJECTS']),","('verifyObjects',['-DVERIFY_OBJECTS']),('verifyTrees',['-DVERIFY_TREES']),");p.write_text(s)
p=base/'contact.h';s=p.read_text().replace('shadePipeline,verifyPipeline,','shadePipeline,verifyPipeline,treeVerifyPipeline,').replace('return contact.scene && contact_pipeline(trianglePower','return contact.scene && contact_pipeline(verifyTrees_words,sizeof verifyTrees_words,&contact.treeVerifyPipeline) && contact_pipeline(trianglePower').replace('    contact_barrier(cb,compute,VK_ACCESS_SHADER_WRITE_BIT,build,VK_ACCESS_SHADER_READ_BIT);','''    if(land.gpu.grove.instanceCount){
        contact_barrier(cb,compute,VK_ACCESS_SHADER_WRITE_BIT,compute,VK_ACCESS_SHADER_READ_BIT|VK_ACCESS_SHADER_WRITE_BIT);
        vkCmdBindPipeline(cb,VK_PIPELINE_BIND_POINT_COMPUTE,contact.treeVerifyPipeline);
        vkCmdDispatch(cb,drawCount,1,1);
        contact_barrier(cb,compute,VK_ACCESS_SHADER_WRITE_BIT,compute,VK_ACCESS_SHADER_READ_BIT);
    }
    contact_barrier(cb,compute,VK_ACCESS_SHADER_WRITE_BIT,build,VK_ACCESS_SHADER_READ_BIT);''');p.write_text(s)
p=base/'contact.comp';s=p.read_text().replace('struct LandInfo {','#include "grove-structs.glsl"\nstruct LandInfo {').replace('vec4 grid;vec4 viewport;};','vec4 grid;vec4 viewport;GroveInfo grove;};').replace('#if defined(FIELD_WITNESS)','#if defined(VERIFY_TREES)\n#include "grove-witness.glsl"\n#elif defined(FIELD_WITNESS)');p.write_text(s)
