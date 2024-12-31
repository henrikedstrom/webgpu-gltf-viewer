#include <memory>

#if defined(__EMSCRIPTEN__)
#include <emscripten/emscripten.h>
#endif

#include "application.h"

// Application default dimensions
constexpr uint32_t kDefaultWidth = 512;
constexpr uint32_t kDefaultHeight = 512;

int main()
{
    // Create and run the application
    auto app = std::make_unique<Application>(kDefaultWidth, kDefaultHeight);
    app->Run();

    // Keep runtime alive for Emscripten builds
#if defined(__EMSCRIPTEN__)
    emscripten_exit_with_live_runtime();
#endif

    return EXIT_SUCCESS;
}
