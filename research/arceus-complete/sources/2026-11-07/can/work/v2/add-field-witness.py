from pathlib import Path
p=Path('work/v2/contact.comp');s=p.read_text().replace('#if defined(EMITTER_TRIANGLES)','#if defined(FIELD_WITNESS)\n#include "field-witness.glsl"\n#elif defined(EMITTER_TRIANGLES)');p.write_text(s)
p=Path('work/v2/props.inc');s=p.read_text().replace('if((tag&255)>=h.materials || (tag>>16)>=h.groups)ok=false;','''if((tag&255)>=h.materials || (tag>>16)>=h.groups)ok=false;
            else if(classes && ((const float*)(data+check+64*(tag>>16)))[7]!=(float)((tag&255)+1))ok=false;''');p.write_text(s)
p=Path('work/v2/build.py');s=p.read_text().replace("defs=[]","defs=[]\nfield_variants=[('fieldWitness',['-DFIELD_WITNESS'])] if '--field-witness' in sys.argv[2:] else []").replace("for name,flags in [('verifyObjects'","for name,flags in field_variants+[('verifyObjects'").replace("subprocess.run(args+witness_flags+['-I'+str(ups)]","subprocess.run(args+witness_flags+(['-DFIELD_WITNESS'] if field_variants else [])+['-I'+str(ups)]");p.write_text(s)
p=Path('work/v2/terrain.inc');s=p.read_text().replace('    if(request==land.request)return;', '''#ifdef FIELD_WITNESS
    if(g_vk_present>=1030 && g_vk_present<1036){
        static const u32 keys[]={FIELD_ARCHIVES};
        u32 area=keys[(g_vk_present-1030)*2],bytes=keys[(g_vk_present-1030)*2+1];
        request=((u64)bytes<<8)|(area+1);
    }
#endif
    if(request==land.request)return;''')
sizes=[]
for area in [1,2,3,4,6,11]:
 f=Path.home()/f'Documents/Codex/work/arceus-games/ArceusTest/ArceusTest_data/romfs/bin/archive/field/terrain/ha_area{area:02}.gfpak'
 sizes+=[str(area),str(f.stat().st_size)]
p.write_text(s.replace('FIELD_ARCHIVES',','.join(sizes)))
p=Path('work/v2/contact.h');s=p.read_text().replace('    land_update(cb);','''#ifdef FIELD_WITNESS
    bool fieldTest=g_vk_present>=1030 && g_vk_present<=1036;
    bool fieldReport=fieldTest || g_vk_present==1037;
    if(g_vk_present>1030 && fieldReport && contact.mappedCounters){
        contact_wait_readback();
        LOG("field-witness: previous present %u attempted %u confirmed %u active %u\\n",g_vk_present-1,
            contact.mappedCounters[11800],contact.mappedCounters[11801],contact.mappedCounters[11802]);
    }
    if(fieldTest)vkCmdFillBuffer(cb,contact.counters.handle,11800*4,12,0);
#endif
    land_update(cb);''').replace('compute,VK_ACCESS_SHADER_READ_BIT);\n    vkCmdBindDescriptorSets','compute,VK_ACCESS_SHADER_READ_BIT|VK_ACCESS_SHADER_WRITE_BIT);\n    vkCmdBindDescriptorSets')
s=s.replace('    vkCmdBindPipeline(cb,VK_PIPELINE_BIND_POINT_COMPUTE,contact.calibrationPipeline);','''#ifdef FIELD_WITNESS
    if(fieldTest && land.gpu.props.groups && contact.mappedCounters){
        static VkPipeline witness;
        if(!witness)contact_pipeline(fieldWitness_words,sizeof fieldWitness_words,&witness);
        if(witness){
            vkCmdBindPipeline(cb,VK_PIPELINE_BIND_POINT_COMPUTE,witness);
            vkCmdDispatch(cb,land.gpu.props.groups,1,1);
        }
    }
#endif
    vkCmdBindPipeline(cb,VK_PIPELINE_BIND_POINT_COMPUTE,contact.calibrationPipeline);''')
# vkCmdFillBuffer requires TRANSFER_DST on the shared statistics buffer in witness builds.
s=s.replace('contact_buffer(&contact.counters,65536,VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,true)','contact_buffer(&contact.counters,65536,VK_BUFFER_USAGE_STORAGE_BUFFER_BIT|VK_BUFFER_USAGE_TRANSFER_DST_BIT,true)')
p.write_text(s)
