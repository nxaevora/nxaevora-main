from pathlib import Path
base=Path('work/v2')
p=base/'props.inc';s=p.read_text().replace('u64 properties,padding;','u64 properties;u32 firstPlaced,padding;').replace('(h.version==1 || h.version==2)','(h.version>=1 && h.version<=3)').replace('    if(!ok){fclose(f);return false;}','''    u32 firstPlaced=h.groups;
    if(ok && h.version==3)ok=fread(&firstPlaced,sizeof firstPlaced,1,f)==1 && firstPlaced<=h.groups;
    if(!ok){fclose(f);return false;}''').replace('h.version==2?','h.version>=2?').replace('address+properties:0};','address+properties:0,.firstPlaced=firstPlaced};')
p.write_text(s)
p=base/'contact.comp';s=p.read_text().replace('uvec2 properties;uvec2 padding;','uvec2 properties;uint firstPlaced;uint padding;').replace('if(stamp==0u || stamp==u.frame+1u)','if((stamp==0u && (tag>>16)<props.firstPlaced) || stamp==u.frame+1u)').replace('uint alphaRejected=0,propShadow=0;','uint alphaRejected=0,propShadow=0,placedShadow=0,placedBounce=0;\nbool placedProp(uint id){return (u.draws.land.props.tags.v[id]>>16)>=u.draws.land.props.firstPlaced;}').replace('rayQueryGetIntersectionInstanceCustomIndexEXT(ray,true)==1)propShadow++;','''rayQueryGetIntersectionInstanceCustomIndexEXT(ray,true)==1){
        propShadow++;
        if(placedProp(rayQueryGetIntersectionPrimitiveIndexEXT(ray,true)))placedShadow++;
    }''').replace('else retainedBounce++;','else {retainedBounce++;if(placedProp(primitive&~propFlag))placedBounce++;}').replace('        if(terrainPbr!=0u)','        if(placedShadow!=0u)atomicAdd(diagnostics.v[16376],placedShadow);\n        if(placedBounce!=0u)atomicAdd(diagnostics.v[16377],placedBounce);\n        if(terrainPbr!=0u)')
p.write_text(s)
p=base/'contact.h';s=p.read_text().replace('depthConfirmedCutouts=%u terrainPbrHits=%u','depthConfirmedCutouts=%u terrainPbrHits=%u placedShadow=%u placedBounce=%u').replace('contact.mappedCounters[16378]);','contact.mappedCounters[16378],contact.mappedCounters[16376],contact.mappedCounters[16377]);');p.write_text(s)
