// Native deferred lighting flips world Z for both cubemap lookups.
vec3 probeDirection(vec3 world){return vec3(world.xy,-world.z);}
vec3 skyCellDirection(vec2 uv){
    float phi=6.28318530718*(uv.x-.5),y=1.-2.*uv.y,r=sqrt(max(0.,1.-y*y));
    return vec3(r*cos(phi),y,r*sin(phi));
}
