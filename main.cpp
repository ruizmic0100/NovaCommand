#include <iostream>
#include <libusb-1.0/libusb.h>
#include <iomanip>

// Sony Vendor ID
const uint16_t SONY_VID = 0x054c;

void print_device_info(libusb_device *dev) {
    libusb_device_descriptor desc;
    int r = libusb_get_device_descriptor(dev, &desc);
    if (r < 0) {
        std::cerr << "Failed to get device descriptor" << std::endl;
        return;
    }

    if (desc.idVendor == SONY_VID) {
        std::cout << "------------------------------------------------" << std::endl;
        std::cout << "Found Sony Device!" << std::endl;
        std::cout << "Vendor ID: 0x" << std::hex << std::setw(4) << std::setfill('0') << desc.idVendor << std::endl;
        std::cout << "Product ID: 0x" << std::hex << std::setw(4) << std::setfill('0') << desc.idProduct << std::endl;
        std::cout << "Bus: " << (int)libusb_get_bus_number(dev) << " Address: " << (int)libusb_get_device_address(dev) << std::endl;

        libusb_device_handle *handle = nullptr;
        r = libusb_open(dev, &handle);
        if (r == 0) {
            unsigned char data[256];
            
            // Get Manufacturer
            if (desc.iManufacturer) {
                r = libusb_get_string_descriptor_ascii(handle, desc.iManufacturer, data, sizeof(data));
                if (r > 0) std::cout << "Manufacturer: " << data << std::endl;
            }

            // Get Product Name
            if (desc.iProduct) {
                r = libusb_get_string_descriptor_ascii(handle, desc.iProduct, data, sizeof(data));
                if (r > 0) std::cout << "Product: " << data << std::endl;
            }
            
            // Get Serial Number
            if (desc.iSerialNumber) {
                r = libusb_get_string_descriptor_ascii(handle, desc.iSerialNumber, data, sizeof(data));
                if (r > 0) std::cout << "Serial: " << data << std::endl;
            }

            libusb_close(handle);
        } else {
            std::cout << "(Could not open device for strings - might be busy or need permissions)" << std::endl;
        }
        std::cout << "------------------------------------------------" << std::endl;
    }
}

int main() {
    libusb_context *ctx = nullptr;
    int r = libusb_init(&ctx);
    if (r < 0) {
        std::cerr << "Init Error: " << r << std::endl;
        return 1;
    }

    libusb_set_option(ctx, LIBUSB_OPTION_LOG_LEVEL, 3);

    libusb_device **devs;
    ssize_t cnt = libusb_get_device_list(ctx, &devs);
    if (cnt < 0) {
        std::cerr << "Get Device Error" << std::endl;
        return 1;
    }

    std::cout << "Scanning " << cnt << " USB devices for Sony hardware..." << std::endl;

    for (ssize_t i = 0; i < cnt; i++) {
        print_device_info(devs[i]);
    }

    libusb_free_device_list(devs, 1);
    libusb_exit(ctx);
    return 0;
}
