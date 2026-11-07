bool instanceShading(uint primitive,vec2 bary,out vec3 smoothN,out vec3 detailN,out vec3 reverseN){
    GroveInfo g=u.draws.land.grove;uint inst,tri;
    if((primitive&(treeBit|propFlag))==(treeBit|propFlag)){
        inst=treeInstance(primitive&~propFlag);tri=treePrimitive(primitive);
    }else if(primitive<u.count){
        uint draw=uint(u.vertices.v[1500000u+3u*primitive].w);
        if(draw>=uint(u.axisX.w) || u.draws.draw[draw].instanceLink==0u)return false;
        inst=u.draws.draw[draw].instanceLink-1u;tri=primitive-u.draws.draw[draw].base;
    }else return false;
    if(inst>=g.instanceCount)return false;
    TreePlacement placement=g.placements.items[inst];uint model=placement.key.x;
    TreeShape shape=g.shapes.items[model];
    if(tri>=shape.count || (shape.pbr&0x40000000u)==0u)return false;
    uint frames=g.shapeCount*8192u;
    TreeShape last=g.shapes.items[g.shapeCount-1u];uint map=frames+6u*(last.first+last.count)+8193u*model;
    vec3 n=vec3(0),t=vec3(0),w=vec3(1.-bary.x-bary.y,bary);
    vec2 uv=vec2(0);float signSum=0.;
    for(uint j=0u;j<3u;j++){
        uint k=3u*(shape.first+tri)+j,nw=g.colors.v[frames+2u*k],tw=g.colors.v[frames+2u*k+1u];
        if((nw>>30u)==0u)return false;
        n+=w[j]*unpackPropDirection(nw);t+=w[j]*unpackPropDirection(tw);
        signSum+=w[j]*((tw>>30u)==2u?-1.:(tw>>30u)==1u?1.:0.);
        uv+=w[j]*unpackHalf2x16(floatBitsToUint(g.mesh.v[k].w));
    }
    vec3 x=placement.x.xyz,y=placement.y.xyz,z=placement.z.xyz;
    float orientation=sign(dot(x,cross(y,z)));
    vec3 worldN=orientation*vec3(dot(cross(y,z),n),dot(cross(z,x),n),dot(cross(x,y),n));
    vec3 worldT=vec3(dot(x,t),dot(y,t),dot(z,t));
    return applyNativeFrame(worldN,worldT,signSum*orientation,uv,g.colors,map,g.colors.v[map]!=0u,smoothN,detailN,reverseN);
}
