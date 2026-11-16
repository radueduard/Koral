# Building the SDK

Koral is built with CMake, its dependencies by vcpkg. What a program builds against is the **SDK**: the headers,
the libraries, the modules, the runtime and Koral's own shaders, installed into `stage/debug` or `stage/release` by
the `stage` target.

## Configure and build

```sh
git clone https://github.com/radueduard/Koral.git
cd Koral
cmake -S . -B cmake-build-release -G Ninja \
      -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_TOOLCHAIN_FILE="$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake"
cmake --build cmake-build-release --target stage
```

The first configure has vcpkg build every dependency, which takes a while; after that they come from its cache.
The `stage` target builds what it installs before installing it, so the SDK is never older than the source.

For a Debug SDK, configure another folder with `-DCMAKE_BUILD_TYPE=Debug` and stage it: it goes to `stage/debug`.
RelWithDebInfo and MinSizeRel stage as release, since they link the release C runtime.

On Windows, give the triplet the release workflow uses: `-DVCPKG_TARGET_TRIPLET=x64-windows-static-md`. On Apple
silicon, `arm64-osx`.

## Options

| Option | Default | |
|---|---|---|
| `KORAL_BUILD_MODULES` | `ON` | The modules that ship with Koral: camera, mesh, image import, export and compression, model import, `koral-ui`, `koral-net`. |
| `KORAL_BUILD_TESTS` | `ON` | The test suite. The GPU tests need a GPU, and run with the validation layers. |
| `KORAL_BUILD_DOTNET` | when the .NET 10 SDK is found | The C# bindings and `koral-dotnet`. See [C#](../csharp.md). |
| `KORAL_BUILD_KOTLIN` | when JDK 25 is found | The Kotlin bindings (`KORAL_JAVA_HOME`, or `JAVA_HOME`). See [Kotlin and Compose](../kotlin.md). |

## The tests

```sh
cmake --build cmake-build-release
ctest --test-dir cmake-build-release -j 8
```

## Using the SDK

A program finds the SDK as a CMake package, given its folder:

```cmake
cmake_minimum_required(VERSION 3.25)
project(Hello LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 23)

find_package(Koral REQUIRED)
add_executable(hello main.cpp)
target_link_libraries(hello PRIVATE Koral::Koral)
```

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH=<Koral>/stage/release
```

Build a Debug program against `stage/debug` and a Release one against `stage/release`: on Windows the two link
different C runtimes, and a program linking the wrong one crashes rather than failing to link.

Each module is a target of its own: `Koral::koral-ui`, `Koral::koral-net`, `Koral::koral-camera`,
`Koral::koral-mesh`, `Koral::koral-image-import`, `Koral::koral-image-export`, `Koral::koral-image-compress`,
`Koral::koral-model-import`. Link the ones a program uses; one it doesn't link costs it nothing.

Next: [A first program](first-program.md).
