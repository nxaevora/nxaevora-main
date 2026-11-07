// Isotropic single scattering in the captured smoke shader. RGB stores the
// fraction removed by a blocked sun; alpha and the guest blend equation stay native.
void main(){
    uvec3 cell=gl_GlobalInvocationID;
    const uvec3 extent=uvec3(60,34,44);
    if(any(greaterThanEqual(cell,extent)))return;
    vec3 uv=(vec3(cell)+.5)/vec3(extent);
    float z=exp2(uv.z*11.-1.);
    vec3 point=vec3((((uv.xy*vec2(1920,1080)-u.draws.land.viewport.xy)/u.draws.land.viewport.zw)*2.-1.)*u.camera.xy*z,z);
    vec3 blocked=vec3(0);
    if(u.draws.lightDirection.w>0.){
        uint ignored=0;
        vec3 sun=u.draws.lightRadiance.rgb/(4.*3.14159265);
        if(any(greaterThan(sun,vec3(.00001))) &&
           shadowed(point,fromWorldDirection(u.draws.lightDirection.xyz),max(.004,.00008*z),ignored)){
            vec3 ambient=vec3(0);
            // A diffuse irradiance cubemap convolved with a normalized cosine lobe
            // retains the spherical mean. Four tetrahedral directions estimate that mean.
            for(int i=0;i<4;i++){
                vec3 d=normalize(vec3((i&1)!=0?1.:-1.,(i&2)!=0?1.:-1.,(i==1 || i==2)?1.:-1.));
                ambient+=.25*max(textureLod(diffuseProbe,probeDirection(d),0.).rgb,vec3(0));
            }
            blocked=sun/max(sun+ambient,vec3(.0001));
        }
    }
    u.draws.particleGrid.v[cell.x+extent.x*(cell.y+extent.y*cell.z)]=packUnorm4x8(vec4(blocked,1));
}
