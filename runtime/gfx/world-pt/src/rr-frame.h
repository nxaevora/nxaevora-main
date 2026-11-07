#ifndef ARCEUS_RR_FRAME_H
#define ARCEUS_RR_FRAME_H
enum {RR_DIFFUSE, RR_SPECULAR, RR_NORMAL_ROUGHNESS, RR_HIT_DISTANCE, RR_GUIDE_COUNT};
typedef struct {
    VkImage image[RR_GUIDE_COUNT];
    VkImageView view[RR_GUIDE_COUNT];
    VkImage beforeImage;
    VkImageView beforeView;
    float worldToView[16], viewToClip[16];
} RrGuides;
bool vk_rr_available(void);
bool vk_rr_create(VkCommandBuffer cb,const VkFsr2Frame *frame);
bool vk_rr_evaluate(VkCommandBuffer cb, const VkFsr2Frame *frame, RrGuides *guides);
#endif
