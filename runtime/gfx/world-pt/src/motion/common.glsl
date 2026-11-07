#ifndef MATERIAL_FLOW_GLSL
#define MATERIAL_FLOW_GLSL
layout(set=0,binding=0) uniform sampler2D sceneColor;
layout(set=0,binding=1) uniform sampler2D sceneDepth;
layout(set=0,binding=2) uniform sampler2D materialTexture;
layout(rg16f,set=0,binding=3) uniform image2D motionImage;
layout(set=0,binding=4) uniform usampler2D history;
layout(rg32ui,set=0,binding=5) uniform writeonly uimage2D nextHistory;
layout(r32i,set=0,binding=6) uniform readonly iimage2D previousStats;
layout(r32i,set=0,binding=7) uniform iimage2D stats;
layout(rgba16f,set=0,binding=8) uniform image2D candidates;
layout(r8,set=0,binding=9) uniform writeonly image2D reactiveMask;
layout(push_constant) uniform FlowParams {
    mat4 clipToPrevious;
    vec2 jitter;
    vec2 previousJitter;
    uint historyValid;
    float refineStep;
} pc;

const int materialCount = 1024;
const int sampleStride = 4;
const int statCount = 25;
const int cellSize = 384;
const int countField = 0, sumX = 1, sumY = 2, maxX = 3, maxY = 4, invX = 5, invY = 6;
const int visibleCount = 7, projectedX = 8, projectedY = 9;
const int projectedMaxX = 10, projectedMaxY = 11, projectedInvX = 12, projectedInvY = 13;
const int wins = 14, losses = 15;

// Overlapping spatial partitions keep distant instances sharing a material independent.
ivec3 buckets(vec2 pixel,ivec2 size) {
    ivec2 grid = (size + cellSize/2 + cellSize-1) / cellSize;
    ivec2 p = clamp(ivec2(floor(pixel+.5)),ivec2(0),size-1);
    ivec2 local = p/cellSize, shifted=(p+cellSize/2)/cellSize;
    return ivec3(0,1+local.y*grid.x+local.x,1+grid.x*grid.y+shifted.y*grid.x+shifted.x);
}
ivec2 statAt(int bucket,int field,int id) { return ivec2(bucket*statCount+field,id); }

uint materialAt(ivec2 p) {
    uvec2 code = uvec2(round(texelFetch(materialTexture,p,0).ba * 255.0));
    uint id = (code.x << 8) | code.y;
    return id < uint(materialCount) ? id : 4095u;
}
bool inside(ivec2 p, ivec2 size) {
    return all(greaterThanEqual(p,ivec2(0))) && all(lessThan(p,size));
}
vec3 tone(vec3 linear) {
    vec3 positive = max(linear, vec3(0));
    return sqrt(positive / (1.0 + positive));
}
float expectedDepth(ivec2 p, ivec2 size, float z) {
    vec2 uv = (vec2(p) + .5 - pc.jitter) / vec2(size);
    vec4 q = pc.clipToPrevious * vec4(2.0*uv.x-1.0,1.0-2.0*uv.y,z,1.0);
    return q.w > 0.0 ? q.z / q.w : -1.0;
}
float matchCost(ivec2 p, ivec2 size, uint id, vec3 color, float expected) {
    if (!inside(p,size) || expected <= 0.0 || expected > 1.0) return 1.0;
    uvec2 feature = texelFetch(history,p,0).rg;
    if ((feature.y >> 20u) != id) return 1.0;
    float z = float(feature.y & 0xfffffu) / 1048575.0;
    float relative = abs(z-expected) / max(.001,expected);
    if (relative > .18) return 1.0;
    vec3 delta = abs(unpackUnorm4x8(feature.x).rgb-color);
    return min(1.0,max(delta.r,max(delta.g,delta.b))+.6*relative);
}
ivec2 lookupPixel(ivec2 p, vec2 flow) {
    return ivec2(floor(vec2(p)+flow+pc.previousJitter-pc.jitter+.5));
}
// A subpixel projection can cover either adjacent geometry sample. During
// voting, penalize the camera only when neither plausible sample fits.
float cameraCost(ivec2 p,vec2 flow,ivec2 size,uint id,vec3 color,float expected) {
    vec2 previous=vec2(p)+flow+pc.previousJitter-pc.jitter;
    ivec2 nearest=ivec2(floor(previous+.5));
    vec2 fraction=previous-vec2(nearest);
    float cost=matchCost(nearest,size,id,color,expected);
    ivec2 step=ivec2(sign(fraction));
    if(abs(fraction.x)>.12)cost=min(cost,matchCost(nearest+ivec2(step.x,0),size,id,color,expected)+.006);
    if(abs(fraction.y)>.12)cost=min(cost,matchCost(nearest+ivec2(0,step.y),size,id,color,expected)+.006);
    if(all(greaterThan(abs(fraction),vec2(.12))))cost=min(cost,matchCost(nearest+step,size,id,color,expected)+.01);
    return cost;
}
float subpixelCost(vec2 previous,ivec2 size,uint id,vec3 color,float expected) {
    ivec2 corner=ivec2(floor(previous));
    vec2 f=fract(previous);
    float sum=0.0,depthError=0.0;vec3 reconstructed=vec3(0);
    for(int y=0;y<2;y++)for(int x=0;x<2;x++){
        ivec2 at=corner+ivec2(x,y);
        float weight=(x==0?1.-f.x:f.x)*(y==0?1.-f.y:f.y);
        if(weight<.001 || !inside(at,size))continue;
        uvec2 feature=texelFetch(history,at,0).rg;
        float depth=float(feature.y&0xfffffu)/1048575.;
        float relative=abs(depth-expected)/max(.001,expected);
        if((feature.y>>20u)!=id || relative>.18)continue;
        reconstructed+=weight*unpackUnorm4x8(feature.x).rgb;depthError+=weight*relative;sum+=weight;
    }
    if(sum<.10 || expected<=0. || expected>1.)return 1.;
    vec3 difference=abs(color-reconstructed/sum);
    return max(difference.r,max(difference.g,difference.b))+.6*depthError/sum+.2*(1.-sum);
}
#endif
