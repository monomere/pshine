# PlanetShine

Atmosphere and meshing playground. WIP.

TODO:

- [ ] Actual atmosphere rendering.
- [ ] Move the backend to vulkan (shouldn't be super hard).
- [ ] Terrain generation, textures.
- [ ] Mesh LOD.

## Cloning

> requirements: git, wget, internet connection

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

TBD. The shader language compiler (giraffe) uses [owl](https://github.com/ianh/owl), but it lacks some stuff.

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

# Running

> requirements: vulkan ≥1.2

```bash
build/main
```

Controls: WASD, Space, Shift.
The sphere has a radius of 100m, the camera moves at 10m/s.
