from pathlib import Path
import re
v=Path("work/v2")
def change(name,old,new):
    p=v/name
    s=p.read_text()
    if s.count(old)!=1:raise ValueError((name,old))
    p.write_text(s.replace(old,new))
change("material.inc","u64 fallbackColor;u32 wrap,pad;","u64 fallbackColor;u32 wrap,instanceLink;\n    u64 roughTile,metalTile;u32 roughWrap,metalWrap,packedProperties,reserved;")
change("material.inc",'sizeof(WorldDrawMaterial)==64','sizeof(WorldDrawMaterial)==96 && offsetof(WorldDrawMaterial,roughTile)==64 && offsetof(WorldDrawMaterial,packedProperties)==88')
change("material.inc","draw->metallic=draw->roughness;break;","draw->metallic=draw->roughness;draw->packedProperties=1;break;")
change("material.inc","static u16 baseTextureId[worldDrawLimit];","static u16 baseTextureId[worldDrawLimit],roughTextureId[worldDrawLimit],metalTextureId[worldDrawLimit];")
change("material.inc","baseTextureId[i]=(d->slot>>16)&0x7fff;","baseTextureId[i]=(d->slot>>16)&0x7fff;\n        roughTextureId[i]=d->roughness;metalTextureId[i]=d->metallic;")
marker="        if(draws[j].emission && infos"
change("material.inc",marker,"""        if(roughTextureId[j] && (!draws[j].roughness || tileWitness))
            draws[j].roughTile=material_color_tile(cb,&capturedTextures[roughTextureId[j]],ro,&draws[j].roughWrap);
        if(!draws[j].packedProperties && metalTextureId[j] && (!draws[j].metallic || tileWitness))
            draws[j].metalTile=material_color_tile(cb,&capturedTextures[metalTextureId[j]],ro,&draws[j].metalWrap);
"""+marker)
change("contact.comp","uint wrap;uint pad;","uint wrap;uint instanceLink;uvec2 roughTile;uvec2 metalTile;uint roughWrap;uint metalWrap;uint packedProperties;uint reserved;")
for n in ["grove-witness.glsl","instance-shading.glsl"]:
    p=v/n
    p.write_text(p.read_text().replace(".pad",".instanceLink"))
p=v/"material-tile.glsl"
s=p.read_text().replace("fallbackAlbedo(DrawMaterial m,vec2 uv)","materialTile(uvec2 address,uint wrap,vec2 uv)").replace("Words(m.fallbackColor)","Words(address)").replace("m.wrap","wrap").replace("return clamp(c,vec3(0),vec3(.95));","return c;")
p.write_text(s+"\nvec3 fallbackAlbedo(DrawMaterial m,vec2 uv){\n    return clamp(materialTile(m.fallbackColor,m.wrap,uv),vec3(0),vec3(.95));\n}\n")
p=v/"contact.comp";s=p.read_text();a=s.index("    if(m.roughness==0u");b=s.index("    rough=clamp(rough",a)
s=s[:a]+"""    bool roughBound=m.roughness!=0u && m.roughness<textureCapacity;
    bool metalBound=m.metallic!=0u && m.metallic<textureCapacity;
    if(!roughBound){
        uint id;
        if(matchingProp(primitive,draw,id)){
            vec4 native=propTexel(id,bary,Words(u.draws.land.props.properties));
            if(native.a>.5){rough=max(.04,native.r);metal=native.g;return true;}
        }
        if(all(equal(m.roughTile,uvec2(0))))return false;
    }
    vec2 uv=a.xy*(1.-bary.x-bary.y)+u.vertices.v[start+1].xy*bary.x+u.vertices.v[start+2].xy*bary.y;
    if(any(isnan(uv)) || any(isinf(uv)) || any(greaterThan(abs(uv),vec2(1000000.))))return false;
    vec3 r=roughBound?textureLod(drawTextures[nonuniformEXT(m.roughness)],uv,1.).rgb:materialTile(m.roughTile,m.roughWrap,uv);
    bool packedMap=m.packedProperties!=0u;
    rough=packedMap?r.g:r.r;
    bool metalTiled=!packedMap && !metalBound && any(notEqual(m.metalTile,uvec2(0)));
    if(packedMap)metal=r.b;
    else if(metalBound)metal=textureLod(drawTextures[nonuniformEXT(m.metallic)],uv,1.).r;
    else if(metalTiled)metal=materialTile(m.metalTile,m.metalWrap,uv).r;
    if(any(notEqual(u.stats,uvec2(0))) && all(equal(gl_GlobalInvocationID.xy%8u,uvec2(3,6)))){
        Words q=Words(u.stats);
        if(!roughBound)atomicAdd(q.v[11960],1u);
        if(metalTiled || (packedMap && !roughBound))atomicAdd(q.v[11961],1u);
        if(u.frame==600u || u.frame==1960u){
            uint k=u.frame==600u?11962u:11968u;
            if(roughBound && any(notEqual(m.roughTile,uvec2(0)))){
                vec3 sampleValue=materialTile(m.roughTile,m.roughWrap,uv);
                atomicAdd(q.v[k],1u);atomicAdd(q.v[k+1u],uint(round(4096.*abs(rough-(packedMap?sampleValue.g:sampleValue.r)))));
                atomicAdd(q.v[k+2u],uint(round(4096.*abs(1.-rough))));
            }
            if((packedMap && roughBound && any(notEqual(m.roughTile,uvec2(0)))) ||
               (!packedMap && metalBound && any(notEqual(m.metalTile,uvec2(0))))){
                float val=packedMap?materialTile(m.roughTile,m.roughWrap,uv).b:materialTile(m.metalTile,m.metalWrap,uv).r;
                atomicAdd(q.v[k+3u],1u);atomicAdd(q.v[k+4u],uint(round(4096.*abs(metal-val))));
                atomicAdd(q.v[k+5u],uint(round(4096.*abs(metal))));
            }
        }
    }
"""+s[b:];p.write_text(s)
mark='        LOG("world-pt: sampled overflow albedo='
change("contact.h",mark,r'''        LOG("world-pt: overflow property samples rough=%u metal=%u\n",contact.mappedCounters[11960],contact.mappedCounters[11961]);
        for(u32 k=11962;k<=11968;k+=6)
            LOG("world-pt: property tile reference present=%u rough=%u error=%u default=%u metal=%u error=%u default=%u\n",
                k==11962?600:1960,contact.mappedCounters[k],contact.mappedCounters[k+1],contact.mappedCounters[k+2],
                contact.mappedCounters[k+3],contact.mappedCounters[k+4],contact.mappedCounters[k+5]);
'''+mark)
