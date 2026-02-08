#include <iostream>
#include <fstream>
#include <filesystem>
#include <string>
#include <vector>
#include <chrono>
#include <thread>
#include <algorithm>
#include <gphoto2/gphoto2-camera.h>
#include <gphoto2/gphoto2.h>

namespace fs = std::filesystem;

/*
 * ======================================================================================
 * CAPTURE FLOW DOCUMENTATION
 * ======================================================================================
 * 
 * The following steps outline how a photo is taken using libgphoto2 (PTP):
 * 
 * 1. INITIALIZATION (setup_camera)
 *    - Create a GPContext (handles error reporting and callbacks).
 *    - Create a Camera struct.
 *    - Call gp_camera_init(): connects to USB, finds the first camera, 
 *      and establishes a PTP session (OpenSession).
 * 
 * 2. CONFIGURATION (Not yet implemented deeply, but part of the flow)
 *    - Set exposure settings (ISO, Shutter Speed) via gp_camera_set_config.
 * 
 * 3. TRIGGER CAPTURE (capture_photo)
 *    - Call gp_camera_capture(..., GP_CAPTURE_IMAGE, ...).
 *    - This sends the "InitiateCapture" PTP command.
 *    - The camera takes the photo.
 *    - The function blocks until the camera says the image is ready (or fails).
 *    - Returns a CameraFilePath (folder + filename on the camera's storage/RAM).
 * 
 * 4. DOWNLOAD (download_photo)
 *    - Allocate a generic CameraFile in memory.
 *    - Call gp_camera_file_get() to pull data from the CameraFilePath to our memory buffer.
 *    - Save the memory buffer to a local file on the Linux filesystem.
 * 
 * 5. CLEANUP (delete_file_on_camera)
 *    - Call gp_camera_file_delete() to remove the image from the camera's internal buffer
 *      (crucial for avoiding "Card Full" errors during tethered shooting).
 * 
 * 6. SHUTDOWN
 *    - gp_camera_exit() closes the PTP session.
 * 
 * ======================================================================================
 */

// Helper for error handling
#define CHECK_GP(func, msg) \
    if (func < GP_OK) { \
        std::cerr << "Error: " << msg << " (" << func << ")" << std::endl; \
        return func; \
    }

// Global Context (for simplicity in this stage)
GPContext *context;

std::string sanitize_filename(std::string name) {
    std::replace(name.begin(), name.end(), ' ', '_');
    std::replace(name.begin(), name.end(), '/', '-');
    return name;
}

// ----------------------------------------------------------------------------------
// CORE FUNCTIONS
// ----------------------------------------------------------------------------------

int setup_camera(Camera **camera) {
    int ret;
    context = gp_context_new();

    ret = gp_camera_new(camera);
    if (ret < GP_OK) return ret;

    std::cout << "[Step 1] Initializing camera connection..." << std::endl;
    ret = gp_camera_init(*camera, context);
    if (ret < GP_OK) {
        std::cerr << "Could not find camera. Check USB connection." << std::endl;
        return ret;
    }
    return GP_OK;
}

int capture_photo(Camera *camera, CameraFilePath &camera_file_path) {
    int ret;
    std::cout << "[Step 2] Triggering Capture..." << std::endl;
    
    // GP_CAPTURE_IMAGE tells the camera to take a still photo
    // This function blocks until the capture is done and file is ready
    ret = gp_camera_capture(camera, GP_CAPTURE_IMAGE, &camera_file_path, context);
    
    if (ret < GP_OK) {
        std::cerr << "Capture failed." << std::endl;
        return ret;
    }
    
    std::cout << "  Camera saved image to: " << camera_file_path.folder << "/" << camera_file_path.name << std::endl;
    return GP_OK;
}

int download_photo(Camera *camera, const CameraFilePath &camera_file_path, const std::string &local_filename) {
    int ret;
    CameraFile *file;
    std::cout << "[Step 3] Downloading to " << local_filename << "..." << std::endl;

    // Create a new file handle
    ret = gp_file_new(&file);
    if (ret < GP_OK) return ret;

    // Download from camera to the file handle
    ret = gp_camera_file_get(camera, camera_file_path.folder, camera_file_path.name, GP_FILE_TYPE_NORMAL, file, context);
    if (ret < GP_OK) {
        std::cerr << "Failed to download file." << std::endl;
        gp_file_free(file);
        return ret;
    }

    // Save to disk
    ret = gp_file_save(file, local_filename.c_str());
    gp_file_free(file); // Free the memory buffer
    
    if (ret < GP_OK) {
        std::cerr << "Failed to save file to disk." << std::endl;
        return ret;
    }
    
    std::cout << "  Download successful." << std::endl;
    return GP_OK;
}

int delete_file_on_camera(Camera *camera, const CameraFilePath &camera_file_path) {
    int ret;
    std::cout << "[Step 4] Deleting file from camera buffer..." << std::endl;
    
    // Remove the file from the camera's RAM/Storage to keep it clean
    ret = gp_camera_file_delete(camera, camera_file_path.folder, camera_file_path.name, context);
    if (ret < GP_OK) {
        std::cerr << "Failed to delete file on camera." << std::endl;
        return ret;
    }
    
    std::cout << "  Cleanup successful." << std::endl;
    return GP_OK;
}

void close_camera(Camera *camera) {
    std::cout << "[Step 5] Closing session..." << std::endl;
    gp_camera_exit(camera, context);
    gp_camera_free(camera);
    gp_context_unref(context);
}

// ----------------------------------------------------------------------------------
// HIGH LEVEL ACTIONS
// ----------------------------------------------------------------------------------

void save_device_summary(Camera *camera) {
    CameraText text;
    int ret = gp_camera_get_summary(camera, &text, context);
    if (ret == GP_OK) {
        // Create directory
        std::string dir_path = "device_summaries";
        if (!fs::exists(dir_path)) fs::create_directory(dir_path);

        // Get Model Name
        CameraAbilities abilities;
        gp_camera_get_abilities(camera, &abilities);
        std::string model = sanitize_filename(abilities.model);
        
        // Save
        std::string filename = dir_path + "/" + model + "_device_summary.txt";
        std::ofstream outfile(filename);
        if (outfile.is_open()) {
            outfile << "Camera Model: " << abilities.model << "\n";
            outfile << "----------------------------------------\n";
            outfile << text.text << "\n";
            outfile.close();
            std::cout << "  Device summary saved to " << filename << std::endl;
        }
    }
}

// The requested "test_shot" function
void test_shot(Camera *camera) {
    std::cout << "\n--- STARTING TEST SHOT ROUTINE ---\n" << std::endl;
    
    CameraFilePath camera_file_path;
    int ret;

    // 1. Capture
    ret = capture_photo(camera, camera_file_path);
    if (ret < GP_OK) {
        std::cerr << "Test shot failed at capture stage." << std::endl;
        return;
    }

    // 2. Download
    // Ensure 'captures' folder exists
    if (!fs::exists("captures")) fs::create_directory("captures");
    
    std::string local_filename = "captures/test_shot_" + std::string(camera_file_path.name);
    ret = download_photo(camera, camera_file_path, local_filename);
    if (ret < GP_OK) {
        std::cerr << "Test shot failed at download stage." << std::endl;
        // Try to clean up anyway
        delete_file_on_camera(camera, camera_file_path); 
        return;
    }

    // 3. Cleanup
    ret = delete_file_on_camera(camera, camera_file_path);
    if (ret < GP_OK) {
        std::cerr << "Warning: Could not delete file from camera." << std::endl;
    }

    std::cout << "\n--- TEST SHOT COMPLETE ---\n" << std::endl;
}

int main() {
    Camera *camera;
    int ret;

    std::cout << "NovaCommand: Camera Control System" << std::endl;

    // 1. Setup
    ret = setup_camera(&camera);
    if (ret < GP_OK) return 1;

    // 2. Info
    save_device_summary(camera);

    // 3. Test Shot
    test_shot(camera);

    // 4. Teardown
    close_camera(camera);

    return 0;
}
