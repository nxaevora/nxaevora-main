// The equal-area (azimuth, vertical cosine) grid has constant cell solid angle.
shared float rowMass[32];
void main(){
    uint row=gl_LocalInvocationIndex;
    if(row<32u){
        float sum=0.;
        for(uint col=0u;col<64u;col++){
            float power=0.;
            for(uint i=0u;i<4u;i++){
                vec2 uv=(vec2(col,row)+(.25+.5*vec2(i&1u,i>>1u)))/vec2(64,32);
                vec3 color=textureLod(environmentMap,probeDirection(skyCellDirection(uv)),0.).rgb;
                power+=.25*dot(max(color,vec3(0)),vec3(.2126,.7152,.0722));
            }
            if(!isnan(power) && !isinf(power))sum+=min(power,65504.);
            u.draws.skyColumns[64u*row+col]=sum;
        }
        rowMass[row]=sum;
    }
    barrier();
    if(row==0u){
        float sum=0.;
        for(uint i=0u;i<32u;i++){sum+=rowMass[i];u.draws.skyRows[i]=sum;}
    }
}
