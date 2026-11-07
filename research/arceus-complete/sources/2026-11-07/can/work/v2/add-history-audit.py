from pathlib import Path
v=Path("work/v2")
def edit(name,a,b):
    path=v/name;s=path.read_text();
    if b in s:return
    assert s.count(a)==1,(name,a);path.write_text(s.replace(a,b))
edit("build.py","'-o',target,tmp/'denoise.comp'","*(['-DHISTORY_WITNESS'] if '--history-witness' in sys.argv else []),'-o',target,tmp/'denoise.comp'")
edit("build.py","args+reflection_flags+(['-DEMISSION_WITNESS']","args+reflection_flags+(['-DHISTORY_WITNESS'] if '--history-witness' in sys.argv else [])+(['-DEMISSION_WITNESS']")
edit("reference.inc","    if(reference.frame &&", """#ifdef HISTORY_WITNESS
    bytes=6ull*referenceWidth*referenceHeight*16;
#endif
    if(reference.frame &&""")
p=v/"denoise.comp";s=p.read_text()
def before(mark,code):
    global s
    assert s.count(mark)==1,mark;s=s.replace(mark,"\n#ifdef HISTORY_WITNESS\n"+code+"\n#endif\n"+mark)
before("    if(age>0. && frame.historyValid", """    uint passes=uint(age>0.)+2u*uint(frame.historyValid!=0u);
    vec4 distances=vec4(-1);
    vec3 projection=vec3(0);""")
before("        if(clip.w>0.", """        projection=vec3(length(delta),clip.z/clip.w,z);
        passes|=4u*uint(clip.w>0.);""")
before("            ivec2 origin=", "            passes|=8u;")
before("                if((data.x&4095u)!=materialID)", """                if((data.x&4095u)==materialID)passes|=16u;
                if(x==0 && y==0){
                    distances.x=abs(clip.z/clip.w-uintBitsToFloat(data.x&0xfffff000u))/max(.000001,z);
                    distances.y=distance(material.rgb,surface);
                    distances.z=dot(guideNormal(uint(baseAt(q,size,frame.layer^1u).a)),guideNormal(uint(base.a)));
                    distances.w=material.a*255.;
                }""")
before("                if(material.a==0.", """                if(material.a>0.)passes|=32u;
                if(material.a>0. && abs(expected-previousZ)<=max(2e-6,.015*expected))passes|=64u;""")
before("                float weight=(x==1?", "                passes|=128u;")
before("                if(((uint(oldBase.a)", """                if(((uint(oldBase.a)^uint(base.a))&0x4000u)==0u)passes|=256u;""")
before("                previous+=weight", "                passes|=512u;")
before("                vec2 historyMoments=", "                passes|=1024u;")
before("        ref.v[480u*270u+i]", """        ref.v[4u*480u*270u+i]=vec4(float(passes),projection);
        ref.v[5u*480u*270u+i]=distances;""")
p.write_text(s)

