from pathlib import Path
p=Path(__file__).resolve().parent
f=p/'common.glsl';s=f.read_text().replace('const int statCount = 12;', 'const int statCount = 16;\nconst int cellSize = 384;')
s=s.replace('projectedY = 9, wins = 10, losses = 11;', '''projectedY = 9;
const int projectedMaxX = 10, projectedMaxY = 11, projectedInvX = 12, projectedInvY = 13;
const int wins = 14, losses = 15;

// Overlapping spatial partitions keep distant instances sharing a material independent.
ivec3 buckets(vec2 pixel,ivec2 size) {
    ivec2 grid = (size + cellSize/2 + cellSize-1) / cellSize;
    ivec2 p = clamp(ivec2(floor(pixel+.5)),ivec2(0),size-1);
    ivec2 local = p/cellSize, shifted=(p+cellSize/2)/cellSize;
    return ivec3(0,1+local.y*grid.x+local.x,1+grid.x*grid.y+shifted.y*grid.x+shifted.x);
}
ivec2 statAt(int bucket,int field,int id) { return ivec2(bucket*statCount+field,id); }''')
f.write_text(s)
f=p/'measure.comp';s=f.read_text();a=s.index('    imageAtomicAdd(stats,ivec2(countField');b=s.index('    vec2 projected',a)
s=s[:a]+'''    ivec3 currentBuckets = buckets(vec2(p)-pc.jitter,size);
    for (int i=0;i<3;++i) {
        int local=currentBuckets[i];
        imageAtomicAdd(stats,statAt(local,countField,id),1);
        imageAtomicAdd(stats,statAt(local,sumX,id),fixedPoint.x);
        imageAtomicAdd(stats,statAt(local,sumY,id),fixedPoint.y);
        imageAtomicMax(stats,statAt(local,maxX,id),p.x);
        imageAtomicMax(stats,statAt(local,maxY,id),p.y);
        imageAtomicMax(stats,statAt(local,invX,id),size.x-1-p.x);
        imageAtomicMax(stats,statAt(local,invY,id),size.y-1-p.y);
    }
'''+s[b:];a=s.index('    imageAtomicAdd(stats,ivec2(visibleCount');s=s[:a]+'''    vec2 prevPixel=projected+.5*vec2(size);
    ivec2 q=clamp(ivec2(round(prevPixel)),ivec2(0),size-1);
    ivec3 previousBuckets=buckets(prevPixel,size);
    for(int i=0;i<3;++i) {
        int local=previousBuckets[i];
        imageAtomicAdd(stats,statAt(local,visibleCount,id),1);
        imageAtomicAdd(stats,statAt(local,projectedX,id),previous.x);
        imageAtomicAdd(stats,statAt(local,projectedY,id),previous.y);
        imageAtomicMax(stats,statAt(local,projectedMaxX,id),q.x);
        imageAtomicMax(stats,statAt(local,projectedMaxY,id),q.y);
        imageAtomicMax(stats,statAt(local,projectedInvX,id),size.x-1-q.x);
        imageAtomicMax(stats,statAt(local,projectedInvY,id),size.y-1-q.y);
    }
}
''';f.write_text(s)
f=p/'candidates.comp';s=f.read_text().replace('ivec2(field,id)', 'statAt(int(gl_GlobalInvocationID.y),field,id)').replace('ivec2 entry = ivec2(id,0);','ivec2 entry = ivec2(int(gl_GlobalInvocationID.y),id);').replace('int current = now(countField,id), old = then(countField,id), visible = now(visibleCount,id);','int old=then(countField,id), visible=now(visibleCount,id);').replace('min(current,old)<12 || visible < .9*current', 'min(visible,old)<12').replace('float(current)/float(old)','float(visible)/float(old)').replace('now(maxX,id)+now(invX,id),now(maxY,id)+now(invY,id)','now(projectedMaxX,id)+now(projectedInvX,id),now(projectedMaxY,id)+now(projectedInvY,id)')
f.write_text(s)
f=p/'validate.comp';s=f.read_text();a=s.index('    vec4 proposal');s=s[:a]+'''    vec2 base = imageLoad(motionImage,p).rg;
    ivec3 keys=buckets(vec2(p)-pc.jitter+base,size);
    float z = expectedDepth(p,size,texelFetch(sceneDepth,p,0).r);
    vec3 color = tone(texelFetch(sceneColor,p,0).rgb);
    float a = matchCost(lookupPixel(p,base),size,id,color,z);
    for(int i=0;i<3;++i) {
        int bucket=keys[i];
        vec4 proposal=imageLoad(candidates,ivec2(bucket,int(id)));
        if(proposal.z==0.)continue;
        float b=matchCost(lookupPixel(p,base+proposal.xy),size,id,color,z);
        if(b<.09 && a-b>.025)imageAtomicAdd(stats,statAt(bucket,wins,int(id)),1);
        if(a<.09 && b-a>.025)imageAtomicAdd(stats,statAt(bucket,losses,int(id)),1);
    }
}
''';f.write_text(s)
f=p/'apply.comp';s=f.read_text();a=s.index('        vec4 proposal');b=s.index('        reactive =',a)
s=s[:a]+'''        ivec3 keys=buckets(vec2(p)-pc.jitter+base,size);
        float best=cost+.008;
        vec2 bestMotion=base;
        for(int i=0;i<3;++i) {
            int bucket=keys[i];
            vec4 proposal=imageLoad(candidates,ivec2(bucket,int(id)));
            if(proposal.z==0.)continue;
            int support=imageLoad(stats,statAt(bucket,wins,int(id))).r;
            int opposed=imageLoad(stats,statAt(bucket,losses,int(id))).r;
            if(support<4 || support<=2*opposed)continue;
            vec2 motion=base+proposal.xy;
            float error=matchCost(lookupPixel(p,motion),size,id,color,expected);
            float ranked=error+.002*float(i!=0);
            if(error<.09 && ranked<best) {
                best=ranked;
                bestMotion=motion;
                cost=error;
            }
        }
        imageStore(motionImage,p,vec4(bestMotion,0,0));
'''+s[b:];f.write_text(s)
f=p/'material_flow.h';s=f.read_text().replace('width, height, historyIndex;', 'width, height, historyIndex, bucketCount;').replace('g->height=height;', 'g->height=height;\n    g->bucketCount=1+2*((width+192+383)/384)*((height+192+383)/384);').replace('w=12;h=1024;', 'w=16*g->bucketCount;h=1024;').replace('w=1024;h=1;format=VK_FORMAT_R16G16B16A16_SFLOAT;', 'w=g->bucketCount;h=1024;format=VK_FORMAT_R16G16B16A16_SFLOAT;').replace('groupX=16;groupY=1;','groupX=16;groupY=g->bucketCount;');f.write_text(s)
