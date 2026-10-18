XeGTAO, Intel's implementation of GTAO (ground-truth ambient occlusion, Jimenez et al. 2016),
https://github.com/GameTechDev/XeGTAO, master, commit a5b1686c7ea37788eeb3576b5be47f7c03db532c,
MIT licence (LICENSE.txt, Copyright (C) 2016-2021, Intel Corporation). Source/Rendering/Shaders/XeGTAO.h
and XeGTAO.hlsli only, unchanged. Used by twoc/src/ao.c, the "Ambient occlusion" setting (notes 18.95):
CMakeLists.txt embeds both files as byte arrays (build/generated/xegtao_hlsl.h), and ao.c compiles them
with D3DCompile at the first use, with its own entry points (after Source/Rendering/Shaders/vaGTAO.hlsl)
and its own copy of XeGTAO::GTAOUpdateConstants (XeGTAO.h's C++ part) written in C.
