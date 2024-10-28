#include <webgpu/webgpu.h>
#include <iostream>
#include <cassert>

/**
 * Utility function to get a WebGPU adapter, so that
 *     WGPUAdapter adapter = requestAdapterSync(options);
 * is roughly equivalent to
 *     const adapter = await navigator.gpu.requestAdapter(options);
 */
WGPUAdapter requestAdapterSync(WGPUInstance instance, WGPURequestAdapterOptions const * options) {
    // A simple structure holding the local information shared with the
    // onAdapterRequestEnded callback.
    struct UserData {
        WGPUAdapter adapter = nullptr;
        bool requestEnded = false;
    };
    UserData userData;

    // Callback called by wgpuInstanceRequestAdapter when the request returns
    // This is a C++ lambda function, but could be any function defined in the
    // global scope. It must be non-capturing (the brackets [] are empty) so
    // that it behaves like a regular C function pointer, which is what
    // wgpuInstanceRequestAdapter expects (WebGPU being a C API). The workaround
    // is to convey what we want to capture through the pUserData pointer,
    // provided as the last argument of wgpuInstanceRequestAdapter and received
    // by the callback as its last argument.
    auto onAdapterRequestEnded = [](WGPURequestAdapterStatus status, WGPUAdapter adapter, char const * message, void * pUserData) {
        UserData& userData = *reinterpret_cast<UserData*>(pUserData);
        if (status == WGPURequestAdapterStatus_Success) {
            userData.adapter = adapter;
        } else {
            std::cout << "Could not get WebGPU adapter: " << message << std::endl;
        }
        userData.requestEnded = true;
    };

    // Call to the WebGPU request adapter procedure
    wgpuInstanceRequestAdapter(
        instance /* equivalent of navigator.gpu */,
        options,
        onAdapterRequestEnded,
        (void*)&userData
    );

    // We wait until userData.requestEnded gets true
    #ifdef __EMSCRIPTEN__
    while (!userData.requestEnded) {
        emscripten_sleep(100);
    }
	#endif // __EMSCRIPTEN__

    assert(userData.requestEnded);

    return userData.adapter;
}

int main (int, char**) {
	// We create a descriptor
	WGPUInstanceDescriptor desc = {};
	desc.nextInChain = nullptr;

	// We create the instance using this descriptor
#ifdef WEBGPU_BACKEND_EMSCRIPTEN
	WGPUInstance instance = wgpuCreateInstance(nullptr);
#else //  WEBGPU_BACKEND_EMSCRIPTEN
	WGPUInstance instance = wgpuCreateInstance(&desc);
#endif //  WEBGPU_BACKEND_EMSCRIPTEN

	// We can check whether there is actually an instance created
	if (!instance) {
		std::cerr << "Could not initialize WebGPU!" << std::endl;
		return 1;
	}

	// Display the object (WGPUInstance is a simple pointer, it may be
	// copied around without worrying about its size).
	std::cout << "WGPU instance: " << instance << std::endl;

	std::cout << "Requesting adapter..." << std::endl;

	WGPURequestAdapterOptions adapterOpts = {};
	adapterOpts.nextInChain = nullptr;
	WGPUAdapter adapter = requestAdapterSync(instance, &adapterOpts);

	std::cout << "Got adapter: " << adapter << std::endl;

	// We clean up the WebGPU instance
	wgpuInstanceRelease(instance);

	return 0;
}