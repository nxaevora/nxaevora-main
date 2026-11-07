int tileCoordinate(int i,uint mode){
    if(mode==0u)return i&63;
    if(mode==1u){int k=i&127;return k<64?k:127-k;}
    return clamp(i,0,63);
}
vec3 fallbackAlbedo(DrawMaterial m,vec2 uv){
    vec2 p=uv*64.-.5,f=fract(p);ivec2 base=ivec2(floor(p));
    Words tile=Words(m.fallbackColor);vec3 c=vec3(0);
    for(int y=0;y<2;y++)for(int x=0;x<2;x++){
        uint k=uint(tileCoordinate(base.y+y,(m.wrap>>3u)&7u)*64+tileCoordinate(base.x+x,m.wrap&7u));
        vec3 sampleColor=vec3(unpackHalf2x16(tile.v[2u*k]),unpackHalf2x16(tile.v[2u*k+1u]).x);
        c+=sampleColor*((x==0?1.-f.x:f.x)*(y==0?1.-f.y:f.y));
    }
    return clamp(c,vec3(0),vec3(.95));
}
