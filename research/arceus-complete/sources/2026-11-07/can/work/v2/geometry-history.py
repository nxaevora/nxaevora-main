from pathlib import Path
v=Path("work/v2")
def sub(file,a,b):
    p=v/file;s=p.read_text()
    assert s.count(a)==1,(file,a);p.write_text(s.replace(a,b))
sub("build.py","reflection_flags=(","geometry_history=['-DGEOMETRY_HISTORY'] if '--geometry-history' in sys.argv else []\nreflection_flags=(")
sub("build.py","'--target-env','vulkan1.2',*flags,","'--target-env','vulkan1.2',*geometry_history,*flags,")
sub("build.py","*(['-DHISTORY_WITNESS']","*geometry_history,*(['-DHISTORY_WITNESS']")
sub("build.py","args+reflection_flags+(['-DHISTORY_WITNESS']","args+reflection_flags+geometry_history+(['-DHISTORY_WITNESS']")
sub("build.py","    target=tmp/'denoise-baseline.spv'","""    baseline=tmp/'denoise-baseline.comp'
    if geometry_history:
        # Run the released moments filter unchanged against the same path samples and histories.
        baseline=tmp/'denoise-prior.comp'
        released=(tmp/'native-scenery/inputs/denoise.comp').read_text()
        baseline.write_text(released.replace('ref.v[2u*480u*270u+i]','ref.v[3u*480u*270u+i]'))
    target=tmp/'denoise-baseline.spv'""")
sub("build.py","target,tmp/'denoise-baseline.comp'","target,baseline")
sub("contact.h","2ull*1920*1080*8","""2ull*1920*1080*
#ifdef GEOMETRY_HISTORY
            12
#else
            8
#endif
            """)
sub("material.inc","u32 roughWrap,metalWrap,packedProperties,reserved;","u32 roughWrap,metalWrap,packedProperties,surfaceKey;")
sub("material.inc","    if(shader && shader->hash==0xea4d", """#ifdef GEOMETRY_HISTORY
    if(shader){
        u64 image=capturedTextures[draw->slot>>16].address;
        draw->surfaceKey=(u32)(shader->hash^(shader->hash>>32)^image^(image>>32));
    }
#endif
    if(shader && shader->hash==0xea4d""")
sub("contact.comp","uint packedProperties;uint reserved;","uint packedProperties;uint surfaceKey;")
sub("contact.comp","    for(uint i=0;i<3;i++){\n        vec4 p=u.clip", """#ifdef GEOMETRY_HISTORY
    uint key=2166136261u^material.surfaceKey;
    bool usable=!missing;
#endif
    for(uint i=0;i<3;i++){
        vec4 p=u.clip""")
sub("contact.comp","        u.vertices.v[dst+i]=vec4(point,1);","""        u.vertices.v[dst+i]=vec4(point,1);
#ifdef GEOMETRY_HISTORY
        vec3 world=u.origin.xyz+u.axisX.xyz*point.x+u.axisY.xyz*point.y+u.axisZ.xyz*point.z;
        if(any(isnan(world)) || any(isinf(world)) || any(greaterThan(abs(world),vec3(1000000.))))usable=false;
        else{
            uvec3 quantized=uvec3(ivec3(round(world*32.)));
            key=(key^quantized.x)*16777619u;
            key=(key^quantized.y)*16777619u;
            key=(key^quantized.z)*16777619u;
        }
        if(i==2u && usable)u.vertices.v[dst].w=uintBitsToFloat(0x3f800000u|(1u+key%0x7fffffu));
#endif""")
sub("contact.comp","    uint surfaceGuide=0u;","    uint surfaceGuide=0u,geometryKey=0u;")
sub("contact.comp","        bool fromBuffers=depth==0u && recovered;", """#ifdef GEOMETRY_HISTORY
        if(depth==0u && depthMatch && !recovered && primitive<u.count)
            geometryKey=floatBitsToUint(u.vertices.v[3u*primitive].w)&0x7fffffu;
#endif
        bool fromBuffers=depth==0u && recovered;""")
sub("contact.comp","        PrimaryLight(u.draws.primaryLight).values[index]","""#ifdef GEOMETRY_HISTORY
        Words(u.draws.primaryLight).v[4u*uint(size.x*size.y)+index]=written?geometryKey:0u;
#endif
        PrimaryLight(u.draws.primaryLight).values[index]""")
sub("denoise.comp","vec3 guideNormal(uint tag)", """#ifdef GEOMETRY_HISTORY
layout(buffer_reference,std430,buffer_reference_align=4) readonly buffer Keys {uint v[];};
uint triangleKey(ivec2 p,ivec2 size,uint layer){
    if(all(equal(frame.primaryLight,uvec2(0))))return 0u;
    return Keys(frame.primaryLight).v[(4u+layer)*uint(size.x*size.y)+uint(p.y*size.x+p.x)];
}
#endif
vec3 guideNormal(uint tag)""")
sub("denoise.comp","                if((data.x&4095u)!=materialID)continue;", """                bool sameMaterial=(data.x&4095u)==materialID;
#ifdef GEOMETRY_HISTORY
                uint key=triangleKey(p,size,frame.layer);
                bool sameTriangle=key!=0u && key==triangleKey(q,size,frame.layer^1u);
                if(!sameMaterial && !sameTriangle)continue;
#else
                if(!sameMaterial)continue;
#endif""")
sub("denoise.comp","                previous+=weight", """#ifdef GEOMETRY_HISTORY
                if(!sameMaterial && (distance(material.rgb,surface)>.035 ||
                   abs(expected-previousZ)>max(.000002,.005*expected) ||
                   dot(guideNormal(uint(oldBase.a)),guideNormal(uint(base.a)))<.96))continue;
#endif
                previous+=weight""")
sub("denoise.comp","distances.w=material.a*255.;","distances.w=float((materialID<<12u)|(data.x&4095u));")
