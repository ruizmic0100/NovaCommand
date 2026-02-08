#include <iostream>
#include <gphoto2/gphoto2-camera.h>
#include <gphoto2/gphoto2.h>

// Helper for error handling
#define CHECK_GP(func, msg) \
    if (func < GP_OK) { \
        std::cerr << "Error: " << msg << std::endl; \
        return 1; \
    }

int main() {
    Camera *camera;
    GPContext *context;
    int ret;

    std::cout << "Initializing NovaCommand (libgphoto2 backend)..." << std::endl;

    // 1. Create a context (required for callbacks/error reporting)
    context = gp_context_new();

    // 2. Initialize the camera
    // gp_camera_new just allocates structure, doesn't connect yet
    ret = gp_camera_new(&camera);
    CHECK_GP(ret, "Failed to create camera instance");

    std::cout << "Detecting camera..." << std::endl;

    // 3. Init: This connects to the first available camera via USB
    // This implicitly performs the "OpenSession" logic internally in the driver
    ret = gp_camera_init(camera, context);
    if (ret < GP_OK) {
        std::cerr << "No camera found or could not initialize. Is it connected and in PC Remote mode?" << std::endl;
        gp_camera_free(camera);
        return 1;
    }

    // 4. Print confirmation
    CameraText text;
    ret = gp_camera_get_summary(camera, &text, context);
    if (ret == GP_OK) {
        std::cout << "Session Opened Successfully!" << std::endl;
        std::cout << "Camera Summary : " << std::string(text.text).substr(0, -1) << "..." << std::endl;
    } else {
        std::cout << "Session Opened, but failed to get summary." << std::endl;
    }

    // 5. Clean up (Close Session)
    std::cout << "Closing session..." << std::endl;
    gp_camera_exit(camera, context);
    gp_camera_free(camera);
    gp_context_unref(context);

    return 0;
}
