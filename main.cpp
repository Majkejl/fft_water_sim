#include "Application.h"
#include "SimulationConfig.h"

int main(int, char**)
{
    SimulationConfig config;
    Application app(config.app.window_width, config.app.window_height);

#ifdef __EMSCRIPTEN__
    auto callback = [](void* arg) {
        static_cast<Application*>(arg)->main_loop();
    };
    emscripten_set_main_loop_arg(callback, &app, 0, true);
#else
    while (app.is_running()) {
        app.main_loop();
    }
#endif

    return 0;
}
