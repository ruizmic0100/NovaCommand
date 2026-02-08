# NovaCommand
My sony camera control software for astrophotography.

## Project Structure
- `main.cpp`: Main C++ source code for camera control (using libgphoto2).
- `CMakeLists.txt`: CMake build configuration.
- `LINUX_CAMERA_INTERNALS.md`: Documentation on Linux camera drivers & protocols.
- `build/`: Directory for build artifacts.

## Requirements
To build and run this project, you need the following dependencies installed on your Linux machine:

- **C++ Compiler** (GCC/Clang) with C++17 support.
- **CMake** (version 3.10 or higher).
- **libgphoto2**: The core camera control library.
- **pkg-config**: Helper tool used by CMake to find libraries.

### Install Dependencies (Ubuntu/Debian)
```bash
sudo apt-get update
sudo apt-get install -y libgphoto2-dev pkg-config cmake build-essential
```

### Build Instructions
```bash
mkdir -p build
cd build
cmake ..
make
./nova_command
```

## Links & References
*   **[libgphoto2 Documentation](http://www.gphoto.org/doc/api/)**: Official API reference.
*   **[USB ID Repository](http://www.linux-usb.org/usb.ids)**: Source for looking up Vendor (0x054c) and Product IDs.
*   **[Sony Camera Remote SDK](https://support.d-imaging.sony.co.jp/app/sdk/en/index.html)**: Official Sony documentation.
