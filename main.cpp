#include <iostream>
#include <fstream>
#include <filesystem>
#include <string>
#include <algorithm>
#include <gphoto2/gphoto2-camera.h>
#include <gphoto2/gphoto2.h>

namespace fs = std::filesystem;

// Helper for error handling
#define CHECK_GP(func, msg) \
    if (func < GP_OK) { \
        std::cerr << "Error: " << msg << std::endl; \
        return 1; \
    }

std::string sanitize_filename(std::string name) {
    std::replace(name.begin(), name.end(), ' ', '_');
    std::replace(name.begin(), name.end(), '/', '-');
    return name;
}

int main() {
    Camera *camera;
    GPContext *context;
    int ret;

    std::cout << "Initializing NovaCommand (libgphoto2 backend)..." << std::endl;

    // 1. Create a context
    context = gp_context_new();

    // 2. Initialize the camera
    ret = gp_camera_new(&camera);
    CHECK_GP(ret, "Failed to create camera instance");

    std::cout << "Detecting camera..." << std::endl;

    // 3. Init: Connects to the first available camera
    ret = gp_camera_init(camera, context);
    if (ret < GP_OK) {
        std::cerr << "No camera found or could not initialize. Is it connected and in PC Remote mode?" << std::endl;
        gp_camera_free(camera);
        gp_context_unref(context);
        return 1; // Exit if no camera found
    }

    std::cout << "Session Opened Successfully!" << std::endl;

    // 4. Get Camera Name (Model)
    CameraAbilities abilities;
    ret = gp_camera_get_abilities(camera, &abilities);
    std::string camera_model = "Unknown_Camera";
    if (ret == GP_OK) {
        camera_model = abilities.model;
    }
	std::cout << "CAMERA_MODEL: " << camera_model.substr(0, 14) << std::endl;
    std::string safe_model_name = sanitize_filename(camera_model.substr(0,14));
	std::cout << "SAFE_MODULE_NAME: " << safe_model_name << std::endl;

    // 5. Get Summary
    CameraText text;
    ret = gp_camera_get_summary(camera, &text, context);
    if (ret == GP_OK) {
        // Create directory if it doesn't exist
        std::string dir_path = "device_summaries";
        if (!fs::exists(dir_path)) {
            fs::create_directory(dir_path);
        }

        // Construct filename
        std::string filename = dir_path + "/" + safe_model_name + "_device_summary.txt";
		std::cout << "FILENAME: " << filename << std::endl;
        
        // Write to file
        std::ofstream outfile(filename);
        if (outfile.is_open()) {
            outfile << "Camera Model: " << camera_model << "\n";
            outfile << "----------------------------------------\n";
            outfile << text.text << "\n";
            outfile.close();
            std::cout << "Summary saved to: " << filename << std::endl;
        } else {
            std::cerr << "Failed to open file for writing: " << filename << std::endl;
        }

        // Print a snippet to console
        std::cout << "Camera Summary Snippet: " << std::string(text.text).substr(0, 100) << "..." << std::endl;
    } else {
        std::cerr << "Failed to get camera summary." << std::endl;
    }

    // 6. Clean up
    std::cout << "Closing session..." << std::endl;
    gp_camera_exit(camera, context);
    gp_camera_free(camera);
    gp_context_unref(context);

    return 0;
}
