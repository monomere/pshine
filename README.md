# PlanetShine

Atmosphere and meshing playground. WIP.

TODO:

- [ ] Actual atmosphere rendering.
- [ ] Move the backend to vulkan (shouldn't be super hard).
- [ ] Terrain generation, textures.
- [ ] Mesh LOD.

## Cloning

> requirements: git, wget, python, internet connection

```bash
git clone https://github.com/monomere/pshine
cd pshine
```

The graphics backend uses [volk](https://github.com/zeux/volk) and
[VMA](https://github.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator),
so after cloning (only once):

```bash
wget -P include https://raw.githubusercontent.com/zeux/volk/master/volk.h
wget -P src/vendor https://raw.githubusercontent.com/zeux/volk/master/volk.c
wget -P include https://raw.githubusercontent.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator/master/include/vk_mem_alloc.h
```

### Patching owl

TBD. The shader language compiler (giraffe) uses [owl](https://github.com/ianh/owl), but it doesn't have a gitignore and
doesn't prefix some of the symbols in the generated header. Giraffe is currently in development so this doesn't matter.

## Generating the ImGui bindings

The project uses [dear_bindings](https://github.com/dearimgui/dear_bindings) to generate c version of the c++ imgui headers.

```bash
mkdir -p pshine/src/vendor/cimgui/
mkdir    pshine/src/vendor/cimgui/backends/

python vendor/dear_bindings/dear_bindings.py \
  vendor/imgui/imgui.h \
  --imgui-include-dir imgui/ \
	--backend-include-dir imgui/backends/ \
	-t vendor/dear_bindings/src/templates \
	-o pshine/src/vendor/cimgui/cimgui

python vendor/dear_bindings/dear_bindings.py \
  --backend \
  --imgui-include-dir imgui/ \
	--backend-include-dir imgui/backends/ \
	--imconfig-path vendor/imgui/imgui.h \
	-t vendor/dear_bindings/src/templates \
	-o pshine/src/vendor/cimgui/backends/cimgui_impl_vulkan \
  vendor/imgui/backends/imgui_impl_vulkan.h

python vendor/dear_bindings/dear_bindings.py \
  --backend \
  --imgui-include-dir imgui/ \
	--backend-include-dir imgui/backends/ \
	--imconfig-path vendor/imgui/imgui.h \
	-t vendor/dear_bindings/src/templates \
	-o pshine/src/vendor/cimgui/backends/cimgui_impl_glfw \
  vendor/imgui/backends/imgui_impl_glfw.h
```

## Building

> requirements: ninja, gcc-compatible c/c++ compiler, glfw

To build (incremental)
```bash
ninja
```

> **NB:** by default, the build.ninja file uses the full LLVM setup
> with lld, clang, libc++, etc. To use the system-provided stuff
> change the first line in `build.ninja` to include `build.system.ninja`
> instead. Or make your own config if you want to (or need to)!

## Running

> requirements: vulkan ≥1.2

```bash
build/pshine/main
```

The planet has a radius of 6371km (Earth), the atmosphere has a height of 100km, and the camera moves at 5km/s by default.

### Controls

Key|Action
---|---
<kbd>F</kbd> | Switch camera mode (default is arcball)

#### Arcball Mode

Key|Action
---|---
<kbd>A</kbd>/<kbd>D</kbd> | Rotate left/right (yaw)
<kbd>W</kbd>/<kbd>S</kbd> | Rotate up/down (pitch)
<kbd>X</kbd>/<kbd>Z</kbd> | Zoom in/out

#### Flycam Mode
Key|Action
---|---
<kbd>A</kbd>/<kbd>D</kbd> | Move left/right
<kbd>W</kbd>/<kbd>S</kbd> | Move forward/backward
<kbd>Shift</kbd>/<kbd>Space</kbd> | Move down/up

## TODO

- [x] Use double precision for position data and etc.
- [ ] Use near-origin coordinates for the "player".
- [ ] Velocty reference-frame.
- [ ] Fake perspective for celestial bodies.

### Useful stuff

- [Unite 2013 - Building a new universe in Kerbal Space Program](https://www.youtube.com/watch?v=mXTxQko-JH0)
  how ksp does stuff!
