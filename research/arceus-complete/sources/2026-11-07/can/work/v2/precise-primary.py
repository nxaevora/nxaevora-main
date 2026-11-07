from pathlib import Path
p=Path('work/v2/contact.comp')
s=p.read_text();i=s.index('// The guest depth buffer records');j=s.index('bool traceScene(',i)
s=s[:i]+'''// Raster depth gives the covered sample's distance even when textured foliage or
// decimated terrain hides it from the first ray. Search live triangles on both
// sides of that distance and choose the closest projected depth.
bool primaryAtDepth(vec3 direction,float expected,float tolerance,out vec3 hit,out uint primitive,out vec2 bary,out bool cutout){
    float nearT=max(.01,(expected-tolerance)/direction.z);
    float farT=min(5000.,(expected+tolerance)/direction.z);
    if(!(farT>nearT))return false;
    rayQueryEXT ray;
    rayQueryInitializeEXT(ray,accelerationStructureEXT(u.scene),gl_RayFlagsNoOpaqueEXT,255,vec3(0),nearT,direction,farT);
    float best=tolerance;
    bool found=false;
    while(rayQueryProceedEXT(ray)){
        if(rayQueryGetIntersectionTypeEXT(ray,false)!=gl_RayQueryCandidateIntersectionTriangleEXT)continue;
        float t=rayQueryGetIntersectionTEXT(ray,false);
        float error=abs(direction.z*t-expected);
        uint geometry=rayQueryGetIntersectionGeometryIndexEXT(ray,false);
        uint id=rayQueryGetIntersectionPrimitiveIndexEXT(ray,false)+(geometry==1u?uint(u.axisZ.w):0u);
        if(id>=u.count || !(error<best))continue;
        best=error;hit=direction*t;primitive=id;bary=rayQueryGetIntersectionBarycentricsEXT(ray,false);
        cutout=geometry==1u;found=true;
    }
    return found;
}
'''+s[j:]
s=s.replace('matchedInterval=false,matchedCutout=false','matchedInterval=false,matchedCutout=false,matchedPrecise=false')
mark='            // A coarse retained mesh'
s=s.replace(mark,'''            // Account for D24 quantization in reversed-Z and view-space float precision.
            float fineTolerance=.0005+expected*.00012+expected*expected*abs(u.camera.z)/16777215.;
            if(z>0. && expected>.001 && expected<5000. && (!found || abs(hit.z-expected)>fineTolerance)){
                vec3 candidate;uint id;vec2 bc;bool cut;
                if(primaryAtDepth(direction,expected,fineTolerance,candidate,id,bc,cut)){
                    hit=candidate;primitive=id;bary=bc;found=true;primaryHit=true;depthMatch=true;
                    matchedCutout=cut;matchedPrecise=!cut;
                }
            }
'''+mark)
i=s.index('            if(!depthMatch && z>0. && expected>.001 && expected<5000.)');j=s.index('            if(!depthMatch && hasInput',i);s=s[:i]+s[j:]
s=s.replace('if(hasInput(8u))primaryAudit.w=recovered?3u:matchedCutout?4u:matchedInterval?2u:1u;', '''if(hasInput(8u)){
                primaryAudit.w=recovered?3u:matchedCutout?4u:matchedPrecise?5u:matchedInterval?2u:1u;
                if(!recovered){primaryAudit.x=primitive;primaryAudit.y=floatBitsToUint(hit.z);}
            }''')
s=s.replace('        if(matchedCutout)atomicAdd','        if(matchedPrecise)atomicAdd(diagnostics.v[16375],1u);\n        if(matchedCutout)atomicAdd')
p.write_text(s)
p=Path('work/v2/contact.h');s=p.read_text().replace('placedShadow=%u placedBounce=%u','placedShadow=%u placedBounce=%u depthConfirmedOpaque=%u').replace('contact.mappedCounters[16376],contact.mappedCounters[16377]);','contact.mappedCounters[16376],contact.mappedCounters[16377],contact.mappedCounters[16375]);');p.write_text(s)
