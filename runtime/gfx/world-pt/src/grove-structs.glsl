struct TreeShape {uint first;uint count;uint cut;uint pbr;uvec4 anchor;};
struct TreePlacement {vec4 x;vec4 y;vec4 z;uvec4 key;};
layout(buffer_reference,std430,buffer_reference_align=16) readonly buffer TreeShapes {TreeShape items[];};
layout(buffer_reference,std430,buffer_reference_align=16) readonly buffer TreePlacements {TreePlacement items[];};
struct GroveInfo {TreeShapes shapes;TreePlacements placements;Vertices mesh;Words colors;Words activeGroups;uint shapeCount;uint instanceCount;};
vec3 treeWorld(TreePlacement p,vec3 v){
    vec4 q=vec4(v,1.);
    return vec3(dot(p.x,q),dot(p.y,q),dot(p.z,q));
}
