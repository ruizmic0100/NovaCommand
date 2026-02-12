#include <algorithm>
#include <iostream>
#include <iomanip>
#include <sstream>
#include <fstream>
#include <filesystem>
#include <string>
#include <vector>
#include <chrono>
#include <thread>
#include <algorithm>
#include <cstring>
#include <gphoto2/gphoto2-camera.h>
#include <gphoto2/gphoto2.h>
#include "json.hpp"

using json = nlohmann::json;

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
 * 2. CONFIGURATION (set_camera_config)
 *    - Fetch the configuration tree (gp_camera_get_config).
 *    - Find specific widgets by name (e.g., "iso", "shutterspeed", "f-number").
 *    - Set the value of the widget.
 *    - Apply the changes back to the camera (gp_camera_set_config).
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

// Global Context
GPContext *context;

// Callback for error reporting
static void error_callback(GPContext *context, const char *text, void *data) {
    std::cerr << "[GPhoto Error] " << text << std::endl;
}

// Callback for messages
static void message_callback(GPContext *context, const char *text, void *data) {
    // std::cout << "[GPhoto Message] " << text << std::endl;
}

std::string sanitize_filename(std::string name) {
    std::replace(name.begin(), name.end(), ' ', '_');
    std::replace(name.begin(), name.end(), '/', '-');
    return name;
}

// ----------------------------------------------------------------------------------
// CONFIGURATION HELPERS
// ----------------------------------------------------------------------------------

int _lookup_widget(CameraWidget *widget, const char *key, CameraWidget **child) {
    int ret = gp_widget_get_child_by_name(widget, key, child);
    if (ret < GP_OK) {
        ret = gp_widget_get_child_by_label(widget, key, child);
    }
    return ret;
}

void print_config_options(Camera *camera, const char *key) {
    CameraWidget *widget = nullptr;
    CameraWidget *child = nullptr;
    int ret;

    ret = gp_camera_get_config(camera, &widget, context);
    if (ret < GP_OK) return;

    ret = _lookup_widget(widget, key, &child);
    if (ret < GP_OK) {
        std::cout << "  Config [" << key << "] not found." << std::endl;
        gp_widget_free(widget);
        return;
    }

    const char *value;
    gp_widget_get_value(child, &value);
    std::cout << "  Current [" << key << "]: " << value << std::endl;

    int type;
    gp_widget_get_type(child, (CameraWidgetType*)&type);
    if (type == GP_WIDGET_RADIO) {
        int count = gp_widget_count_choices(child);
        std::cout << "  Available options for [" << key << "]:" << std::endl;
        for (int i = 0; i < count; i++) {
            const char *choice;
            gp_widget_get_choice(child, i, &choice);
            std::cout << "    - " << choice << std::endl;
        }
    }
    gp_widget_free(widget);
}

int set_config_value(Camera *camera, const char *key, const char *value) {
    CameraWidget *widget = nullptr;
    CameraWidget *child = nullptr;
    int ret;

    // 1. Get the current configuration tree
    std::cout << "  Setting config [" << key << "] to [" << value << "]... ";
    ret = gp_camera_get_config(camera, &widget, context);
    if (ret < GP_OK) {
        std::cerr << "Failed to get config." << std::endl;
        return ret;
    }

    // 2. Find the specific setting widget
    ret = _lookup_widget(widget, key, &child);
    if (ret < GP_OK) {
        std::cerr << "Setting not found." << std::endl;
        gp_widget_free(widget);
        return ret;
    }

    // 3. Set the value
    // Note: This only updates the local widget structure, not the camera yet.
    ret = gp_widget_set_value(child, value);
    if (ret < GP_OK) {
        std::cerr << "Invalid value for this setting." << std::endl;
        gp_widget_free(widget);
        return ret;
    }

    // 4. Apply changes back to the camera
    ret = gp_camera_set_config(camera, widget, context);
    if (ret < GP_OK) {
        std::cerr << "Failed to apply config to camera." << std::endl;
    } else {
        std::cout << "Done." << std::endl;
    }

    // 5. Clean up
    gp_widget_free(widget);
    return ret;
}

// ----------------------------------------------------------------------------------
// CORE FUNCTIONS
// ----------------------------------------------------------------------------------

int setup_camera(Camera **camera) {
    int ret;
    context = gp_context_new();
    
    // Set callbacks
    gp_context_set_error_func(context, error_callback, NULL);
    gp_context_set_message_func(context, message_callback, NULL);

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
    std::cout << "[Step 3] Triggering Capture..." << std::endl;
    
    // GP_CAPTURE_IMAGE tells the camera to take a still photo
    // This function blocks until the capture is done and file is ready
    ret = gp_camera_capture(camera, GP_CAPTURE_IMAGE, &camera_file_path, context);
    
    if (ret < GP_OK) {
        std::cerr << "Capture failed. Error code: " << ret << " (" << gp_result_as_string(ret) << ")" << std::endl;
        return ret;
    }
    
    std::this_thread::sleep_for(std::chrono::milliseconds(200)); // Small delay after capture

    // IMPORTANT: Verify the path is valid before using it
    if (strlen(camera_file_path.folder) == 0 || strlen(camera_file_path.name) == 0) {
        std::cerr << "Warning: Camera returned empty file path!" << std::endl;
        return GP_ERROR;
    }

    std::cout << "  Camera saved image to: [" << camera_file_path.folder << "] / [" << camera_file_path.name << "]" << std::endl;

    // FIX: Some cameras return "//" or "/" as folder. Clean it up for display/logic if needed,
    // but usually we just pass it back to the library.
    
    return GP_OK;
}

int download_photo(Camera *camera, const CameraFilePath &camera_file_path, const std::string &local_filename) {
    int ret;
    CameraFile *file;
    std::cout << "[Step 4] Downloading to " << local_filename << "..." << std::endl;

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
    
    std::this_thread::sleep_for(std::chrono::milliseconds(200)); // Small delay after download from camera

    // Save to disk
    std::cout << "  Saving to disk..." << std::endl;
    ret = gp_file_save(file, local_filename.c_str());
    
    // Always free the file handle regardless of save success.
    // NOTE: gp_file_save does NOT free the file handle.
    gp_file_free(file); 
    
    // Force a small flush/sync to ensure OS writes are done?
    // Not strictly necessary for segfault prevention but good for debug.
    std::this_thread::sleep_for(std::chrono::milliseconds(200)); // Small delay after freeing file
    
    if (ret < GP_OK) {
        std::cerr << "Failed to save file to disk (" << ret << ")." << std::endl;
        return ret;
    } 
    
    std::cout << "  Download successful." << std::endl;
    return GP_OK;
}

int delete_file_on_camera(Camera *camera, const CameraFilePath &camera_file_path) {
    int ret;
    std::cout << "[Step 5] Deleting file from camera buffer: [" << camera_file_path.folder << "] / [" << camera_file_path.name << "]" << std::endl;
    
    // Verify paths are valid
    if (strlen(camera_file_path.folder) == 0 || strlen(camera_file_path.name) == 0) {
        std::cerr << "Warning: Invalid file path for deletion." << std::endl;
        return GP_ERROR;
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(200)); // Small delay before delete

    // Try deleting without folder path if it seems empty or root
    // Some cameras are weird about the folder argument for delete.
    const char* folder_to_use = camera_file_path.folder;
    if (strcmp(folder_to_use, "/") == 0 || strcmp(folder_to_use, "\\") == 0) {
        folder_to_use = "/"; // normalize to root
    }

    // Remove the file from the camera's RAM/Storage to keep it clean
    std::cout << "  Calling gp_camera_file_delete(" << folder_to_use << ", " << camera_file_path.name << ")..." << std::endl;
    
    // IMPORTANT: Some Sony/Canon drivers have issues if you delete immediately after download without a small wait or context check.
    // Also, if 'camera' pointer is somehow corrupted (unlikely if passed by pointer), that would cause a segfault.
    
    ret = gp_camera_file_delete(camera, folder_to_use, camera_file_path.name, context);
    
    std::cout << "  gp_camera_file_delete returned: " << ret << std::endl;

    if (ret < GP_OK) {
        // Log error properly, but don't crash
        std::cerr << "Failed to delete file on camera. Error: " << ret << " (" << gp_result_as_string(ret) << ")" << std::endl;
        return ret;
    }
    
    std::cout << "  Cleanup successful." << std::endl;
    return GP_OK;
}

void close_camera(Camera *camera) {
    std::cout << "[Step 6] Closing session..." << std::endl;
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

std::string get_next_capture_filename(std::string directory, std::string prefix, std::string extension = ".jpg") {
	int maxIndex = -1;

	if (fs::exists(directory) && fs::is_directory(directory)) {
		for (const auto& entry : fs::directory_iterator(directory)) {
			std::string filename = entry.path().stem().string(); // Get name without extension
			
			if (filename.find(prefix) == 0) {
				std::string numPart = filename.substr(prefix.length()); // Use prefix length instead of hardcoded 4

				try {
					int currentId = std::stoi(numPart);
					if (currentId > maxIndex) {
						maxIndex = currentId;
					}
				} catch (...) {
					// Ignore files that don't end in valid numbers
				}
			}
		}
	}

	int nextId = maxIndex + 1;

	std::ostringstream oss;

	oss << prefix << std::setw(4) << std::setfill('0') << nextId << extension;
	return oss.str();	
}

// The requested "test_shot" function
void test_shot(Camera *camera) {
    std::cout << "\n--- STARTING TEST SHOT ROUTINE ---\n" << std::endl;
    
    CameraFilePath camera_file_path;
    int ret;

    // 0. Configuration (Step 2 in docs)
    // Note: Values like "100" or "1/50" must be EXACT strings supported by the camera.
    // Use 'gphoto2 --list-config' or 'gphoto2 --get-config iso' to see valid values.
    std::cout << "[Step 2] Applying Settings..." << std::endl;
    
    // DIAGNOSTIC: Check what 'capturetarget' options exist
    // print_config_options(camera, "capturetarget");

    // Force capture to internal RAM to avoid fetching old SD card images
    // Options found: "Memory card", "Internal RAM" (or similar)
    // Update: User reports "sdram" is the correct option for this camera.
    set_config_value(camera, "capturetarget", "sdram");
    set_config_value(camera, "iso", "10000");
    set_config_value(camera, "shutterspeed", "1");
    set_config_value(camera, "f-number", "3.5"); // Uncomment if lens supports aperture control
    
    // Set format to RAW
    set_config_value(camera, "imageformat", "RAW");

    // 1. Capture
    ret = capture_photo(camera, camera_file_path);
    if (ret < GP_OK) {
        std::cerr << "Test shot failed at capture stage." << std::endl;
        return;
    }

    // 2. Download
    // Ensure 'captures' folder exists
    if (!fs::exists("captures")) fs::create_directory("captures");

    // Try to detect extension from camera file path
    std::string extension = ".jpg"; // Default fallback
    std::string camera_fname = camera_file_path.name;
    size_t last_dot = camera_fname.find_last_of(".");
    if (last_dot != std::string::npos) {
        extension = camera_fname.substr(last_dot);
    }
    
    std::string nextFile = get_next_capture_filename("captures/", "test_shot_capt", extension);

    
    std::string local_filename = "captures/" + nextFile;
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


// ----------------------------------------------------------------------------------
// JSON SEQUENCE EXECUTION
// ----------------------------------------------------------------------------------

void run_json_sequence(const std::string& json_path) {
    std::cout << "\n--- STARTING JSON SEQUENCE ---\n" << std::endl;
    
    std::ifstream f(json_path);
    if (!f.is_open()) {
        std::cerr << "Could not open JSON file: " << json_path << std::endl;
        return;
    }

    json data;
    try {
        f >> data;
    } catch (const json::parse_error& e) {
        std::cerr << "JSON Parse Error: " << e.what() << std::endl;
        return;
    }

    if (!data.contains("shots")) {
        std::cerr << "JSON must contain a 'shots' key." << std::endl;
        return;
    }

    // Ensure 'captures' folder exists
    if (!fs::exists("captures")) {
        fs::create_directory("captures");
        std::cout << "[Sequence] Created 'captures' directory." << std::endl;
    }

    auto process_shot_config = [&](const json& config) {
        // Determine Count
        int count = 1;
        if (config.contains("count")) {
            count = config["count"].get<int>();
        }

        std::cout << "[Sequence] Taking " << count << " shots with new config..." << std::endl;
        
        for (int i = 0; i < count; i++) {
            // RE-INIT CAMERA FOR EACH SHOT
            // This is heavy, but safest for stability if session state is corrupted.
            Camera *camera;
            if (setup_camera(&camera) < GP_OK) {
                std::cerr << "Failed to re-initialize camera for shot " << (i+1) << std::endl;
                // Wait and try again? Or abort?
                std::this_thread::sleep_for(std::chrono::seconds(2));
                continue;
            }

            // 1. Apply Settings (Must apply every time since we just opened connection)
            if (config.contains("ISO")) {
                std::string val;
                if (config["ISO"].is_number()) val = std::to_string(config["ISO"].get<int>());
                else val = config["ISO"].get<std::string>();
                set_config_value(camera, "iso", val.c_str());
            }
            if (config.contains("Shutter speed")) {
                std::string val;
                if (config["Shutter speed"].is_number()) val = std::to_string(config["Shutter speed"].get<int>());
                else val = config["Shutter speed"].get<std::string>();
                set_config_value(camera, "shutterspeed", val.c_str());
            }
            if (config.contains("F-Number")) {
                std::string val;
                if (config["F-Number"].is_number()) val = std::to_string(config["F-Number"].get<double>());
                else val = config["F-Number"].get<std::string>();
                set_config_value(camera, "f-number", val.c_str());
            }
            
            // Force SDRAM every time
            set_config_value(camera, "capturetarget", "sdram");

            // Set Image Format to RAW
            // The value "RAW" is common, but some cameras use "RAW + Large Fine JPEG" or similar.
            // Adjust this string if your specific camera model requires a different value.
            set_config_value(camera, "imageformat", "RAW");


            // 2. Capture
            CameraFilePath camera_file_path;
            memset(&camera_file_path, 0, sizeof(camera_file_path));

            int ret = capture_photo(camera, camera_file_path);
            if (ret < GP_OK) {
                std::cerr << "Failed to capture shot " << (i+1) << "/" << count << std::endl;
                close_camera(camera); // Clean up
                continue; 
            }

            // 3. Download
            // Use a unique prefix for the sequence batch, or keep global index? 
            // "seq_shot_" is good, get_next_capture_filename should handle incrementing.
            // But let's verify if the logic holds up for rapid fire.
            
            // Try to detect extension from camera file path
            std::string extension = ".jpg"; // Default fallback
            std::string camera_fname = camera_file_path.name;
            size_t last_dot = camera_fname.find_last_of(".");
            if (last_dot != std::string::npos) {
                extension = camera_fname.substr(last_dot);
            }

            std::string nextFile = get_next_capture_filename("captures/", "seq_shot_", extension);
            std::filesystem::path cwd = std::filesystem::current_path();
            std::filesystem::path full_path = cwd / "captures" / nextFile;
            std::string local_filename = full_path.string();
            
            std::cout << "[Sequence] Saving to: " << local_filename << std::endl;
            
            if (download_photo(camera, camera_file_path, local_filename) == GP_OK) {
                std::this_thread::sleep_for(std::chrono::milliseconds(200));
                delete_file_on_camera(camera, camera_file_path);
            } else {
                 std::cerr << "Failed to download shot " << (i+1) << ". Leaving file on camera." << std::endl;
                 // Even if download fails, we probably want to try delete to clear buffer
                 std::this_thread::sleep_for(std::chrono::milliseconds(200));
                 delete_file_on_camera(camera, camera_file_path);
            }
            
            // 4. Close Camera Session
            close_camera(camera);
            
            // Wait to let camera stabilize
            std::cout << "[Sequence] Shot " << (i+1) << "/" << count << " complete. Waiting 3s..." << std::endl;
            std::this_thread::sleep_for(std::chrono::milliseconds(3000));
        }
    };

    if (data["shots"].is_array()) {
        for (const auto& shot : data["shots"]) {
            process_shot_config(shot);
        }
    } else if (data["shots"].is_object()) {
        process_shot_config(data["shots"]);
    } else {
        std::cerr << "'shots' must be an object or an array." << std::endl;
    }
    
    std::cout << "\n--- JSON SEQUENCE COMPLETE ---\n" << std::endl;
}

int main() {
    // Note: camera setup is now handled inside the sequence loop
    std::cout << "NovaCommand: Camera Control System" << std::endl;

    // Run Sequence (if file exists) or fallback to legacy test shot
    if (fs::exists("sequence.json")) {
        run_json_sequence("sequence.json");
    } else {
        // Legacy/Fallback (still needs manual setup if used)
        Camera *camera;
        if (setup_camera(&camera) == GP_OK) {
            save_device_summary(camera);
            std::cout << "No sequence.json found. Running test shot..." << std::endl;
            test_shot(camera);
            close_camera(camera);
        }
    }

    return 0;
}
