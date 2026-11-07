uint hashBits(uint x){
    x^=x>>16; x*=0x7feb352du; x^=x>>15; x*=0x846ca68bu; return x^(x>>16);
}
float unitRandom(uint seed){return float(hashBits(seed)>>8)*(1./16777216.);}
vec3 hemisphere(vec3 normal,float radial,float azimuth){
    vec3 tangent=normalize(cross(abs(normal.z)<.85?vec3(0,0,1):vec3(0,1,0),normal));
    float angle=6.2831853*azimuth;
    return normal*sqrt(1.-radial)+sqrt(radial)*(cos(angle)*tangent+sin(angle)*cross(normal,tangent));
}
// The raster lighting buffer supplies incoming radiance and an approximate diffuse reflectance.
vec3 reflectance(vec3 lit){return clamp(sqrt(max(lit,vec3(0))),vec3(.035),vec3(.82));}
vec3 indirectLight(ivec2 pixel,vec3 position,vec3 normal,vec3 base,out uint secondHits,out uint litSecondHits){
    vec3 result=vec3(0);
    secondHits=0;litSecondHits=0;
    uint seed=hashBits(uint(pixel.x)+1920u*uint(pixel.y));
    for(uint path=0;path<1;path++){
        vec3 origin=position, surfaceNormal=normal, throughput=reflectance(base);
        for(uint bounce=0;bounce<2;bounce++){
            uint stream=seed+0x9e3779b9u*bounce;
            float radial=fract(unitRandom(stream)+.5*float(path)+.75487766*float(rt.frame%64u));
            float azimuth=fract(unitRandom(stream+17u)+.5*float(path)+.56984029*float(rt.frame%64u));
            vec3 direction=hemisphere(surfaceNormal,radial,azimuth);
            float bias=max(.0001,origin.z*.0015);
            vec3 start=origin+bias*surfaceNormal;
            rayQueryEXT query;
            rayQueryInitializeEXT(query,accelerationStructureEXT(rt.scene),gl_RayFlagsOpaqueEXT,255,
                                  start,bias,direction,max(bias*32.,origin.z*.75));
            while(rayQueryProceedEXT(query)){}
            if(rayQueryGetIntersectionTypeEXT(query,true)!=gl_RayQueryCommittedIntersectionTriangleEXT)break;
            origin=start+direction*rayQueryGetIntersectionTEXT(query,true);
            vec2 uv=.5+.5*origin.xy/(origin.z*rt.camera.xy);
            if(any(lessThan(uv,vec2(0)))||any(greaterThanEqual(uv,vec2(1))))break;
            ivec2 size=textureSize(sceneColor,0), hitPixel=ivec2(uv*vec2(size));
            if(abs(positionAt(hitPixel).z-origin.z)>max(bias*2.,origin.z*.025))break;
            vec3 radiance=clamp(texelFetch(sceneColor,hitPixel,0).rgb,vec3(0),vec3(4));
            vec3 contribution=throughput*radiance;
            result+=contribution;
            if(bounce==1u){
                secondHits++;
                if(max(contribution.r,max(contribution.g,contribution.b))>.0001)litSecondHits++;
                break;
            }
            vec3 surfaceCenter=positionAt(hitPixel);
            vec3 right=positionAt(hitPixel+ivec2(1,0))-surfaceCenter;
            vec3 down=positionAt(hitPixel+ivec2(0,1))-surfaceCenter;
            vec3 hitNormal=cross(right,down);
            float lengthSquared=dot(hitNormal,hitNormal);
            if(!(lengthSquared>1e-16) || isnan(lengthSquared) || isinf(lengthSquared))break;
            surfaceNormal=hitNormal*inversesqrt(lengthSquared);
            if(dot(surfaceNormal,direction)>0.)surfaceNormal=-surfaceNormal;
            throughput*=reflectance(radiance);
        }
    }
    return result;
}

