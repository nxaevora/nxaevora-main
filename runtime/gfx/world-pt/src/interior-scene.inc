static struct {ContactTree top;ContactBuffer instance;bool failed;} interior;
static void interior_update(VkCommandBuffer cb,u32 archive){
    PropArea *props=&propAreas[12];
    if(interior.failed || !props_load(props,12,archive))return;
    const VkPipelineStageFlags build=VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR;
    const VkAccessFlags read=VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR,write=VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_KHR;
    if(!interior.top.handle){
        if(!contact_buffer(&interior.instance,sizeof(VkAccelerationStructureInstanceKHR),
           VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR|VK_BUFFER_USAGE_TRANSFER_DST_BIT,false)){
            interior.failed=true;return;
        }
        interior.top.geometry=(VkAccelerationStructureGeometryKHR){.sType=VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR,
            .geometryType=VK_GEOMETRY_TYPE_INSTANCES_KHR,.geometry.instances={.sType=VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_INSTANCES_DATA_KHR,
            .data.deviceAddress=interior.instance.address}};
        if(!contact_tree(&interior.top,VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR,1)){interior.failed=true;return;}
        const VkAccelerationStructureInstanceKHR instance={.transform.matrix={{1,0,0,0},{0,1,0,0},{0,0,1,0}},
            .instanceCustomIndex=1,.mask=255,.flags=VK_GEOMETRY_INSTANCE_TRIANGLE_FACING_CULL_DISABLE_BIT_KHR,
            .accelerationStructureReference=contact_address(&props->bottom)};
        vkCmdUpdateBuffer(cb,interior.instance.handle,0,sizeof instance,&instance);
        contact_barrier(cb,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_ACCESS_TRANSFER_WRITE_BIT,build,read);
        contact_build(cb,&props->bottom);
        contact_barrier(cb,build,write,build,read);
        contact_build(cb,&interior.top);
        contact_barrier(cb,build,write,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,read);
        props->built=true;
    }
    contact_barrier(cb,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_ACCESS_SHADER_READ_BIT|VK_ACCESS_SHADER_WRITE_BIT,
                    VK_PIPELINE_STAGE_TRANSFER_BIT,VK_ACCESS_TRANSFER_WRITE_BIT);
    vkCmdFillBuffer(cb,props->data.handle,props->activeOffset,4ull*(props->info.groups+props->info.classCount),0);
    contact_barrier(cb,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_ACCESS_TRANSFER_WRITE_BIT,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                    VK_ACCESS_SHADER_READ_BIT|VK_ACCESS_SHADER_WRITE_BIT);
    land.gpu=(LandInfo){.scene=contact_address(&interior.top),.enabled=1,.props=props->info};
    LOG("world-pt: room ha_area00_s01 loaded %u retained triangles in %u verified groups\n",props->info.triangles,props->info.groups);
}
