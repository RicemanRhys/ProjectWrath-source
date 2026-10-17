volk 1.4.350 (https://github.com/zeux/volk, tag 1.4.350), MIT licence (LICENSE.md). volk.c and volk.h,
unchanged: the Vulkan loader (vulkan-1.dll) is loaded at run time and its entry points fetched, so
the exe imports nothing for Vulkan. Used by twoc/src/vk_probe.c (notes 18.89).
