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

## Building

> requirements: ninja, clang ≥16, glfw

To build (incremental)
```bash
ninja
```

# Running

> requirements: vulkan ≥1.2

```bash
build/main
```

Controls: WASD, Space, Shift.
The sphere has a radius of 100m, the camera moves at 10m/s.
