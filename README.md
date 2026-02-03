# Bosch Etch Process Simulator

A C++ simulation of the Bosch deep reactive ion etching (DRIE) process, implemented using OpenGL and GLFW.  
The project models particle-based etching and deposition to approximate real Bosch process behavior.

Built with CMake and C++17.

---

## Features

- Particle-based etch and deposition simulation  
- Voxel/grid based material interaction  
- OpenGL rendering  
- Custom ray marching for particle motion  
- Shader-based visualization  

---

## Requirements

You need the following installed:

- CMake 3.16 or newer  
- C++ compiler with C++17 support (MSVC, GCC, or Clang)  
- GLFW library  

---

## Installing GLFW (recommended via vcpkg)

If you are using vcpkg:

```bash
vcpkg install glfw3
vcpkg integrate install
```

## Building the Project

From the project root directory:

```bash
mkdir build
cd build
cmake ..
cmake --build .
```

## Windows (with vcpkg)
```bash
cmake .. -DCMAKE_TOOLCHAIN_FILE=[path_to_vcpkg]/scripts/buildsystems/vcpkg.cmake
cmake --build .
```

Replace [path_to_vcpkg] with your actual vcpkg directory.

## Running

Linux / macOS:

```bash
./BoschEtchProcess
```

Windows:

Executable will be in:

```bash
build/Debug/
```
