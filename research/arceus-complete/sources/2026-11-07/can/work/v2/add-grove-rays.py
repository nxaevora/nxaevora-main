from pathlib import Path
p=Path('work/v2/contact.comp');s=p.read_text()
i=s.index('    vec2 r=fract(uv)');j=s.index('\n}',i)+2
body=s[i:j].replace('uint base=(props.tags.v[id]&255u)*4096u;','uint base=material*4096u;')
s=s[:i]+'    return samplePropAtlas(atlas,props.tags.v[id]&255u,uv);\n}'+s[j:]
mark='vec4 propTexel(uint id,vec2 bary,Words atlas){'
s=s.replace(mark,'vec4 samplePropAtlas(Words atlas,uint material,vec2 uv){\n'+body+'\n'+mark)
s=s.replace('bool acceptsTriangle(uint id,vec2 bary){','#include "grove-rays.glsl"\nbool acceptsTriangle(uint id,vec2 bary){')
s=s.replace(':acceptsProp(id,bc)',':acceptsInstance(rayQueryGetIntersectionInstanceCustomIndexEXT(ray,false),id,bc)')
s=s.replace('    bool prop=!guest && rayQueryGetIntersectionInstanceCustomIndexEXT(ray,true)==1;','''    uint custom=guest?0u:rayQueryGetIntersectionInstanceCustomIndexEXT(ray,true);
    if(custom>=2u){
        uint instance=custom-2u;
        if(!treeSurface(instance,primitive) || distanceHit<nearT || distanceHit>farT)return false;
        primitive=propFlag|treeBit|(instance<<14u)|primitive;return true;
    }
    bool prop=custom==1u;''')
s=s.replace('    if(found && !guest && rayQueryGetIntersectionInstanceCustomIndexEXT(ray,true)==1){\n        propShadow++;\n        if(placedProp(rayQueryGetIntersectionPrimitiveIndexEXT(ray,true)))placedShadow++;\n    }','''    if(found && !guest){
        uint instance=rayQueryGetIntersectionInstanceCustomIndexEXT(ray,true);
        if(instance>0u){
            propShadow++;
            if(instance>=2u || placedProp(rayQueryGetIntersectionPrimitiveIndexEXT(ray,true)))placedShadow++;
        }
    }''')
s=s.replace('        uint j=3u*(primitive&~propFlag);','        if((primitive&treeBit)!=0u)return treeFace(primitive&~propFlag);\n        uint j=3u*(primitive&~propFlag);')
s=s.replace('draw=2048u;vec3 c=propColor(primitive&~propFlag,bary).rgb;', '''draw=2048u;uint id=primitive&~propFlag;
        vec3 c=(id&treeBit)!=0u?treeTexel(treeInstance(id),treePrimitive(id),bary).rgb:propColor(id,bary).rgb;''')
s=s.replace('    rough=1.;metal=0.;','''    rough=1.;metal=0.;
    if((primitive&(treeBit|propFlag))==(treeBit|propFlag)){
        GroveInfo g=u.draws.land.grove;
        uint model=g.placements.items[treeInstance(primitive&~propFlag)].key.x;
        uint pbr=g.shapes.items[model].pbr;
        rough=max(.04,float(pbr&255u)/255.);metal=float((pbr>>8u)&255u)/255.;
        return true;
    }''')
s=s.replace('if(placedProp(primitive&~propFlag))placedBounce++;','if((primitive&treeBit)!=0u || placedProp(primitive&~propFlag))placedBounce++;')
p.write_text(s)
