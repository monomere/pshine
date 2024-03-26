# PlanetShine

Atmosphere and meshing playground. WIP.

TODO:

- [ ] Actual atmosphere rendering.
- [ ] Move the backend to vulkan (shouldn't be super hard).
- [ ] Terrain generation, textures.
- [ ] Mesh LOD.

## Building

> requirements: ninja, clang ≥16, glfw

Once after cloning:

```bash
wget https://raw.githubusercontent.com/skaslev/gl3w/master/gl3w_gen.py
python3 gl3w_gen.py
mkdir -p src/vendor
mv src/gl3w.c src/vendor
```

To build (incremental)
```bash
ninja
```

# Running

> requirements: opengl ≥4.6

```bash
build/main
```

Controls: WASD, Space, Shift.
The sphere has a radius of 100m, the camera moves at 10m/s.
