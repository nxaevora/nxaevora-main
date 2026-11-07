from pathlib import Path
p=Path('work/v2/build.py');s=p.read_text();k="if '--audit-overlay-rows' in sys.argv[2:]:"
v='''if '--terrain-uniforms' in sys.argv[2:]:
    instrument=Path(args[-1]).read_text()
    marker='    u32 inputVertices=indexed?icount:count;'
    assert instrument.count(marker)==1
    instrument=instrument.replace(marker,"""    static bool terrainUniforms;
    if(!terrainUniforms && g_vk_present>=1950 && fs && fs->hash==0xea4d86b0272a581full){
        terrainUniforms=true;
        for(u32 id=4;id<=8;id++){
            u32 bytes=cb_range(ch,4,fs,id);
            u64 addr=ch->cbind[4][id];
            if(!addr || !bytes || bytes>65536 || vk_buf_pending(addr,bytes) || vk_compute_pending(addr,bytes))continue;
            const void *source=gmem_host(addr,bytes);
            if(source){
                char path[1024];
                const char *dir=getenv("ARCEUS_QA_TERRAIN_PARAMS");
                if(dir && snprintf(path,sizeof path,"%s/cb%u.bin",dir,id)<(int)sizeof path){
                    FILE *out=fopen(path,"wb");
                    if(out){fwrite(source,1,bytes,out);fclose(out);}
                }
            }
        }
    }
"""+marker)
    (tmp/'terrain-uniforms.c').write_text(instrument)
    args[-1]=str(tmp/'terrain-uniforms.c')
'''
assert s.count(k)==1;p.write_text(s.replace(k,v+k))
