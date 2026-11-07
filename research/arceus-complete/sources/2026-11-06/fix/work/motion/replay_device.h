/* Device setup shared with the vendor motion replay. */
typedef struct { VkPhysicalDevice physical;VkDevice device;VkQueue queue;unsigned family; } ReplayDevice;
static ReplayDevice open_device(bool dl){
const char *ext[160];unsigned n=0;if(dl)vk_dlaa_instance_exts(ext,&n,160);
VkApplicationInfo app={VK_STRUCTURE_TYPE_APPLICATION_INFO,.pApplicationName="ArceusUpscaleOfflineTest",.apiVersion=VK_API_VERSION_1_3};VkInstanceCreateInfo ic={VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,.pApplicationInfo=&app,.enabledExtensionCount=n,.ppEnabledExtensionNames=ext};VkInstance inst;OK(vkCreateInstance(&ic,0,&inst));unsigned count=1;VkPhysicalDevice phys;VkResult rr=vkEnumeratePhysicalDevices(inst,&count,&phys);if(rr<0||!count)exit(2);
VkPhysicalDeviceProperties prop;vkGetPhysicalDeviceProperties(phys,&prop);printf("%s\n",prop.deviceName);
uint32_t nq=0;vkGetPhysicalDeviceQueueFamilyProperties(phys,&nq,0);VkQueueFamilyProperties qp[64];if(nq>64)exit(2);vkGetPhysicalDeviceQueueFamilyProperties(phys,&nq,qp);unsigned fam=0;while(fam<nq && (qp[fam].queueFlags&3)!=3)fam++;if(fam==nq)exit(2);
float one=1;VkDeviceQueueCreateInfo qc={VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,.queueFamilyIndex=fam,.queueCount=1,.pQueuePriorities=&one};vkEnumerateDeviceExtensionProperties(phys,0,&count,0);VkExtensionProperties *exts=calloc(count,sizeof *exts);vkEnumerateDeviceExtensionProperties(phys,0,&count,exts);n=0;if(dl)vk_dlaa_device_exts(exts,count,ext,&n,160);
VkPhysicalDeviceVulkan13Features f13={VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES};VkPhysicalDeviceVulkan12Features f12={VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES,.pNext=&f13};VkPhysicalDeviceVulkan11Features f11={VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES,.pNext=&f12};VkPhysicalDeviceFeatures2 f2={VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2,.pNext=&f11};vkGetPhysicalDeviceFeatures2(phys,&f2);
VkPhysicalDeviceVulkan11Features e11={VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES,.shaderDrawParameters=f11.shaderDrawParameters,.samplerYcbcrConversion=f11.samplerYcbcrConversion};
VkPhysicalDeviceVulkan12Features e12={VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES,.pNext=&e11,.samplerMirrorClampToEdge=f12.samplerMirrorClampToEdge,.samplerFilterMinmax=f12.samplerFilterMinmax,.timelineSemaphore=1,.shaderFloat16=f12.shaderFloat16,.shaderBufferInt64Atomics=f12.shaderBufferInt64Atomics,.shaderOutputViewportIndex=f12.shaderOutputViewportIndex,.shaderOutputLayer=f12.shaderOutputLayer,.shaderSampledImageArrayNonUniformIndexing=f12.shaderSampledImageArrayNonUniformIndexing};
VkPhysicalDeviceVulkan13Features e13={VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES,.pNext=&e12,.synchronization2=1,.dynamicRendering=1,.shaderDemoteToHelperInvocation=1};f2.pNext=&e13;
VkDeviceCreateInfo dc={VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,.pNext=&f2,.queueCreateInfoCount=1,.pQueueCreateInfos=&qc,.enabledExtensionCount=n,.ppEnabledExtensionNames=ext};VkDevice dev;OK(vkCreateDevice(phys,&dc,0,&dev));if(dl){vk_dlaa_init(inst,phys,dev);if(!vk_dlaa_available())exit(3);}
VkQueue q;vkGetDeviceQueue(dev,fam,0,&q);
return (ReplayDevice){phys,dev,q,fam};
}
