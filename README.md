# NovaCommand
My sony camera control software for astrophotography.

## Project Structure
- `main.cpp`: Main C++ source code for camera detection (using libusb).
- `CMakeLists.txt`: CMake build configuration.
- `LINUX_CAMERA_INTERNALS.md`: Documentation on Linux camera drivers & protocols.
- `build/`: Directory for build artifacts (created during build).

## Requirements
To build and run this project, you need the following dependencies installed on your Linux machine:

- **C++ Compiler** (GCC/Clang) with C++17 support.
- **CMake** (version 3.10 or higher).
- **libusb-1.0**: Library for generic USB device access (user-space driver).
- **pkg-config**: Helper tool used by CMake to find libraries.

### Install Dependencies (Ubuntu/Debian)
```bash
sudo apt-get update
sudo apt-get install -y libusb-1.0-0-dev pkg-config cmake build-essential
```

### Build Instructions
```bash
mkdir -p build
cd build
cmake ..
make
./nova_detect
```

## Links & References
*   **[libusb-1.0 Documentation](https://libusb.info/api-1.0/)**: Official C API reference for the USB library used in this project.
*   **[USB ID Repository](http://www.linux-usb.org/usb.ids)**: Source for looking up Vendor (0x054c) and Product IDs.
*   **[Sony Camera Remote SDK](https://support.d-imaging.sony.co.jp/app/sdk/en/index.html)**: Official Sony documentation for remote camera control (useful for protocol reference).
*   **[libgphoto2](http://www.gphoto.org/)**: The standard open-source camera control library (a good reference for PTP commands).
