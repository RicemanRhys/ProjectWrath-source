/**
 * The Vulkan probe (step 86, notes 18.89). See vk_probe.h.
 *
 * One thread does everything: it makes the instance and device, its window
 * (shown without activation, so it never takes focus from the game's), and
 * then loops: pump the window's messages, wait for the oldest of two frames
 * in flight, acquire, clear the image to a slowly changing colour, submit,
 * present (FIFO, so vsync paces it), and with present_wait wait for the
 * frame before to reach the screen and note when it did (step 90: with
 * present_wait2 where version 1 is missing, as on AMD). Closing the probe's
 * window ends the probe, not the game. A failure anywhere logs why and ends
 * the probe the same way.
 */

#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "volk.h"               /* VK_USE_PLATFORM_WIN32_KHR from CMakeLists.txt */
#include "vk_probe.h"

#define VKP_INFLIGHT 2
#define VKP_MAX_IMAGES 8
#define VKP_GAPS 4096           /* present_wait gaps kept for one 10-s line */

static struct {
    LUID luid;
    int have_luid;
    VkInstance inst;
    VkPhysicalDevice pd;
    VkDevice dev;
    VkQueue q;
    uint32_t qf;
    VkSurfaceKHR surf;
    VkSwapchainKHR sc;
    VkFormat fmt;
    VkColorSpaceKHR cs;
    VkExtent2D ext;
    uint32_t nimg;
    VkImage img[VKP_MAX_IMAGES];
    VkSemaphore done[VKP_MAX_IMAGES];   /* one an image: a present waits on the one its image was drawn with */
    VkCommandPool pool;
    VkCommandBuffer cb[VKP_INFLIGHT];
    VkFence fence[VKP_INFLIGHT];
    VkSemaphore acq[VKP_INFLIGHT];
    int present_wait;                   /* 1: VK_KHR_present_id and VK_KHR_present_wait on; 2 (step 90): their
                                         * version 2 (present_id2, present_wait2: AMD's, notes 18.92); 0 neither */
    int have_caps2;                     /* VK_KHR_get_surface_capabilities2 on (present_wait2 needs it) */
    HWND hwnd;
    volatile LONG w, h, closed;
    LARGE_INTEGER qpf;
} g;

static const char *vk_res(VkResult r)
{
    switch (r) {
    case VK_SUCCESS: return "VK_SUCCESS";
    case VK_NOT_READY: return "VK_NOT_READY";
    case VK_TIMEOUT: return "VK_TIMEOUT";
    case VK_SUBOPTIMAL_KHR: return "VK_SUBOPTIMAL_KHR";
    case VK_ERROR_OUT_OF_HOST_MEMORY: return "VK_ERROR_OUT_OF_HOST_MEMORY";
    case VK_ERROR_OUT_OF_DEVICE_MEMORY: return "VK_ERROR_OUT_OF_DEVICE_MEMORY";
    case VK_ERROR_INITIALIZATION_FAILED: return "VK_ERROR_INITIALIZATION_FAILED";
    case VK_ERROR_DEVICE_LOST: return "VK_ERROR_DEVICE_LOST";
    case VK_ERROR_EXTENSION_NOT_PRESENT: return "VK_ERROR_EXTENSION_NOT_PRESENT";
    case VK_ERROR_FEATURE_NOT_PRESENT: return "VK_ERROR_FEATURE_NOT_PRESENT";
    case VK_ERROR_INCOMPATIBLE_DRIVER: return "VK_ERROR_INCOMPATIBLE_DRIVER";
    case VK_ERROR_SURFACE_LOST_KHR: return "VK_ERROR_SURFACE_LOST_KHR";
    case VK_ERROR_OUT_OF_DATE_KHR: return "VK_ERROR_OUT_OF_DATE_KHR";
    case VK_ERROR_NATIVE_WINDOW_IN_USE_KHR: return "VK_ERROR_NATIVE_WINDOW_IN_USE_KHR";
    default: return "another VkResult";
    }
}

static const char *mode_name(VkPresentModeKHR m)
{
    switch (m) {
    case VK_PRESENT_MODE_IMMEDIATE_KHR: return "immediate";
    case VK_PRESENT_MODE_MAILBOX_KHR: return "mailbox";
    case VK_PRESENT_MODE_FIFO_KHR: return "fifo";
    case VK_PRESENT_MODE_FIFO_RELAXED_KHR: return "fifo_relaxed";
    case VK_PRESENT_MODE_FIFO_LATEST_READY_KHR: return "fifo_latest_ready";
    default: return "other";
    }
}

static const char *type_name(VkPhysicalDeviceType t)
{
    switch (t) {
    case VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU: return "integrated";
    case VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU: return "discrete";
    case VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU: return "virtual";
    case VK_PHYSICAL_DEVICE_TYPE_CPU: return "CPU";
    default: return "other";
    }
}

static double now_ms(void)
{
    LARGE_INTEGER q;
    QueryPerformanceCounter(&q);
    return (double)q.QuadPart * 1000.0 / (double)g.qpf.QuadPart;
}

static int has_ext(const VkExtensionProperties *e, uint32_t n, const char *name)
{
    uint32_t i;
    for (i = 0; i < n; i++)
        if (strcmp(e[i].extensionName, name) == 0)
            return 1;
    return 0;
}

/* ── The window ────────────────────────────────────────────── */

static LRESULT CALLBACK wnd_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_SIZE:
        if (wp != SIZE_MINIMIZED) {
            InterlockedExchange(&g.w, (LONG)LOWORD(lp));
            InterlockedExchange(&g.h, (LONG)HIWORD(lp));
        }
        return 0;
    case WM_CLOSE:                      /* the probe ends; the game goes on */
        InterlockedExchange(&g.closed, 1);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

static int make_window(void)
{
    WNDCLASSW wc;
    RECT r = { 0, 0, 640, 360 };

    memset(&wc, 0, sizeof(wc));
    wc.lpfnWndProc = wnd_proc;
    wc.hInstance = GetModuleHandleW(NULL);
    wc.hCursor = LoadCursorW(NULL, (LPCWSTR)IDC_ARROW);
    wc.lpszClassName = L"TwocVulkanProbe";
    RegisterClassW(&wc);
    AdjustWindowRect(&r, WS_OVERLAPPEDWINDOW, FALSE);
    g.hwnd = CreateWindowExW(0, wc.lpszClassName, L"Crash TWOC - Vulkan probe (RECOMP_VULKAN=probe)",
                             WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, r.right - r.left, r.bottom - r.top,
                             NULL, NULL, wc.hInstance, NULL);
    if (!g.hwnd)
        return -1;
    GetClientRect(g.hwnd, &r);
    g.w = r.right - r.left;
    g.h = r.bottom - r.top;
    ShowWindow(g.hwnd, SW_SHOWNOACTIVATE);
    return 0;
}

static void pump(void)
{
    MSG m;
    while (PeekMessageW(&m, NULL, 0, 0, PM_REMOVE)) {
        TranslateMessage(&m);
        DispatchMessageW(&m);
    }
}

/* ── Instance, device ──────────────────────────────────────── */

static int make_instance(void)
{
    static const char *want[] = { VK_KHR_SURFACE_EXTENSION_NAME, VK_KHR_WIN32_SURFACE_EXTENSION_NAME,
                                  VK_KHR_GET_SURFACE_CAPABILITIES_2_EXTENSION_NAME };
    VkExtensionProperties *ie = NULL;
    const char *on[3];
    uint32_t n = 0, ver = VK_API_VERSION_1_0, i, non = 0;
    VkApplicationInfo ai;
    VkInstanceCreateInfo ci;
    VkResult r;

    if (vkEnumerateInstanceVersion)
        vkEnumerateInstanceVersion(&ver);
    vkEnumerateInstanceExtensionProperties(NULL, &n, NULL);
    ie = (VkExtensionProperties *)calloc(n ? n : 1, sizeof(*ie));
    vkEnumerateInstanceExtensionProperties(NULL, &n, ie);
    for (i = 0; i < 3; i++)
        if (has_ext(ie, n, want[i])) {
            on[non++] = want[i];
            g.have_caps2 |= i == 2;
        } else if (i < 2) {
            fprintf(stderr, "[VK] probe: the loader has no %s; probe off\n", want[i]);
            free(ie);
            return -1;
        }
    free(ie);
    fprintf(stderr, "[VK] probe: the loader offers Vulkan %u.%u.%u, %u instance extensions\n",
            VK_API_VERSION_MAJOR(ver), VK_API_VERSION_MINOR(ver), VK_API_VERSION_PATCH(ver), n);
    if (ver < VK_API_VERSION_1_1) {
        fprintf(stderr, "[VK] probe: Vulkan 1.1 or later needed; probe off\n");
        return -1;
    }
    memset(&ai, 0, sizeof(ai));
    ai.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    ai.pApplicationName = "Crash TWOC (Vulkan probe)";
    ai.pEngineName = "twoc";
    ai.apiVersion = ver >= VK_API_VERSION_1_3 ? VK_API_VERSION_1_3 : VK_API_VERSION_1_1;
    memset(&ci, 0, sizeof(ci));
    ci.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    ci.pApplicationInfo = &ai;
    ci.enabledExtensionCount = non;
    ci.ppEnabledExtensionNames = on;
    r = vkCreateInstance(&ci, NULL, &g.inst);
    if (r != VK_SUCCESS) {
        fprintf(stderr, "[VK] probe: vkCreateInstance failed: %s (%d); probe off\n", vk_res(r), (int)r);
        return -1;
    }
    volkLoadInstance(g.inst);
    return 0;
}

/* The extensions a native backend would look for, and whether this GPU has them. */
static void log_exts(const VkExtensionProperties *e, uint32_t n)
{
    static const char *names[] = {
        "VK_KHR_swapchain", "VK_KHR_present_id", "VK_KHR_present_wait", "VK_KHR_present_id2",
        "VK_KHR_present_wait2", "VK_EXT_present_timing", "VK_GOOGLE_display_timing",
        "VK_KHR_swapchain_maintenance1", "VK_EXT_swapchain_maintenance1", "VK_EXT_full_screen_exclusive",
        "VK_KHR_dynamic_rendering", "VK_KHR_synchronization2", "VK_KHR_timeline_semaphore",
        "VK_KHR_push_descriptor", "VK_EXT_descriptor_buffer", "VK_EXT_descriptor_indexing",
        "VK_EXT_extended_dynamic_state3", "VK_EXT_graphics_pipeline_library", "VK_EXT_shader_object",
        "VK_EXT_vertex_input_dynamic_state", "VK_EXT_robustness2", "VK_EXT_custom_border_color",
        "VK_EXT_depth_clip_enable", "VK_KHR_maintenance5", "VK_KHR_external_memory_win32",
        "VK_KHR_external_semaphore_win32", "VK_EXT_memory_budget", "VK_EXT_calibrated_timestamps",
    };
    char have[1024], miss[1024];
    size_t i, lh = 0, lm = 0;

    have[0] = miss[0] = 0;
    for (i = 0; i < sizeof(names) / sizeof(names[0]); i++) {
        int y = has_ext(e, n, names[i]);
        char *b = y ? have : miss;
        size_t *l = y ? &lh : &lm;
        int k = snprintf(b + *l, sizeof(have) - *l, "%s%s", *l ? " " : "", names[i] + 3);
        if (k > 0 && *l + (size_t)k < sizeof(have))
            *l += (size_t)k;
    }
    fprintf(stderr, "[VK] probe:   %u device extensions; has %s\n", n, have);
    fprintf(stderr, "[VK] probe:   lacks %s\n", miss[0] ? miss : "none of those");
}

static int pick_device(void)
{
    VkPhysicalDevice pds[16];
    uint32_t n = 16, i, best = UINT32_MAX, discrete = UINT32_MAX;
    VkResult r = vkEnumeratePhysicalDevices(g.inst, &n, pds);

    if ((r != VK_SUCCESS && r != VK_INCOMPLETE) || n == 0) {
        fprintf(stderr, "[VK] probe: no Vulkan GPU (%s); probe off\n", vk_res(r));
        return -1;
    }
    for (i = 0; i < n; i++) {
        VkPhysicalDeviceIDProperties id;
        VkPhysicalDeviceDriverProperties dr;
        VkPhysicalDeviceProperties2 p;
        int match;

        memset(&id, 0, sizeof(id));
        memset(&dr, 0, sizeof(dr));
        memset(&p, 0, sizeof(p));
        id.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ID_PROPERTIES;
        dr.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DRIVER_PROPERTIES;
        id.pNext = &dr;
        p.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
        p.pNext = &id;
        vkGetPhysicalDeviceProperties2(pds[i], &p);
        match = g.have_luid && id.deviceLUIDValid && memcmp(id.deviceLUID, &g.luid, sizeof(LUID)) == 0;
        fprintf(stderr, "[VK] probe: GPU %u: %s (%s), Vulkan %u.%u.%u, driver %s %s (0x%08X), vendor 0x%04X device"
                " 0x%04X%s\n", i, p.properties.deviceName, type_name(p.properties.deviceType),
                VK_API_VERSION_MAJOR(p.properties.apiVersion), VK_API_VERSION_MINOR(p.properties.apiVersion),
                VK_API_VERSION_PATCH(p.properties.apiVersion), dr.driverName[0] ? dr.driverName : "?",
                dr.driverInfo, p.properties.driverVersion, p.properties.vendorID, p.properties.deviceID,
                match ? ": the D3D11 device's (LUID)" : "");
        if (match && best == UINT32_MAX)
            best = i;
        if (p.properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU && discrete == UINT32_MAX)
            discrete = i;
    }
    if (best == UINT32_MAX)
        best = discrete != UINT32_MAX ? discrete : 0;
    g.pd = pds[best];
    fprintf(stderr, "[VK] probe: using GPU %u%s\n", best, g.have_luid ? "" : " (the D3D11 device's LUID not known)");
    return 0;
}

static int make_device(void)
{
    VkExtensionProperties *de = NULL;
    VkQueueFamilyProperties qp[16];
    uint32_t n = 0, nq = 16, i;
    const char *on[3];
    uint32_t non = 0;
    float prio = 1.0f;
    VkDeviceQueueCreateInfo qi;
    VkDeviceCreateInfo ci;
    VkPhysicalDevicePresentIdFeaturesKHR pid;
    VkPhysicalDevicePresentWaitFeaturesKHR pw;
    VkPhysicalDevicePresentId2FeaturesKHR pid2;
    VkPhysicalDevicePresentWait2FeaturesKHR pw2;
    VkPhysicalDeviceFeatures2 f2;
    VkResult r;
    const char *why2 = "";

    vkEnumerateDeviceExtensionProperties(g.pd, NULL, &n, NULL);
    de = (VkExtensionProperties *)calloc(n ? n : 1, sizeof(*de));
    vkEnumerateDeviceExtensionProperties(g.pd, NULL, &n, de);
    log_exts(de, n);
    if (!has_ext(de, n, VK_KHR_SWAPCHAIN_EXTENSION_NAME)) {
        fprintf(stderr, "[VK] probe: no VK_KHR_swapchain; probe off\n");
        free(de);
        return -1;
    }
    on[non++] = VK_KHR_SWAPCHAIN_EXTENSION_NAME;
    memset(&pid, 0, sizeof(pid));
    memset(&pw, 0, sizeof(pw));
    memset(&f2, 0, sizeof(f2));
    pid.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PRESENT_ID_FEATURES_KHR;
    pw.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PRESENT_WAIT_FEATURES_KHR;
    f2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
    if (has_ext(de, n, VK_KHR_PRESENT_ID_EXTENSION_NAME) && has_ext(de, n, VK_KHR_PRESENT_WAIT_EXTENSION_NAME) &&
        !(getenv("RECOMP_VULKAN_PRESENT_WAIT") && atoi(getenv("RECOMP_VULKAN_PRESENT_WAIT")) == 2)) {
        /* (RECOMP_VULKAN_PRESENT_WAIT=2, a test aid: version 2 even where version 1 is) */
        pid.pNext = &pw;
        f2.pNext = &pid;
        vkGetPhysicalDeviceFeatures2(g.pd, &f2);
        if (pid.presentId && pw.presentWait) {
            on[non++] = VK_KHR_PRESENT_ID_EXTENSION_NAME;
            on[non++] = VK_KHR_PRESENT_WAIT_EXTENSION_NAME;
            g.present_wait = 1;
        }
    }
    if (!g.present_wait && has_ext(de, n, VK_KHR_PRESENT_ID_2_EXTENSION_NAME) &&
        has_ext(de, n, VK_KHR_PRESENT_WAIT_2_EXTENSION_NAME)) {
        /* Step 90: version 1 missing (AMD has only version 2): present_id2 and present_wait2, which the surface
         * must support too (vkGetPhysicalDeviceSurfaceCapabilities2KHR) and the swap chain asks for by flags. */
        VkPhysicalDeviceSurfaceInfo2KHR sinfo;
        VkSurfaceCapabilities2KHR caps2;
        VkSurfaceCapabilitiesPresentId2KHR sid2;
        VkSurfaceCapabilitiesPresentWait2KHR sw2;
        memset(&pid2, 0, sizeof(pid2));
        memset(&pw2, 0, sizeof(pw2));
        memset(&f2, 0, sizeof(f2));
        pid2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PRESENT_ID_2_FEATURES_KHR;
        pw2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PRESENT_WAIT_2_FEATURES_KHR;
        f2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
        pid2.pNext = &pw2;
        f2.pNext = &pid2;
        vkGetPhysicalDeviceFeatures2(g.pd, &f2);
        memset(&sinfo, 0, sizeof(sinfo));
        memset(&caps2, 0, sizeof(caps2));
        memset(&sid2, 0, sizeof(sid2));
        memset(&sw2, 0, sizeof(sw2));
        sinfo.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SURFACE_INFO_2_KHR;
        sinfo.surface = g.surf;
        caps2.sType = VK_STRUCTURE_TYPE_SURFACE_CAPABILITIES_2_KHR;
        sid2.sType = VK_STRUCTURE_TYPE_SURFACE_CAPABILITIES_PRESENT_ID_2_KHR;
        sw2.sType = VK_STRUCTURE_TYPE_SURFACE_CAPABILITIES_PRESENT_WAIT_2_KHR;
        caps2.pNext = &sid2;
        sid2.pNext = &sw2;
        if (!pid2.presentId2 || !pw2.presentWait2)
            why2 = " (present_id2 / present_wait2 listed, their features off)";
        else if (!g.have_caps2 || !vkGetPhysicalDeviceSurfaceCapabilities2KHR ||
                 vkGetPhysicalDeviceSurfaceCapabilities2KHR(g.pd, &sinfo, &caps2) != VK_SUCCESS)
            why2 = " (present_id2 / present_wait2: the surface's support can't be asked)";
        else if (!sid2.presentId2Supported || !sw2.presentWait2Supported)
            why2 = " (present_id2 / present_wait2: not for this window's surface)";
        else {
            on[non++] = VK_KHR_PRESENT_ID_2_EXTENSION_NAME;
            on[non++] = VK_KHR_PRESENT_WAIT_2_EXTENSION_NAME;
            g.present_wait = 2;
            pid2.pNext = &pw2;
            pw2.pNext = NULL;
            f2.pNext = &pid2;
        }
    }
    free(de);
    vkGetPhysicalDeviceQueueFamilyProperties(g.pd, &nq, qp);
    g.qf = UINT32_MAX;
    for (i = 0; i < nq; i++) {
        VkBool32 pres = VK_FALSE;
        vkGetPhysicalDeviceSurfaceSupportKHR(g.pd, i, g.surf, &pres);
        if ((qp[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) && pres) {
            g.qf = i;
            break;
        }
    }
    if (g.qf == UINT32_MAX) {
        fprintf(stderr, "[VK] probe: no queue family draws and presents to the window; probe off\n");
        return -1;
    }
    memset(&qi, 0, sizeof(qi));
    qi.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    qi.queueFamilyIndex = g.qf;
    qi.queueCount = 1;
    qi.pQueuePriorities = &prio;
    memset(&ci, 0, sizeof(ci));
    ci.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    ci.pNext = g.present_wait ? &f2 : NULL;    /* f2 chains present_id(2) and present_wait(2), on as queried */
    ci.queueCreateInfoCount = 1;
    ci.pQueueCreateInfos = &qi;
    ci.enabledExtensionCount = non;
    ci.ppEnabledExtensionNames = on;
    r = vkCreateDevice(g.pd, &ci, NULL, &g.dev);
    if (r != VK_SUCCESS) {
        fprintf(stderr, "[VK] probe: vkCreateDevice failed: %s (%d); probe off\n", vk_res(r), (int)r);
        return -1;
    }
    volkLoadDevice(g.dev);
    vkGetDeviceQueue(g.dev, g.qf, 0, &g.q);
    fprintf(stderr, "[VK] probe: device made: queue family %u of %u (%u queues), %s%s\n", g.qf, nq,
            qp[g.qf].queueCount, g.present_wait == 1 ? "present_id + present_wait on"
                                 : g.present_wait == 2 ? "present_id2 + present_wait2 on (version 1 not here)"
                                                       : "present_id + present_wait not available", why2);
    return 0;
}

/* ── Swap chain ────────────────────────────────────────────── */

static int make_swapchain(int first)
{
    VkSurfaceCapabilitiesKHR caps;
    VkSurfaceFormatKHR fm[64];
    VkPresentModeKHR pm[16];
    uint32_t nf = 64, np = 16, i;
    VkSwapchainCreateInfoKHR ci;
    VkSwapchainKHR old = g.sc;
    VkResult r;

    if (vkGetPhysicalDeviceSurfaceCapabilitiesKHR(g.pd, g.surf, &caps) != VK_SUCCESS)
        return -1;
    vkGetPhysicalDeviceSurfaceFormatsKHR(g.pd, g.surf, &nf, fm);
    vkGetPhysicalDeviceSurfacePresentModesKHR(g.pd, g.surf, &np, pm);
    if (first) {
        char modes[160] = "";
        size_t l = 0;
        for (i = 0; i < np; i++) {
            int k = snprintf(modes + l, sizeof(modes) - l, "%s%s", i ? ", " : "", mode_name(pm[i]));
            if (k > 0 && l + (size_t)k < sizeof(modes))
                l += (size_t)k;
        }
        fprintf(stderr, "[VK] probe: surface: %u formats, present modes %s; images %u-%u, usage 0x%X\n", nf, modes,
                caps.minImageCount, caps.maxImageCount, (unsigned)caps.supportedUsageFlags);
    }
    if (!(caps.supportedUsageFlags & VK_IMAGE_USAGE_TRANSFER_DST_BIT)) {
        fprintf(stderr, "[VK] probe: the swap chain's images can't be cleared by a transfer; probe off\n");
        return -1;
    }
    g.fmt = fm[0].format;
    g.cs = fm[0].colorSpace;
    for (i = 0; i < nf; i++)
        if (fm[i].format == VK_FORMAT_B8G8R8A8_UNORM && fm[i].colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
            g.fmt = fm[i].format;
            g.cs = fm[i].colorSpace;
        }
    g.ext = caps.currentExtent;
    if (g.ext.width == 0xFFFFFFFFu) {
        g.ext.width = (uint32_t)g.w;
        g.ext.height = (uint32_t)g.h;
    }
    if (g.ext.width == 0 || g.ext.height == 0)
        return 1;                       /* minimised: try again later */
    memset(&ci, 0, sizeof(ci));
    ci.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    ci.surface = g.surf;
    ci.minImageCount = caps.minImageCount < 3 ? 3 : caps.minImageCount;
    if (caps.maxImageCount && ci.minImageCount > caps.maxImageCount)
        ci.minImageCount = caps.maxImageCount;
    ci.imageFormat = g.fmt;
    ci.imageColorSpace = g.cs;
    ci.imageExtent = g.ext;
    ci.imageArrayLayers = 1;
    ci.imageUsage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    ci.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    ci.preTransform = caps.currentTransform;
    ci.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    ci.presentMode = VK_PRESENT_MODE_FIFO_KHR;  /* vsync: always there */
    ci.clipped = VK_TRUE;
    ci.oldSwapchain = old;
    if (g.present_wait == 2)            /* step 90: version 2 is asked for by the swap chain */
        ci.flags = VK_SWAPCHAIN_CREATE_PRESENT_ID_2_BIT_KHR | VK_SWAPCHAIN_CREATE_PRESENT_WAIT_2_BIT_KHR;
    r = vkCreateSwapchainKHR(g.dev, &ci, NULL, &g.sc);
    if (old)
        vkDestroySwapchainKHR(g.dev, old, NULL);
    if (r != VK_SUCCESS) {
        g.sc = VK_NULL_HANDLE;
        fprintf(stderr, "[VK] probe: vkCreateSwapchainKHR failed: %s (%d); probe off\n", vk_res(r), (int)r);
        return -1;
    }
    for (i = 0; i < g.nimg; i++)
        vkDestroySemaphore(g.dev, g.done[i], NULL);
    g.nimg = VKP_MAX_IMAGES;
    vkGetSwapchainImagesKHR(g.dev, g.sc, &g.nimg, g.img);
    for (i = 0; i < g.nimg; i++) {
        VkSemaphoreCreateInfo si;
        memset(&si, 0, sizeof(si));
        si.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
        vkCreateSemaphore(g.dev, &si, NULL, &g.done[i]);
    }
    fprintf(stderr, "[VK] probe: swap chain %ux%u, %u images, format %d colour space %d, fifo%s\n", g.ext.width,
            g.ext.height, g.nimg, (int)g.fmt, (int)g.cs, g.present_wait == 2 ? ", present id 2 and wait 2" : "");
    return 0;
}

static int make_frames(void)
{
    VkCommandPoolCreateInfo pc;
    VkCommandBufferAllocateInfo ai;
    int i;

    memset(&pc, 0, sizeof(pc));
    pc.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    pc.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    pc.queueFamilyIndex = g.qf;
    if (vkCreateCommandPool(g.dev, &pc, NULL, &g.pool) != VK_SUCCESS)
        return -1;
    memset(&ai, 0, sizeof(ai));
    ai.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    ai.commandPool = g.pool;
    ai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    ai.commandBufferCount = VKP_INFLIGHT;
    if (vkAllocateCommandBuffers(g.dev, &ai, g.cb) != VK_SUCCESS)
        return -1;
    for (i = 0; i < VKP_INFLIGHT; i++) {
        VkFenceCreateInfo fi;
        VkSemaphoreCreateInfo si;
        memset(&fi, 0, sizeof(fi));
        fi.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
        fi.flags = VK_FENCE_CREATE_SIGNALED_BIT;
        memset(&si, 0, sizeof(si));
        si.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
        if (vkCreateFence(g.dev, &fi, NULL, &g.fence[i]) != VK_SUCCESS ||
            vkCreateSemaphore(g.dev, &si, NULL, &g.acq[i]) != VK_SUCCESS)
            return -1;
    }
    return 0;
}

/* ── The loop ──────────────────────────────────────────────── */

static void barrier(VkCommandBuffer cb, VkImage img, VkImageLayout from, VkImageLayout to, VkAccessFlags src,
                    VkAccessFlags dst, VkPipelineStageFlags s0, VkPipelineStageFlags s1)
{
    VkImageMemoryBarrier b;
    memset(&b, 0, sizeof(b));
    b.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    b.srcAccessMask = src;
    b.dstAccessMask = dst;
    b.oldLayout = from;
    b.newLayout = to;
    b.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b.image = img;
    b.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    b.subresourceRange.levelCount = 1;
    b.subresourceRange.layerCount = 1;
    vkCmdPipelineBarrier(cb, s0, s1, 0, 0, NULL, 0, NULL, 1, &b);
}

static int cmp_d(const void *a, const void *b)
{
    double x = *(const double *)a, y = *(const double *)b;
    return x < y ? -1 : x > y;
}

/* One 10-s line: frames presented, and the gaps between frames reaching the screen. */
static void interval_line(double t_s, unsigned frames, double *gaps, unsigned ng, unsigned late)
{
    if (!ng) {
        fprintf(stderr, "[VK] probe +%.0f s: %u frames presented (%.1f/s)%s\n", t_s, frames, frames / 10.0,
                g.present_wait ? "; no present_wait gap measured" : "");
        return;
    }
    {
        double med, mean = 0, lo, hi;
        unsigned i, close_n = 0;
        qsort(gaps, ng, sizeof(*gaps), cmp_d);
        med = gaps[ng / 2];
        lo = gaps[0];
        hi = gaps[ng - 1];
        for (i = 0; i < ng; i++) {
            mean += gaps[i];
            close_n += fabs(gaps[i] - med) < 0.5;
        }
        mean /= ng;
        fprintf(stderr, "[VK] probe +%.0f s: %u frames presented (%.1f/s); on screen %.3f ms apart (median; mean"
                " %.3f, %.2f-%.2f), %.1f%% within 0.5 ms of the median; present_wait timeouts %u\n", t_s, frames,
                frames / 10.0, med, mean, lo, hi, 100.0 * close_n / ng, late);
    }
}

static DWORD WINAPI probe_main(LPVOID p)
{
    static double gaps[VKP_GAPS];
    unsigned frame = 0, ng = 0, frames = 0, late = 0;
    uint64_t pid = 0;
    double t0, t_line, last_shown = 0;
    VkWin32SurfaceCreateInfoKHR si;
    VkResult r;
    int resize = 0;

    (void)p;
    QueryPerformanceFrequency(&g.qpf);
    if (volkInitialize() != VK_SUCCESS) {
        fprintf(stderr, "[VK] probe: no Vulkan loader (vulkan-1.dll); probe off\n");
        return 0;
    }
    if (make_instance() != 0 || pick_device() != 0 || make_window() != 0)
        return 0;
    memset(&si, 0, sizeof(si));
    si.sType = VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR;
    si.hinstance = GetModuleHandleW(NULL);
    si.hwnd = g.hwnd;
    r = vkCreateWin32SurfaceKHR(g.inst, &si, NULL, &g.surf);
    if (r != VK_SUCCESS) {
        fprintf(stderr, "[VK] probe: vkCreateWin32SurfaceKHR failed: %s (%d); probe off\n", vk_res(r), (int)r);
        return 0;
    }
    if (make_device() != 0 || make_swapchain(1) < 0 || make_frames() != 0) {
        fprintf(stderr, "[VK] probe: off\n");
        return 0;
    }
    t0 = t_line = now_ms();
    while (!g.closed) {
        uint32_t img = 0, k = frame % VKP_INFLIGHT;
        VkCommandBufferBeginInfo bi;
        VkClearColorValue col;
        VkImageSubresourceRange rg;
        VkSubmitInfo sub;
        VkPipelineStageFlags ws = VK_PIPELINE_STAGE_TRANSFER_BIT;
        VkPresentInfoKHR pi;
        VkPresentIdKHR pidi;
        VkPresentId2KHR pidi2;
        double hue, now;

        pump();
        if (resize || (uint32_t)g.w != g.ext.width || (uint32_t)g.h != g.ext.height || !g.sc) {
            int m;
            vkDeviceWaitIdle(g.dev);
            m = make_swapchain(0);
            if (m < 0)
                break;
            if (m > 0) {                /* minimised */
                Sleep(50);
                continue;
            }
            resize = 0;
        }
        vkWaitForFences(g.dev, 1, &g.fence[k], VK_TRUE, UINT64_MAX);
        r = vkAcquireNextImageKHR(g.dev, g.sc, UINT64_MAX, g.acq[k], VK_NULL_HANDLE, &img);
        if (r == VK_ERROR_OUT_OF_DATE_KHR) {
            resize = 1;
            continue;
        }
        if (r != VK_SUCCESS && r != VK_SUBOPTIMAL_KHR) {
            fprintf(stderr, "[VK] probe: vkAcquireNextImageKHR failed: %s (%d); probe off\n", vk_res(r), (int)r);
            break;
        }
        vkResetFences(g.dev, 1, &g.fence[k]);
        vkResetCommandBuffer(g.cb[k], 0);
        memset(&bi, 0, sizeof(bi));
        bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        vkBeginCommandBuffer(g.cb[k], &bi);
        barrier(g.cb[k], g.img[img], VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 0,
                VK_ACCESS_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);
        hue = fmod(frame / 240.0, 1.0) * 6.2831853;   /* once round in 240 frames: a second at 240 Hz */
        col.float32[0] = 0.25f + 0.2f * (float)sin(hue);
        col.float32[1] = 0.25f + 0.2f * (float)sin(hue + 2.094);
        col.float32[2] = 0.25f + 0.2f * (float)sin(hue + 4.189);
        col.float32[3] = 1.0f;
        memset(&rg, 0, sizeof(rg));
        rg.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        rg.levelCount = 1;
        rg.layerCount = 1;
        vkCmdClearColorImage(g.cb[k], g.img[img], VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &col, 1, &rg);
        barrier(g.cb[k], g.img[img], VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
                VK_ACCESS_TRANSFER_WRITE_BIT, 0, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT);
        vkEndCommandBuffer(g.cb[k]);
        memset(&sub, 0, sizeof(sub));
        sub.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        sub.waitSemaphoreCount = 1;
        sub.pWaitSemaphores = &g.acq[k];
        sub.pWaitDstStageMask = &ws;
        sub.commandBufferCount = 1;
        sub.pCommandBuffers = &g.cb[k];
        sub.signalSemaphoreCount = 1;
        sub.pSignalSemaphores = &g.done[img];
        r = vkQueueSubmit(g.q, 1, &sub, g.fence[k]);
        if (r != VK_SUCCESS) {
            fprintf(stderr, "[VK] probe: vkQueueSubmit failed: %s (%d); probe off\n", vk_res(r), (int)r);
            break;
        }
        memset(&pi, 0, sizeof(pi));
        pi.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
        pi.waitSemaphoreCount = 1;
        pi.pWaitSemaphores = &g.done[img];
        pi.swapchainCount = 1;
        pi.pSwapchains = &g.sc;
        pi.pImageIndices = &img;
        if (g.present_wait == 1) {
            memset(&pidi, 0, sizeof(pidi));
            pidi.sType = VK_STRUCTURE_TYPE_PRESENT_ID_KHR;
            pidi.swapchainCount = 1;
            pidi.pPresentIds = &pid;
            pid++;
            pi.pNext = &pidi;
        } else if (g.present_wait == 2) {   /* step 90 */
            memset(&pidi2, 0, sizeof(pidi2));
            pidi2.sType = VK_STRUCTURE_TYPE_PRESENT_ID_2_KHR;
            pidi2.swapchainCount = 1;
            pidi2.pPresentIds = &pid;
            pid++;
            pi.pNext = &pidi2;
        }
        r = vkQueuePresentKHR(g.q, &pi);
        if (r == VK_ERROR_OUT_OF_DATE_KHR || r == VK_SUBOPTIMAL_KHR)
            resize = 1;
        else if (r != VK_SUCCESS) {
            fprintf(stderr, "[VK] probe: vkQueuePresentKHR failed: %s (%d); probe off\n", vk_res(r), (int)r);
            break;
        }
        frame++;
        frames++;
        if (g.present_wait && pid > 1) {    /* the frame before on screen: when */
            if (g.present_wait == 2) {
                VkPresentWait2InfoKHR wi;
                memset(&wi, 0, sizeof(wi));
                wi.sType = VK_STRUCTURE_TYPE_PRESENT_WAIT_2_INFO_KHR;
                wi.presentId = pid - 1;
                wi.timeout = 100000000ull;
                r = vkWaitForPresent2KHR(g.dev, g.sc, &wi);
            } else
                r = vkWaitForPresentKHR(g.dev, g.sc, pid - 1, 100000000ull);
            if (r == VK_SUCCESS) {
                double t = now_ms();
                if (last_shown > 0 && ng < VKP_GAPS)
                    gaps[ng++] = t - last_shown;
                last_shown = t;
            } else {
                late++;
                last_shown = 0;
            }
        }
        now = now_ms();
        if (now - t_line >= 10000.0) {
            interval_line((now - t0) / 1000.0, frames, gaps, ng, late);
            t_line = now;
            frames = ng = late = 0;
        }
    }
    vkDeviceWaitIdle(g.dev);
    fprintf(stderr, "[VK] probe: ended (%s) after %u frames\n", g.closed ? "its window closed" : "an error", frame);
    if (g.hwnd)
        DestroyWindow(g.hwnd);
    return 0;
}

void vkp_start(const LUID *d3d_luid)
{
    const char *e = getenv("RECOMP_VULKAN");
    HANDLE th;

    if (!e || _stricmp(e, "probe") != 0)
        return;
    if (d3d_luid) {
        g.luid = *d3d_luid;
        g.have_luid = 1;
    }
    fprintf(stderr, "[VK] probe on (RECOMP_VULKAN=probe): a Vulkan device and a window of its own presenting"
            " cleared frames; the game's window and D3D11 are unchanged\n");
    th = CreateThread(NULL, 0, probe_main, NULL, 0, NULL);
    if (th)
        CloseHandle(th);
    else
        fprintf(stderr, "[VK] probe: thread not created (%lu)\n", GetLastError());
}
