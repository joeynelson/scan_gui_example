# scan_gui_example
Example of using the Pinchot C library to plot data from a JS-50 scan head.

### Requirements

- C++17 compiler (MSVC or Clang)
- CMake 3.1 or newer
- Python 3.7 or newer

### Building for Windows

On Windows, we recommend using to MSVC 2019:

```shell
> cd scan_gui_example
> mkdir build
> cd build
> cmake .. -G "Visual Studio 16 2019" -A x64
> cmake --build . --config Release
```

### Building for Linux 

```shell
> cd scan_gui_example
> mkdir build && cd build
> cmake .. -DCMAKE_BUILD_TYPE="Release"
> cmake --build .
```

### Building for macOS (not at all tested, but it might work?)

If you're on a relatively new version of macOS, you should be able to use the defeault Apple Clang compiler:

```shell
> cd scan_gui_example
> mkdir build && cd build
> cmake .. -DCMAKE_BUILD_TYPE="Release"
> cmake --build .
```
