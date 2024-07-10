# voxel-engine
A simple voxel engine made for learning about voxels and advanced GPU / CPU optimization techniques.
Uses C++, HLSL and D3D12.

# Features
* Bindless rendering (SM 6.6)
* Reverse Z
* Multi-threaded, async copy queue chunk loading system
* Indirect rendering
* GPU Culling

# Gallery
[Link : Click here, or on the Image below!](https://youtu.be/E0T0UMnOggg) 

[![Youtube link](https://img.youtube.com/vi/E0T0UMnOggg/hqdefault.jpg)](https://youtu.be/E0T0UMnOggg)

# Building
+ This project uses CMake as a build system, and all third party libs are setup using CMake's FetchContent().
+ There is a custom script that can be used to setup DirectX Shader Compiler (DXC) and the Agility SDK.
+ After cloning the project, build the project using CMake :
``` 
1. Run the setup_dxc_and_agility_sdk.bat file,

2.
cmake -S . -B build 
cmake --build build --config release
```

# Controls
+ WASD -> Move camera.
+ Arrow keys -> Modify camera orientation.
+ Escape -> Exit editor.