Water simulation using Fast Fourier Transform (webGPU)
==============

**TODO:** add fancy description

Questions/Problems
------------------
- *@builtin* how to
- uniforms vs shader const
- which way is up



Done
----
- indexed plane
    - **TODO:** maybe don't use *#define*
- vert + frag pipeline
- camera
    - all the important matrices   
    -  **TODO:** make it movable(optional)   


Steps
-----
- compute shader  
    - make it compute something   
    - shared memory/binding funny biz   
- fft
    - basic concept   
    - fft fancy model
- skybox
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


Sources
-------
- [LearnWebGPU](https://eliemichel.github.io/LearnWebGPU/)
- [webgpufundamentals](https://webgpufundamentals.org)
- [webgpu-samples](https://webgpu.github.io/webgpu-samples/)
- [emscripten](https://emscripten.org)
