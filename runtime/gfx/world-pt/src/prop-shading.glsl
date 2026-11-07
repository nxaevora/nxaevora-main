vec3 unpackPropDirection(uint word){
    return vec3(bitfieldExtract(int(word),0,10),bitfieldExtract(int(word),10,10),bitfieldExtract(int(word),20,10))/511.;
}
uint angularError(vec3 a,vec3 b){
    return uint(round(10.*degrees(acos(clamp(abs(dot(a,b)),0.,1.)))));
}
bool applyNativeFrame(vec3 normal,vec3 tangent,float handedness,vec2 uv,Words maps,uint start,bool mapped,
                      out vec3 smoothN,out vec3 detailN,out vec3 reverseN){
    if(dot(normal,normal)<1.e-6)return false;
    normal=normalize(normal);smoothN=normalize(fromWorldDirection(normal));detailN=smoothN;reverseN=smoothN;
    tangent-=normal*dot(tangent,normal);
    if(!mapped || dot(tangent,tangent)<1.e-6 || abs(handedness)<.5)return true;
    tangent=normalize(tangent);
    vec3 bitangent=sign(handedness)*cross(normal,tangent);
    vec2 p=fract(uv)*128.-.5,f=fract(p),xy=vec2(0);
    for(int y=0;y<2;y++)for(int x=0;x<2;x++){
        ivec2 k=(ivec2(floor(p))+ivec2(x,y))&127;
        uint i=uint(k.y*128+k.x);uint s=maps.v[start+1u+(i>>1u)]>>(16u*(i&1u));
        vec2 sampleXY=vec2(uvec2(s,s>>8u)&255u)/127.5-1.;
        xy+=sampleXY*(x==0?1.-f.x:f.x)*(y==0?1.-f.y:f.y);
    }
    float z=sqrt(max(0.,1.-dot(xy,xy)));
    xy*=uintBitsToFloat(maps.v[start]);
    detailN=normalize(fromWorldDirection(normal*z+tangent*xy.x+bitangent*xy.y));
    reverseN=normalize(fromWorldDirection(normal*z+tangent*xy.x-bitangent*xy.y));
    return true;
}

bool propShading(uint primitive,vec2 bary,out vec3 smoothN,out vec3 detailN,out vec3 reverseN){
    PropInfo props=u.draws.land.props;
    if(all(equal(props.frames,uvec2(0))))return false;
    uint id;
    if((primitive&(treeBit|propFlag))==propFlag)id=primitive&~propFlag;
    else if(primitive<u.count){
        uint draw=uint(u.vertices.v[1500000u+3u*primitive].w);
        if(!matchingProp(primitive,draw,id))return false;
    }else return false;
    if(id>=props.triangles)return false;
    vec3 w=vec3(1.-bary.x-bary.y,bary);
    vec3 normal=vec3(0),tangent=vec3(0);vec2 uv=vec2(0);float handedness=0.;
    Words data=Words(props.frames);
    for(uint j=0u;j<3u;j++){
        uint k=id*3u+j,n=data.v[2u*k],t=data.v[2u*k+1u];
        if((n>>30u)==0u)return false;
        normal+=w[j]*unpackPropDirection(n);
        tangent+=w[j]*unpackPropDirection(t);
        handedness+=w[j]*((t>>30u)==2u?-1.:(t>>30u)==1u?1.:0.);
        uv+=w[j]*unpackHalf2x16(floatBitsToUint(props.mesh.v[k].w));
    }
    uint tag=props.tags.v[id],bank=uint(props.checks.items[tag>>16].position[2].w);
    return applyNativeFrame(normal,tangent,handedness,uv,Words(props.normalMaps),(max(bank,1u)-1u)*8193u,bank!=0u,smoothN,detailN,reverseN);
}
