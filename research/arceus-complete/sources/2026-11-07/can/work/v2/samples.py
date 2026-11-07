from pathlib import Path
p=Path('work/v2')
f=p/'contact.h';s=f.read_text()
s=s.replace('contact.counters,64,','contact.counters,65536,').replace('contact.counters.memory,0,64,','contact.counters.memory,0,65536,').replace('mappedCounters,0,64','mappedCounters,0,65536')
s=s.replace('    if(contact.mappedCounters && contact.diagnosticFrame', '''    if(contact.mappedCounters && getenv("ARCEUS_GEOMETRY_SAMPLES") && g_vk_present%120==0 && g_vk_present!=contact.diagnosticFrame){
        if(vkDeviceWaitIdle(g_vk.dev)==VK_SUCCESS){
            char filename[1024];
            snprintf(filename,sizeof filename,"%s-%05u.bin",getenv("ARCEUS_GEOMETRY_SAMPLES"),g_vk_present);
            FILE *samples=fopen(filename,"wb");
            if(samples){
                // Matrix is emitted by the same shader invocation group as the depth samples below.
                fwrite(contact.mappedCounters+16,1,65536-64,samples);
                fclose(samples);
            }
        }
    }
    pc.axes[1][3]=(getenv("ARCEUS_GEOMETRY_SAMPLES") && contact.mappedCounters)?1.f:0.f;
    pc.camera[2]=camera[11];pc.camera[3]=camera[15];
    if(contact.mappedCounters && contact.diagnosticFrame''')
# Provide exact camera origin through a trailing 16-byte push block for diagnostics and world rays.
s=s.replace('float axes[3][4];} pc','float axes[3][4],origin[4];} pc').replace('    contact.bottom.range.primitiveCount','    for(int i=0;i<3;i++)pc.origin[i]=camera[8+i]/camera[11];\n    contact.bottom.range.primitiveCount')
f.write_text(s)
f=p/'render.c';s=f.read_text().replace('VK_SHADER_STAGE_COMPUTE_BIT,0,112','VK_SHADER_STAGE_COMPUTE_BIT,0,128');f.write_text(s)
f=p/'contact.comp';s=f.read_text().replace('vec4 axisX,axisY,axisZ;','vec4 axisX,axisY,axisZ,origin;')
s=s.replace('layout(push_constant) uniform', 'layout(buffer_reference,std430,buffer_reference_align=16) buffer GeometrySamples {uvec4 header[4]; vec4 camera; vec4 points[];};\nlayout(push_constant) uniform')
a='    bool depthMatch=false;'
s=s.replace(a,'''    if(u.axisY.w>0. && any(notEqual(u.stats,uvec2(0))) && all(equal(pixel%32,ivec2(0)))){
        GeometrySamples samples=GeometrySamples(u.stats);
        if(all(equal(pixel,ivec2(0))))samples.camera=vec4(u.origin.xyz,float(u.frame));
        float depth=texelFetch(depthMap,pixel,0).r;
        float viewZ=1./max(1e-7,depth*u.camera.z+u.camera.w);
        vec3 v=direction*(viewZ/direction.z);
        uint idx=uint(pixel.x/32+60*(pixel.y/32));
        samples.points[idx]=vec4(u.origin.xyz+u.axisX.xyz*v.x+u.axisY.xyz*v.y+u.axisZ.xyz*v.z,depth);
    }
'''+a)
f.write_text(s)
