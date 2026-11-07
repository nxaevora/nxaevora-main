from pathlib import Path
p=Path('work/v2')
s=(Path('work/pt/device.c')).read_text()
s=s.replace('int g_vk_preserve_infnan;', '''extern int arceus_checkpoints;
int arceus_checkpoints;
static int world_fault_enabled;
static void world_fault(void){
    static int reported;
    if(__atomic_exchange_n(&reported,1,__ATOMIC_RELAXED))return;
    if(world_fault_enabled && vkGetDeviceFaultInfoEXT){
        VkDeviceFaultCountsEXT counts={.sType=VK_STRUCTURE_TYPE_DEVICE_FAULT_COUNTS_EXT};
        if(vkGetDeviceFaultInfoEXT(g_vk.dev,&counts,NULL)==VK_SUCCESS){
            VkDeviceFaultAddressInfoEXT addresses[32];
            VkDeviceFaultVendorInfoEXT vendors[8];
            if(counts.addressInfoCount>32)counts.addressInfoCount=32;
            if(counts.vendorInfoCount>8)counts.vendorInfoCount=8;
            counts.vendorBinarySize=0;
            VkDeviceFaultInfoEXT info={.sType=VK_STRUCTURE_TYPE_DEVICE_FAULT_INFO_EXT,.pAddressInfos=addresses,.pVendorInfos=vendors};
            VkResult r=vkGetDeviceFaultInfoEXT(g_vk.dev,&counts,&info);
            if(r==VK_SUCCESS || r==VK_INCOMPLETE){
                WARN("world-pt: device fault %s addresses=%u vendors=%u\\n",info.description,counts.addressInfoCount,counts.vendorInfoCount);
                for(u32 i=0;i<counts.addressInfoCount && i<32;i++)WARN("world-pt: device fault address %llx type=%d\\n",(unsigned long long)addresses[i].reportedAddress,addresses[i].addressType);
                for(u32 i=0;i<counts.vendorInfoCount && i<8;i++)WARN("world-pt: device fault detail %s\\n",vendors[i].description);
            }
        }
    }
    if(arceus_checkpoints && vkGetQueueCheckpointDataNV){
        VkCheckpointDataNV points[16];u32 n=16;
        for(u32 i=0;i<16;i++)points[i]=(VkCheckpointDataNV){.sType=VK_STRUCTURE_TYPE_CHECKPOINT_DATA_NV};
        vkGetQueueCheckpointDataNV(g_vk.gfx,&n,points);
        for(u32 i=0;i<n && i<16;i++)WARN("world-pt: last queue checkpoint stage=%x marker=%llu\\n",points[i].stage,(unsigned long long)(uintptr_t)points[i].pCheckpointMarker);
    }
}
int g_vk_preserve_infnan;''')
s=s.replace('    if (r >= 0) return 1;', '    if (r >= 0) return 1;\n    if(r==VK_ERROR_DEVICE_LOST)world_fault();',1)
mark='    VkDeviceCreateInfo dci ='
s=s.replace(mark,'''    VkPhysicalDeviceFaultFeaturesEXT fault={.sType=VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FAULT_FEATURES_EXT};
    if(getenv("ARCEUS_RT_DIAGNOSTICS")){
        SnxVkExtensions extensions={0};
        if(snx_vk_device_extensions(g_vk.phys,&extensions)){
            if(next<sizeof g_vk.dev_ext/sizeof *ext && snx_vk_extensions_has(&extensions,VK_EXT_DEVICE_FAULT_EXTENSION_NAME)){
                VkPhysicalDeviceFeatures2 features={.sType=VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2,.pNext=&fault};
                vkGetPhysicalDeviceFeatures2(g_vk.phys,&features);
                if(fault.deviceFault){fault.deviceFaultVendorBinary=VK_FALSE;fault.pNext=chain;chain=&fault;world_fault_enabled=1;ext[next++]=VK_EXT_DEVICE_FAULT_EXTENSION_NAME;}
            }
            if(next<sizeof g_vk.dev_ext/sizeof *ext && snx_vk_extensions_has(&extensions,VK_NV_DEVICE_DIAGNOSTIC_CHECKPOINTS_EXTENSION_NAME)){
                ext[next++]=VK_NV_DEVICE_DIAGNOSTIC_CHECKPOINTS_EXTENSION_NAME;arceus_checkpoints=1;
            }
            snx_vk_extensions_free(&extensions);
        }
        LOG("world-pt: driver fault reporting=%d checkpoints=%d\\n",world_fault_enabled,arceus_checkpoints);
    }
'''+mark)
(p/'device.c').write_text(s)
f=p/'build.py';s=f.read_text();s=s.replace('s=(w/\'build-merged.py\')','''e=next(e for e in db if e['file'].endswith('/nx/gpu/vk/device.c'))
args=[v.strip('"') for v in shlex.split(e['command'],posix=False)]
args[args.index('-o')+1]=str(tmp/'device.obj');args[-1]=str(tmp/'device.c')
subprocess.run(args+['-I'+str(ups)],cwd=build,check=True)
s=(w/'build-merged.py')''').replace("w/'pt' if f=='device.obj'","tmp if f=='device.obj'")
f.write_text(s)
f=p/'contact.h';s=f.read_text().replace('enum { rtTriangles','extern int arceus_checkpoints;\nstatic void world_mark(VkCommandBuffer cb,uintptr_t marker){if(arceus_checkpoints)vkCmdSetCheckpointNV(cb,(void*)marker);}\nenum { rtTriangles')
for mark,i in [('    land_update(cb);',1),('    vkCmdBindPipeline(cb,VK_PIPELINE_BIND_POINT_COMPUTE,contact.meshPipeline);',2),('    contact_build(cb,&contact.bottom);',3),('    vkCmdBindPipeline(cb,VK_PIPELINE_BIND_POINT_COMPUTE,contact.calibrationPipeline);',4),('    vkCmdBindPipeline(cb,VK_PIPELINE_BIND_POINT_COMPUTE,contact.shadePipeline);',5),('    vk_render_forget_state();',6)]:
 assert s.count(mark)==1;s=s.replace(mark,'    world_mark(cb,'+str(i)+');\n'+mark)
f.write_text(s)
f=p/'contact.comp';s=f.read_text()
s=s.replace('bool intersection(uvec2','''bool validRay(vec3 origin,vec3 direction,float nearT,float farT){
    float len=dot(direction,direction);
    return all(lessThan(abs(origin),vec3(1e7))) && len>.9 && len<1.1 && nearT>=0. && farT>nearT;
}
bool intersection(uvec2''')
s=s.replace('    rayQueryEXT ray;\n    rayQueryInitializeEXT(ray,accelerationStructureEXT(scene),gl_RayFlagsOpaqueEXT,','    if(!validRay(origin,direction,nearT,farT))return false;\n    rayQueryEXT ray;\n    rayQueryInitializeEXT(ray,accelerationStructureEXT(scene),gl_RayFlagsOpaqueEXT,')
s=s.replace('bool anyHit(uvec2 scene,vec3 origin,vec3 direction,float bias){','bool anyHit(uvec2 scene,vec3 origin,vec3 direction,float bias){\n    if(!validRay(origin,direction,bias,5000.))return false;')
s=s.replace('    draw=uint(a.w);','    draw=uint(a.w);') # Check draw indices before the storage-buffer access
s=s.replace('    Tint t=u.draws.tint[draw];', '    if(draw>=2048u){measured=false;return raw;}\n    Tint t=u.draws.tint[draw];')
s=s.replace('    uvec3 a=uvec3(round(255.*observed))','    if(draw>=2048u)return;\n    uvec3 a=uvec3(round(255.*observed))')
s=s.replace('    primitive=uint(rayQueryGetIntersectionPrimitiveIndexEXT(ray,true));\n    return true;','    primitive=uint(rayQueryGetIntersectionPrimitiveIndexEXT(ray,true));\n    uint maxCount=all(equal(scene,u.scene))?u.count:2u*(u.draws.land.side-1u)*(u.draws.land.side-1u);\n    return primitive<maxCount && distanceHit>=nearT && distanceHit<=farT;')
f.write_text(s)
