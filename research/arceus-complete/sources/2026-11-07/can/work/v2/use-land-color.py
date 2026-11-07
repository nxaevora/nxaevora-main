from pathlib import Path
f=Path('work/assets/land-color.py');s=f.read_text().replace('   if count!=(gw-1)*(gh-1):continue\n   weights=np.frombuffer(data,\'u1\',count,off).reshape(gh-1,gw-1)[np.ix_(z,x)]', '''   side=round(count**.5)
   if side<2 or side*side!=count:continue
   sample=np.clip(np.rint(np.linspace(0,side,w)).astype(int),0,side-1)
   weights=np.frombuffer(data,'u1',count,off).reshape(side,side)[np.ix_(sample,sample)]''')
f.write_text(s)
p=Path('work/v2')
f=p/'terrain.inc';s=f.read_text().replace('head.version==1','head.version==2').replace('    valid=height && fread(height,sizeof(float),n,file)==n && fgetc(file)==EOF;', '''    u32 *colors=height?malloc(n*sizeof(u32)):NULL;
    valid=height && colors && fread(height,sizeof(float),n,file)==n && fread(colors,sizeof(u32),n,file)==n && fgetc(file)==EOF;''').replace('vertex[i][3]=1;','memcpy(&vertex[i][3],&colors[i],4);').replace('    free(height);','    free(height);free(colors);');f.write_text(s)
f=p/'contact.comp';s=f.read_text();s=s.replace('    if((primitive&landFlag)!=0u){draw=2047u;value=vec3(.36);return false;}', '''    if((primitive&landFlag)!=0u){
        draw=2047u;
        uint tri=primitive&~landFlag,side=u.draws.land.side,cell=tri/2u;
        uint a=cell/(side-1u)*side+cell%(side-1u),b=a+1u,c=a+side,d=c+1u;
        uvec3 ids=tri%2u==0u?uvec3(a,c,b):uvec3(b,c,d);
        Vertices mesh=u.draws.land.vertices;
        vec4 ca=unpackUnorm4x8(floatBitsToUint(mesh.v[ids.x].w));
        vec4 cb=unpackUnorm4x8(floatBitsToUint(mesh.v[ids.y].w));
        vec4 cc=unpackUnorm4x8(floatBitsToUint(mesh.v[ids.z].w));
        vec4 tint=ca*(1.-bary.x-bary.y)+cb*bary.x+cc*bary.y;
        if(tint.a<.65){value=vec3(.36);return false;}
        value=mix(tint.rgb/12.92,pow((tint.rgb+.055)/1.055,vec3(2.4)),greaterThan(tint.rgb,vec3(.04045)));
        return true;
    }''')
f.write_text(s)
