Water simulation using Fast Fourier Transform (webGPU)
==============

**TODO:** add fancy description

Questions
---------
- @builtin how to

Steps
-----
- camera
    - all the important matrices   
    - make it movable(optional)   
- compute shader  
    - make it compute something   
    - shared memory/binding funny biz   
- fft
    - basic concept   
    - fft fancy model   
- ***optimize***


Build
-----

```bash
# Build using wgpu-native backend
cmake -B build-wgpu -DWEBGPU_BACKEND=WGPU
cmake --build build-wgpu

# Build using Dawn backend
cmake -B build-dawn -DWEBGPU_BACKEND=DAWN
cmake --build build-dawn

# Build using emscripten
emcmake cmake -B build-emscripten
cmake --build build-emscripten
```

https://eliemichel.github.io/LearnWebGPU/
https://webgpufundamentals.org
https://webgpu.github.io/webgpu-samples/
https://emscripten.org
