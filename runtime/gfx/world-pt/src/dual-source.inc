/* The guest stores its second blend color in the second fragment output.
   Vulkan consumes it from location zero, index one. Input locations can use
   the same numbers, so look up the storage class before rewriting decorations. */
static bool blend_source_module(u32 **module,size_t *count){
    u32 *words=*module;
    const u32 bound=words[3];
    if(!bound || bound>1000000)return false;
    u8 *output=calloc(bound,1);
    if(!output)return false;
    for(size_t p=5;p<*count;){
        u32 n=words[p]>>16,op=words[p]&65535;
        if(!n || n>*count-p){free(output);return false;}
        if(op==59 && n>=4 && words[p+3]==3 && words[p+2]<bound)output[words[p+2]]=1;
        p+=n;
    }
    size_t changed=0;
    for(size_t p=5;p<*count;p+=words[p]>>16)
        if(words[p]==((4u<<16)|71) && words[p+1]<bound && output[words[p+1]] &&
           words[p+2]==30 && words[p+3]==1)changed++;
    if(!changed){free(output);return false;}
    size_t size=*count+4*changed;
    u32 *copy=malloc(size*sizeof(u32));
    if(!copy){free(output);return false;}
    memcpy(copy,words,5*sizeof(u32));
    size_t dst=5;
    for(size_t p=5;p<*count;){
        u32 n=words[p]>>16;
        memcpy(copy+dst,words+p,n*sizeof(u32));
        if(words[p]==((4u<<16)|71) && words[p+1]<bound && output[words[p+1]] &&
           words[p+2]==30 && words[p+3]==1){
            copy[dst+3]=0;
            dst+=4;
            copy[dst]=words[p];copy[dst+1]=words[p+1];copy[dst+2]=32;copy[dst+3]=1;
        }
        dst+=n;p+=n;
    }
    free(output);free(words);*module=copy;*count=size;
    return true;
}
