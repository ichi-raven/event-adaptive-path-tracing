# Path-Tracing-Based Event Camera Simulation via Event-Adaptive Time Refinement (IEEE TVCG 2026)

[![paper](https://img.shields.io/badge/DOI-10.1109/TVCG.2026.3726999-blue.svg)](https://doi.org/10.1109/TVCG.2026.3726999)
[![License: CC BY-NC-SA 4.0](https://img.shields.io/badge/License-CC_BY--NC--SA_4.0-lightgrey.svg)](https://creativecommons.org/licenses/by-nc-sa/4.0/)

## Prerequisites

### Build tools

- C++20 capable compiler (tested with MSVC 19 and GCC 11.4)
- CMake 3.18 or newer.
- Git (used by CMake to download dependencies)
- Vulkan SDK 1.3 or newer (available at [LunarG](https://vulkan.lunarg.com/sdk/home)).

CMake must be able to locate `slang.h` and the Slang library. If they are not
found automatically, set `SLANG_INCLUDE_DIR` and `SLANG_LIBRARY` to their
installation paths when configuring the project. The Slang shared library
must also be discoverable at runtime when using a shared-library installation.

### GPU and operating system

The build has been tested with Vulkan SDK 1.3.x on Windows 11 and Ubuntu 22.04 LTS.

The GPU and driver must support Vulkan 1.3 and the following device extensions:

- `VK_KHR_ray_tracing_pipeline`
- `VK_KHR_acceleration_structure`
- `VK_KHR_ray_query`
- `VK_KHR_deferred_host_operations`
- `VK_NV_ray_tracing_motion_blur`
- `VK_EXT_robustness2`

The application also requires the associated Vulkan features, including
buffer device address and descriptor indexing. Installing the Vulkan SDK
alone does not guarantee that the GPU and driver meet these requirements.

The above requirements are most likely satisfied by NVIDIA RTX 30XX or later GPUs with the latest drivers.

## Build

```shell
git clone https://github.com/ichi-raven/event-adaptive-path-tracing.git
cd EventAdaptivePT

cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
```

### Build options

| CMake setting | Default | Description |
| --- | --- | --- |
| `USE_OPENMP` | `ON` | Enable CPU-side parallel processing. |
| `USE_OIDN` | `OFF` | Build support for RGB denoising with Open Image Denoise. |
| `OIDN_DIR` | `C:/oidn-2.3.3.x64.windows` | OIDN installation directory, used when `USE_OIDN=ON`. |

For example, to build without OpenMP:

```shell
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DUSE_OPENMP=OFF
cmake --build build --parallel
```

OIDN is not downloaded by FetchContent. To enable it, install OIDN separately
and configure with `-DUSE_OIDN=ON` and `-DOIDN_DIR=<installation-directory>`.
The current OIDN build configuration is Windows-oriented: setting `OIDN_DIR`
adds the header search path, but you may also need to configure the linker
search path for `OpenImageDenoise` and the runtime shared-library search path.

Note that OIDN is only used for RGB denoising and is not required for our event
stream rendering; therefore, it is disabled by default.

## Run

```shell
./build/bin/eapt \
  --input scenes/cornell_box/scene.json \
  --width 1024 \
  --height 1024 \
  --frames 20 \
  --spp 4096 \
  --mode events \
  --output outputs/cornell_box
```

### Common renderer options

To display all command-line options without loading a scene or initializing
the GPU, run the executable directly from the repository root:

```shell
./build/bin/eapt --help
```

`--help` does not require `--input`. The short option `-h` specifies height,
not help.

| Option | Description |
| :--- | :--- |
| `--help` | Display command-line help and exit successfully. |
| `--verbose` | Show detailed logs and source locations. |
| `--progress auto/on/off` | Choose automatic terminal detection, a forced live progress bar, or no progress output. Default: `auto`. |
| `--mode events/rgb/full` | Render events only, RGB only, or both; choose one of the three values. |
| `--width`, `--height` | Output image dimensions. |
| `--frames` | Number of frame intervals to process. |
| `--deltatime` | Duration of each frame interval, in microseconds. |
| `--spp` | Samples-per-pixel setting. |
| `--thres` | Override both positive and negative event thresholds. |
| `--threspos`, `--thresneg` | Override the positive or negative event threshold individually. |
| `--event_batch_size` | Maximum candidate pixels processed per event ray-tracing batch. `0` disables batching. Default: `0`. |
| `--exr` | Save RGB images as EXR instead of PNG. |
| `--motion` | Enable motion blur for RGB rendering. |
| `--denoise` | Denoise RGB images with OIDN; requires an OIDN-enabled build and `--exr`. |

Several options, including width, height, frame count, interval duration, spp,
and rendering mode can be specified in the scene JSON and **overridden** by
these command-line options. Event thresholds are resolved in this order:

```text
defaults -> scene JSON -> --thres -> --threspos / --thresneg
```

### Output files

- Events are saved to `<output>/events/events.csv`.
  The CSV has no header and contains `timestamp,x,y,polarity` columns.
  Timestamps are in microseconds; polarity is `1` for positive events and
  `0` for negative events.
- RGB images are saved to `<output>/RGB/` as PNG or EXR.

### Shader files

Shaders are compiled to SPIR-V during the build and written to
`build/bin/shaders` by default.

The executable uses the absolute shader directory recorded at build time,
not an automatic search relative to the executable. If the executable and
shader files are relocated, set `EAPT_SHADER_DIR` to the absolute path of
the new shader directory:

```shell
export EAPT_SHADER_DIR=/absolute/path/to/shaders
```

## License

Creative Commons Attribution-NonCommercial-ShareAlike 4.0 (CC BY-NC-SA 4.0) 2026 (c) Yuichiro Manabe.

Third-party dependencies and borrowed code retain their respective licenses;
the project license does not replace those licenses.
Existing copyright and license notices are retained in the vendored sources
and source-file comments.

The PCG-derived sampler is licensed under Apache-2.0.
See [the license text](licenses/Apache-2.0.txt) and the attribution in
`shaders/Slang/Sampler/IndependentSampler.slang`.

### Dependencies

| Library | License |
| --- | --- |
| GLFW | [zlib](https://github.com/glfw/glfw/blob/3.3.9/LICENSE.md) |
| GLM | [MIT or Happy Bunny License](https://github.com/g-truc/glm/blob/1.0.2/copying.txt) |
| Assimp | [BSD-3-Clause](https://github.com/assimp/assimp/blob/v6.0.2/LICENSE); bundled components have their own licenses |
| ng-log | [BSD-3-Clause](https://github.com/ng-log/ng-log/blob/v0.8.4/LICENSE.md) |
| TinyEXR | [BSD-3-Clause](https://github.com/syoyo/tinyexr/blob/8cf0642ca63802a5a07ca6d6cdc0e731c1f5cd6e/LICENSE); includes [OpenEXR-derived code](https://github.com/syoyo/tinyexr/blob/8cf0642ca63802a5a07ca6d6cdc0e731c1f5cd6e/tinyexr.h) |
| miniz used by TinyEXR | [MIT](https://github.com/syoyo/tinyexr/blob/8cf0642ca63802a5a07ca6d6cdc0e731c1f5cd6e/deps/miniz/LICENSE) |
| stb | [MIT or Public Domain / Unlicense](https://github.com/nothings/stb/blob/2c980bb59875b0d32144a71867fbdebb2f77cd20/LICENSE) |
| Dear ImGui | [MIT](https://github.com/ocornut/imgui/blob/46d39d56febc2a00bdd2270dc88c8a13f2a0441a/LICENSE.txt) |
| cxxopts | [MIT](third_party/cxxopts/LICENSE) |
| nlohmann/json | [MIT](third_party/nlohmann-json/LICENSE.MIT) |
| Slang | See the license accompanying the installed version ([upstream license](https://github.com/shader-slang/slang/blob/master/LICENSE)) |
| Vulkan SDK components | See the licenses accompanying the installed SDK |
| Open Image Denoise (optional) | [Apache-2.0](https://github.com/RenderKit/oidn/blob/v2.3.3/LICENSE.txt); bundled components have their own licenses |
| OpenMP runtime (optional) | Depends on the compiler and runtime implementation |

Dependencies obtained through FetchContent are downloaded from their upstream
repositories during configuration. Their license files remain in the
downloaded sources.
