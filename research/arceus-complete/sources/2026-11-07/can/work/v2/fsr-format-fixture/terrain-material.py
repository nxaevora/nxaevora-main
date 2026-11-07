from pathlib import Path
p=Path('work/assets/adaptive-terrain.cpp');s=p.read_text().replace('static int side;', 'static int side;\nstatic bool fitColors;')
s=s.replace('local=std::max(local,color_error','if(fitColors)local=std::max(local,color_error').replace('if(argc!=4)return 1;', 'if(argc!=5)return 1;\n    fitColors=atoi(argv[4])!=0;')
p.write_text(s)
p=Path('work/assets/export-adaptive.py');s=p.read_text().replace('import hashlib\n','')
s=s.replace('def convert(path,src,dst,tool,temp):','def convert(path,src,dst,tool,temp,vertex_colors):').replace('native,mesh],check=True','native,mesh,str(int(vertex_colors))],check=True')
s=s.replace('f.write(struct.pack("<4sIIIII",b"LAND",3,1,archive,nv,nt));out.tofile(f);triangles.tofile(f)','''f.write(struct.pack("<4sIIIII",b"LAND",3 if vertex_colors else 4,1,archive,nv,nt))
        if not vertex_colors: f.write(struct.pack("<4f",ox,oz,sx,sz))
        out.tofile(f);triangles.tofile(f)
        if not vertex_colors: rgba.tofile(f)
    if not vertex_colors:
        with (dst/(path.stem+".land")).open("rb") as tile:
            tile.seek(40+16*nv+12*nt)
            assert tile.read()==rgba.tobytes()
        material_coords=(out[:,[0,2]].astype("float64")-[ox,oz])/[sx,sz]
        assert np.max(np.abs(material_coords-points/step))<.001''')
s=s.replace('np.percentile(color_error,[95,99,100]),flush=True','np.percentile(color_error,[95,99,100]) if vertex_colors else "source texels verified",flush=True')
s=s.replace('tool.resolve(),Path(scratch))','tool.resolve(),Path(scratch),"--vertex-colors" in sys.argv[4:])');p.write_text(s)
p=Path('work/v2/terrain.inc');s=p.read_text().replace('    VkDeviceAddress index;','    VkDeviceAddress index,colors;\n    float materialGrid[4];',1)
s=s.replace('u32 triangles,pad;} LandInfo;','u32 triangles,pad;VkDeviceAddress colors;u32 colorSide,pad2;float grid[4];} LandInfo;')
s=s.replace('sizeof(LandInfo)==112','offsetof(LandInfo,grid)==128 && sizeof(LandInfo)==144')
s=s.replace('const bool regular=h.version==2;', 'const bool regular=h.version==2, texelColor=h.version==4;')
s=s.replace('}else if(h.version!=3 ||','}else if((h.version!=3 && !texelColor) ||')
s=s.replace('    size_t bytes=vertexBytes+indexWords*sizeof(u32);', '''    float grid[4]={0};
    if(texelColor && (fread(grid,sizeof grid,1,file)!=1 || !isfinite(grid[0]) || !isfinite(grid[1]) ||
       !(grid[2]>0 && grid[2]<=32 && grid[3]>0 && grid[3]<=32)))return false;
    size_t colorWords=texelColor?TERRAIN_SIDE*TERRAIN_SIDE:0;
    size_t bytes=vertexBytes+(indexWords+colorWords)*sizeof(u32);''')
s=s.replace('    ok=ok && fgetc(file)==EOF;', '    if(ok && colorWords)ok=fread(indices+indexWords,sizeof(u32),colorWords,file)==colorWords;\n    ok=ok && fgetc(file)==EOF;')
s=s.replace('        area->vertexCount=nv;', '''        if(texelColor){
            area->colors=area->index+indexWords*sizeof(u32);
            grid[2]=1.f/grid[2];grid[3]=1.f/grid[3];
            memcpy(area->materialGrid,grid,sizeof grid);
        }
        area->vertexCount=nv;''')
s=s.replace('.triangles=area->triangleCount,.enabled=1};','.triangles=area->triangleCount,.colors=area->colors,.colorSide=TERRAIN_SIDE,.enabled=1};\n    memcpy(land.gpu.grid,area->materialGrid,sizeof land.gpu.grid);')
p.write_text(s)
p=Path('work/v2/contact.comp');s=p.read_text().replace('Words indices;uint triangles;uint pad;};','Words indices;uint triangles;uint pad;uvec2 colors;uint colorSide;uint pad2;vec4 grid;};')
a=s.index('        vec4 ca=unpackUnorm4x8(floatBitsToUint(mesh.v[ids.x].w));')
b=s.index('        if(tint.a<',a)
s=s[:a]+'''        vec3 weight=vec3(1.-bary.x-bary.y,bary);
        vec4 tint;
        if(any(notEqual(u.draws.land.colors,uvec2(0)))){
            vec2 world=mesh.v[ids.x].xz*weight.x+mesh.v[ids.y].xz*weight.y+mesh.v[ids.z].xz*weight.z;
            uint side=u.draws.land.colorSide;
            vec2 uv=clamp((world-u.draws.land.grid.xy)*u.draws.land.grid.zw,vec2(0),vec2(side-1u));
            uvec2 cell=min(uvec2(uv),uvec2(side-2u));
            vec2 f=uv-vec2(cell);
            uint a=cell.y*side+cell.x;
            uvec3 texel;
            if(f.x+f.y<=1.){
                texel=uvec3(a,a+1u,a+side);
                weight=vec3(1.-f.x-f.y,f.x,f.y);
            }else{
                texel=uvec3(a+side+1u,a+side,a+1u);
                weight=vec3(f.x+f.y-1.,1.-f.x,1.-f.y);
            }
            Words colors=Words(u.draws.land.colors);
            tint=unpackUnorm4x8(colors.v[texel.x])*weight.x+unpackUnorm4x8(colors.v[texel.y])*weight.y+
                 unpackUnorm4x8(colors.v[texel.z])*weight.z;
        }else{
            tint=unpackUnorm4x8(floatBitsToUint(mesh.v[ids.x].w))*weight.x+
                 unpackUnorm4x8(floatBitsToUint(mesh.v[ids.y].w))*weight.y+
                 unpackUnorm4x8(floatBitsToUint(mesh.v[ids.z].w))*weight.z;
        }
'''+s[b:];p.write_text(s)
