static void particle_modulate(DirectCtx *c,u32 components[4],u32 vec4,u32 vec3,u32 fragcoord,u32 sampler,u32 sampledType,u32 witness,u32 storagePointer,u32 witnessBase){
    SpvEmitter *e=&c->emit;
    u32 f=c->f32_type,one=spv_emit_constant_f32(e,f,1.f);
    u32 coord=spv_emit_load(e,vec4,fragcoord);
    u32 x=spv_emit_fmul(e,f,spv_emit_vec4_extract(e,f,coord,0),spv_emit_constant_f32(e,f,1.f/1920.f));
    u32 y=spv_emit_fmul(e,f,spv_emit_vec4_extract(e,f,coord,1),spv_emit_constant_f32(e,f,1.f/1080.f));
    u32 invW=spv_emit_vec4_extract(e,f,coord,3);
    u32 logW=spv_emit_fnegate(e,f,spv_emit_glsl_unary(e,f,c->glsl,SPV_GLSL_LOG2,invW));
    u32 z=spv_emit_fmul(e,f,spv_emit_fadd(e,f,logW,one),spv_emit_constant_f32(e,f,1.f/11.f));
    u32 uvw=spv_emit_vec3_construct(e,vec3,x,y,z);
    u32 shadow=spv_emit_sample2d_lod(e,vec4,sampledType,sampler,uvw,0,spv_emit_constant_f32(e,f,0.f));
    if(witness){
        u32 cutoff=spv_emit_constant_u32(e,c->u32_type,0x3c23d70au); // .01 as positive IEEE float.
        u32 alpha=spv_emit_bitcast(e,c->u32_type,components[3]);
        u32 cover=spv_emit_u_less(e,c->bool_type,cutoff,alpha);
        u32 active=spv_emit_select(e,c->u32_type,cover,spv_emit_constant_u32(e,c->u32_type,1),c->zero);
        u32 attenuation=spv_emit_bitcast(e,c->u32_type,spv_emit_vec4_extract(e,f,shadow,0));
        u32 counts[]={active,spv_emit_select(e,c->u32_type,spv_emit_u_less(e,c->bool_type,cutoff,attenuation),active,c->zero),
            spv_emit_select(e,c->u32_type,spv_emit_u_less(e,c->bool_type,spv_emit_constant_u32(e,c->u32_type,0x3dcccccdu),attenuation),active,c->zero)};
        for(u32 i=0;i<3;i++){
            u32 ptr=spv_emit_storage_access_u32(e,storagePointer,witness,c->zero,spv_emit_constant_u32(e,c->u32_type,witnessBase+i));
            spv_emit_atomic_u32(e,c->u32_type,SPV_OP_ATOMIC_I_ADD,ptr,counts[i]);
        }
    }
    for(u32 i=0;i<3;i++){
        u32 blocked=spv_emit_vec4_extract(e,f,shadow,i);
        components[i]=spv_emit_fmul(e,f,components[i],spv_emit_fadd(e,f,one,spv_emit_fnegate(e,f,blocked)));
    }
}
