SMAA, Enhanced Subpixel Morphological Antialiasing (https://github.com/iryoku/smaa, master,
commit 71c806a838bdd7d517df19192a20f0c61b3ca29d), MIT licence (LICENSE.txt). SMAA.hlsl and the
precomputed Textures/AreaTex.h and Textures/SearchTex.h only, unchanged. Used by twoc/src/aa.c, the
"SMAA" anti-aliasing choice (notes 18.61): CMakeLists.txt embeds SMAA.hlsl as a byte array
(build/generated/smaa_hlsl.h) that aa.c compiles with D3DCompile at its first use.
