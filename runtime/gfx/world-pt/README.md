# Arceus Vulkan world path tracing

This opt-in renderer traces the opaque world at 1920×1080 and reconstructs a 3840×2160 scene before the game draws its HUD. Standard DLSS Super Resolution and FSR 3.1.6 share the Vulkan temporal denoiser. DLSS Ray Reconstruction is a separate launch option.

## Build on Windows

Use the [ScarletNX Windows build instructions](../../../docs/SCARLETNX_BUILD-NOTES.md) and a short build path without spaces. The preparer copies the vendored ScarletNX tree, applies the runtime changes in that copy, and installs the renderer sources. It requires Git and Python 3.9 or later. Configure with the Vulkan FidelityFX API headers for FSR 3.1.6 and the glslang headers containing `glslang/SPIRV/spirv.hpp11`. CMake also runs the glslang executable to compile the authored shader programs.

From the repository root in a configured CLANG64 environment:

```powershell
python runtime/gfx/world-pt/prepare.py C:/build/arceus-pt-src
cmake -S C:/build/arceus-pt-src -B C:/build/arceus-pt -G Ninja -DWORLD_PT_FFX_INCLUDE=C:/sdk/ffx_api -DWORLD_PT_GLSLANG_INCLUDE=C:/sdk/glslang/include
cmake --build C:/build/arceus-pt --target scarletnx
```

Use the prepared runtime when generating or relinking your local game executable. The project's [build instructions](../../../docs/SCARLETNX_BUILD-NOTES.md) describe the recompiler toolchain. Place the appropriate vendor runtime libraries next to the executable. FSR needs `amd_fidelityfx_loader_vk.dll` and `amd_fidelityfx_upscaler_vk.dll`. DLSS needs an NGX-capable driver and the matching DLSS libraries. Compilation and shader generation need no game assets.

```powershell
./runtime/gfx/world-pt/Start-WorldPT.ps1 -Executable C:/games/build/ArceusTest.exe -Game C:/games/ArceusTest -Terrain C:/games/local-terrain
```

Open the in-game menu with F1, then Display > Graphics. The DLSS Super Resolution and Ray tracing switches are saved in the game's INI file as `arceus_dlss` and `arceus_ray_tracing`. Both default to Off. Change either switch in the menu, close the game normally, and start it again to apply the saved choice. The menu marks these settings as requiring a restart. Ray tracing uses FSR when DLSS is off. The launcher reads the saved settings by default and clears inherited graphics overrides. Optional launch flags remain available for a one-run override. The launcher uses native controller input, game-controlled frame pacing, a 120 FPS target, and camera multiplier 2.2. The terrain directory is optional. Camera-visible geometry is captured from guest draws. Additional local terrain, structure and instance tables provide offscreen geometry and materials.

## Scene and lighting

The renderer captures triangle positions and UVs, reconstructs the active camera and builds acceleration structures for ray queries. It combines live geometry with verified archive placements, rejects duplicated live instances, evaluates alpha cutouts, samples emissive triangles and the environment, and traces shadow, diffuse and GGX reflection rays. Native surface frames and normal maps guide secondary shading. At low descriptor limits, a small GPU texture cache provides overflow albedo samples. Cached guest shaders are isolated by the shader translator, renderer inputs and build flags.

The finite-depth integrator still uses guest diffuse irradiance. Most transparency still uses guest shading. Offscreen dynamic characters and unobserved placements have incomplete coverage.

## Local asset and validation tools

The `assets` directory contains the terrain, material and placement converters. Set `ARCEUS_ROMFS_BIN` to the local extracted RomFS `bin` directory and `ARCEUS_OOZ_LIBRARY` to a locally available library exporting `ooz_decompress`. On Windows, set `ARCEUS_OOZ_DEPENDENCY_DIR` when that library loads runtime DLLs from a separate directory. The Python converters use NumPy and Pillow. Set `ARCEUS_TERRAIN_DIR` before running the initial height and color exporters. Higher-level converters accept a source asset directory and an output directory. Structure matching additionally uses local captured draws and the texture-signature catalog. Preserve the source and destination directories of each conversion stage.

The frozen native-scenery candidate completed six RTX 5090 routes of 2,208 presents each. DLSS and FSR passed at 192 and 16 material slots. RR returned 1,542,385 normalized surface guides, 1,231,854 reflected hits and 0.0118-pixel p99 reprojection error. An independent RR creation-failure test completed using standard DLSS. Geometry probes confirmed all 44,213 nondegenerate placement anchors across eight field archives. Gameplay coverage is the starter room and village. AMD and Intel hardware remain unverified.

The prepared repository source also completed the Windows ScarletNX executable build. A game executable linked with the repository renderer completed the 16-slot FSR route. To verify a recorded route, run `python tools/check-route.py LOG BACKEND SLOTS` from this directory. Accepted backends are `DLSS`, `FSR`, `RR` and `RR-fallback`.

## Experiments

`prepare.py --experiments DESTINATION` also applies the research changes. They cover geometry-assisted temporal history, property texture tiles and native-resolution archived emission. Configure `WORLD_PT_FEATURES` with semicolon-separated feature macros for a diagnostic build. For a same-frame history comparison use `GEOMETRY_HISTORY;HISTORY_WITNESS;COMPARE_FILTER` and set `ARCEUS_QA_REFERENCE_SERIES` to a writable file prefix. The two captures at presents 600 and 1960 contain a 128-sample reference and both filters. Use `tools/check-reference-light.py` and `tools/check-history-witness.py` to compare them.

The experimental history filter reduced normalized squared luminance error by 0.000028 indoors and 0.003700 in the village over the two captured frames. Moving-edge ghosting still needs validation.

Forward reflection linking takes locally captured guest fragment modules. To collect them, enable `FORWARD_MODULE_CAPTURE` in the experimental build, point `ARCEUS_SHADER_CAPTURE_DIR` to an existing empty directory, and play the relevant scenes. Set `WORLD_PT_PREPARED_SOURCE` to the prepared source tree and `ARCEUS_SHADER_CAPTURE_DIR` to the directory of locally captured modules. Put SPIRV-Tools and glslang on PATH and run `tools/link-reflections.py` (add `--pair` for a diagnostic variant). Generated headers belong only in the local prepared tree. These experiments require separate validation before release.
