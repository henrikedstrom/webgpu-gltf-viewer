// Third-Party Library Headers
#if defined(__EMSCRIPTEN__)
#include <emscripten/emscripten.h>
#endif

// Project Headers
#include "application.h"

// Application default dimensions
constexpr uint32_t kDefaultWidth = 800;
constexpr uint32_t kDefaultHeight = 600;

// Main function
int main()
{
    // Create and run the application
    Application app(kDefaultWidth, kDefaultHeight);
    app.Run();

    // Keep runtime alive for Emscripten builds
#if defined(__EMSCRIPTEN__)
    emscripten_exit_with_live_runtime();
#endif

    return EXIT_SUCCESS;
}
