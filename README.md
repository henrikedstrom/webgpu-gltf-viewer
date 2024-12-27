
# WebGPU glTF Viewer for Dawn (native) and Emscripten (web)

## Requirements

For Web builds, make sure Emscripten is installed first.

For native builds, pull Dawn as a submodule:

```sh
git submodule update --init
```

## Native build

```sh
# Build the app with CMake.
cmake -B build && cmake --build build -j8

# Run the app.
./build/app
```

## Web build

```sh
# Build the app with Emscripten.
emcmake cmake -B build-web && cmake --build build-web -j8

# Run a server.
npx http-server
```

```sh
# Open the web app.
open http://127.0.0.1:8080/build-web/app.html
```
