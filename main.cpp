#include "Application.h"

int main(int, char**)
{
    constexpr int width  = 1280;
    constexpr int height = 720;
    Application app(width, height);

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
