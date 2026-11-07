typedef struct {u64 hash,digest;size_t words;const u32 *code;size_t bytes;} ReflectionModule;
#ifdef REFLECTION_PAIR
#include "reflection-pair.h"
#elif defined(REFLECTION_WITNESS)
#include "reflection-witness.h"
#elif defined(FORWARD_REFLECTIONS)
#include "reflection-production.h"
#endif
extern int arceus_contact_supported;
static VkShaderModule make_reflection_module(const VkSh *shader,const u32 *original,size_t count){
#ifdef FORWARD_REFLECTIONS
    if(arceus_contact_supported && shader->stage==ST_FS && world_reflection_program(shader->hash) &&
       shader->si.ngbuf<16 && shader->si.ntex<=SH_MAXTEX-2){
        u64 digest=14695981039346656037ull;
        for(size_t i=0;i<count;i++)digest=(digest^original[i])*1099511628211ull;
        for(size_t i=0;i<sizeof reflectionModules/sizeof reflectionModules[0];i++){
            const ReflectionModule *r=reflectionModules+i;
            if(r->hash!=shader->hash || r->digest!=digest || r->words!=count)continue;
#ifdef REFLECTION_WITNESS
            if(!g_vk.feat.fragmentStoresAndAtomics)break;
#endif
            VkShaderModule mod=vk_module_spv(r->code,r->bytes/4);
            if(mod){
                LOG_LIMIT(16,"world-pt: forward reflection module %016llx variant %016llx\n",
                    (unsigned long long)r->hash,(unsigned long long)r->digest);
                return mod;
            }
        }
    }
#endif
    return vk_module_spv(original,count);
}
