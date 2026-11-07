from pathlib import Path
base=Path('work/v2');p=base/'props.inc';s=p.read_text().replace('u32 firstPlaced,padding;','u32 firstPlaced,classCount;')
s=s.replace('static struct {ContactBuffer data;ContactTree bottom;PropInfo info;u64 activeOffset;int attempted;} props;','typedef struct {ContactBuffer data;ContactTree bottom;PropInfo info;u64 activeOffset;bool attempted,built;} PropArea;\nstatic PropArea propAreas[12];')
s=s.replace('static bool props_load(u32 archiveSize){','static bool props_load(PropArea *props,u32 area,u32 archiveSize){').replace('props.','props->').replace('"%s/ha_area00.props",dir','"%s/ha_area%02u.props",dir,area').replace('h.version<=3','h.version<=4').replace('if(ok && h.version==3)','if(ok && h.version>=3)')
s=s.replace('    u64 tags=','    u32 classes=h.version==4?h.materials:0;\n    u64 tags=').replace('active+4ull*h.groups','active+4ull*(h.groups+classes)').replace('        memset(data+active,0,4ull*h.groups);','''        if(classes){
            for(u32 i=0;ok && i<h.groups;i++){
                float model=((const float*)(data+check+64*i))[7];
                if(!(model>=1.f && model<=classes && floorf(model)==model))ok=false;
            }
        }
        memset(data+active,0,4ull*(h.groups+classes));''').replace('    if(!ok)return false;\n    props->bottom','''    if(!ok){
        if(props->data.handle)vkDestroyBuffer(g_vk.dev,props->data.handle,NULL);
        if(props->data.memory)vkFreeMemory(g_vk.dev,props->data.memory,NULL);
        props->data=(ContactBuffer){0};return false;
    }
    props->bottom''').replace('.firstPlaced=firstPlaced};','.firstPlaced=firstPlaced,.classCount=classes};')
p.write_text(s)
p=base/'terrain.inc';s=p.read_text().replace('bool haveProps=id==0 && props_load((u32)(request>>8));','PropArea *props=&propAreas[id];\n    bool haveProps=props_load(props,id,(u32)(request>>8));')
s=s.replace('props.bottom','props->bottom').replace('props.data','props->data').replace('props.activeOffset','props->activeOffset').replace('props.info','props->info')
s=s.replace('props->info.groups*4ull','(props->info.groups+props->info.classCount)*4ull').replace('if(haveProps)contact_build(cb,&props->bottom);','if(haveProps && !props->built){contact_build(cb,&props->bottom);props->built=true;}').replace('village prop geometry loaded, %u triangles in %u groups\\n",props->info.triangles,props->info.groups','ha_area%02u prop geometry loaded, %u triangles in %u groups (%u field model classes)\\n",id,props->info.triangles,props->info.groups,props->info.classCount')
p.write_text(s)
p=base/'contact.comp';s=p.read_text().replace('uint firstPlaced;uint padding;','uint firstPlaced;uint classCount;')
s=s.replace('if(matches){props.activeGroups.v[group]=u.frame+1u;return;}','''if(matches){
                props.activeGroups.v[group]=u.frame+1u;
                if(props.classCount>0u){
                    uint model=uint(w.position[0].w)-1u;
                    if(model<props.classCount)atomicOr(props.activeGroups.v[props.groups+model],1u);
                }
                return;
            }''')
s=s.replace('    uint cutoff=(tag>>8)&255u;', '''    if(props.classCount>0u && props.activeGroups.v[props.groups+(tag&255u)]==0u)return false;
    uint cutoff=(tag>>8)&255u;''')
p.write_text(s)
