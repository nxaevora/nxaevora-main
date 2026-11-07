shared uint liveDraw;
void main(){
    PropInfo p=u.draws.land.props;uint group=gl_WorkGroupID.x,lane=gl_LocalInvocationIndex;
    if(group>=p.groups || p.emissionBanks==0u)return;
    Words records=Words(p.emissionData);if(records.v[4u*group]==0u)return;
    uint first=(uint(p.checks.items[group].position[1].w)-1u)/3u;
    uint count=p.checks.items[group].key.x;
    if(lane==0u)liveDraw=0xffffffffu;
    barrier();
    for(uint j=lane;j<uint(u.axisX.w);j+=64u)
        if(u.draws.tint[j].surface.z==1u+first)atomicMin(liveDraw,j);
    barrier();
    Words stats=Words(u.stats);
    if(lane==0u){
        atomicAdd(stats.v[11940],1u);
        if(p.activeGroups.v[group]==u.frame+1u)atomicAdd(stats.v[11941],1u);
        if(liveDraw!=0xffffffffu)atomicAdd(stats.v[11942],1u);
    }
    if(liveDraw==0xffffffffu)return;
    DrawMaterial m=u.draws.draw[liveDraw];
    if(m.emission==0u || m.emission>=textureCapacity || m.emissionScale<=0.)return;
    for(uint k=lane;k<count;k+=64u){
        uint s=hash(k+group*10197u),tri=first+k;
        float r=sqrt(sample01(s)),v=sample01(s+1u);
        vec3 w=vec3(1.-r,r*(1.-v),r*v);
        vec3 actual=vec3(0),native=vec3(0);vec2 uv=vec2(0);
        for(uint j=0u;j<3u;j++){
            uint at=3u*(m.base+k)+j;
            actual+=w[j]*u.vertices.v[at].xyz;uv+=w[j]*u.vertices.v[1500000u+at].xy;
            native+=w[j]*p.mesh.v[3u*tri+j].xyz;
        }
        native=fromWorldDirection(native-u.origin.xyz);
        float err=distance(actual,native);
        atomicAdd(stats.v[11943],1u);atomicMax(stats.v[11944],floatBitsToUint(err));
        bool enabled;vec3 archived=retainedEmission(tri,w.yz,enabled);
        vec3 ground=m.emissionScale*textureLod(drawTextures[nonuniformEXT(m.emission)],uv,1.).rgb;
        if(!enabled)continue;
        if(any(greaterThan(max(archived,ground),vec3(.004)))){
            atomicAdd(stats.v[11945],1u);
            atomicAdd(stats.v[11946],uint(round(4096.*dot(abs(ground-archived),vec3(1./3.)))));
            atomicAdd(stats.v[11947],uint(round(4096.*dot(ground,vec3(1./3.)))));
        }
        if(k%4u!=0u)continue;
        ivec2 extent=textureSize(depthMap,0);
        ivec2 pixel=ivec2(vec2(sample01(s+2u),sample01(s+3u))*vec2(extent));
        float depth=texelFetch(depthMap,pixel,0).r;if(depth<=0.)continue;
        float z=1./max(1.e-7,depth*u.camera.z+u.camera.w);
        vec2 xy=((vec2(pixel)+.5-u.draws.land.jitter-u.draws.land.viewport.xy)/u.draws.land.viewport.zw*2.-1.)*u.camera.xy;
        vec3 origin=z*vec3(xy,1.);vec3 a=actual-origin,b=native-origin;
        float la=length(a),lb=length(b);if(min(la,lb)<.08)continue;
        vec3 hit;uint id;vec2 bary;
        bool blockedA=traceScene(origin,a/la,.025,la-.025,hit,id,bary);
        bool blockedB=traceScene(origin,b/lb,.025,lb-.025,hit,id,bary);
        atomicAdd(stats.v[11948],1u);
        if(blockedA==blockedB)atomicAdd(stats.v[11949],1u);
        if(!blockedA)atomicAdd(stats.v[11950],1u);
    }
}
