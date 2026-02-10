# NovaCommand Code Explanation Report

This document details the functionality, API usage, and design rationale for the `NovaCommand` camera control software. The software uses `libgphoto2` to interface with supported cameras via PTP (Picture Transfer Protocol) over USB.

**Date:** 2026-02-10
**File:** `main.cpp`
**Dependencies:** `libgphoto2`, `nlohmann/json`

---

## 1. Global Helpers & Includes

```cpp
#include <gphoto2/gphoto2-camera.h>
#include <gphoto2/gphoto2.h>
#include "json.hpp"
```
- **Why:** `gphoto2.h` provides the core API. `json.hpp` is a single-header library for parsing the `sequence.json` configuration file.

```cpp
#define CHECK_GP(func, msg) ...
```
- **Why:** A macro to simplify error checking. If a GPhoto function returns a value `< GP_OK` (which is 0), it prints an error and returns the code.

```cpp
GPContext *context;
```
- **Why:** The `GPContext` is a global structure required by almost all libgphoto2 functions. It handles error reporting, progress updates, and cancelability. We initialize it once per session (or per shot in the current robust implementation).

---

## 2. Core Functions

### `setup_camera(Camera **camera)`

**Purpose:** Establishes the initial connection to the camera.

**Detailed Breakdown:**
1.  `context = gp_context_new();`
    - Allocates a new context instance. This is the "environment" for the camera session.
2.  `gp_context_set_error_func(..., error_callback, ...)`
    - Registers a callback to catch internal library errors (debug logging).
3.  `gp_camera_new(camera);`
    - Allocates memory for the `Camera` structure. It does *not* connect yet.
4.  `gp_camera_init(*camera, context);`
    - **Crucial Step.** Scans USB ports, finds the first compatible camera, claims the USB interface, and starts the PTP session.
    - If this fails, no camera is detected or it's busy.

---

### `set_config_value(Camera *camera, const char *key, const char *value)`

**Purpose:** Changes a specific setting (e.g., "iso", "shutterspeed") on the camera.

**Detailed Breakdown:**
1.  `gp_camera_get_config(camera, &widget, context);`
    - Fetches the *entire* configuration tree from the camera. This is a hierarchical structure of widgets.
2.  `_lookup_widget(widget, key, &child);`
    - Recursively searches the tree for a widget with the name `key` (e.g., "iso").
3.  `gp_widget_set_value(child, value);`
    - Updates the value *in the local widget structure*. This does **not** update the camera yet.
4.  `gp_camera_set_config(camera, widget, context);`
    - Sends the modified configuration tree back to the camera. The camera then applies the changes physically.
5.  `gp_widget_free(widget);`
    - Frees the memory allocated for the configuration tree. *Memory Leak Prevention.*

---

### `capture_photo(Camera *camera, CameraFilePath &camera_file_path)`

**Purpose:** Triggers the shutter and waits for the image to be stored in the camera's internal buffer (SDRAM).

**Detailed Breakdown:**
1.  `gp_camera_capture(camera, GP_CAPTURE_IMAGE, &camera_file_path, context);`
    - **The Big One.** Sends the "InitiateCapture" command.
    - `GP_CAPTURE_IMAGE`: Tells it to take a photo (vs. audio/video).
    - **Blocking:** This function blocks execution until the exposure is finished and the camera says "Here is the file."
    - Populates `camera_file_path` with the folder (e.g., `/`) and filename (e.g., `capt0000.jpg`) where the image is stored in the camera's RAM.
2.  `std::this_thread::sleep_for(200ms);`
    - **Stabilization:** A small delay to let the camera's internal state settle before we try to read the file.

---

### `download_photo(..., const std::string &local_filename)`

**Purpose:** Transfers the image from camera RAM to the Linux filesystem.

**Detailed Breakdown:**
1.  `gp_file_new(&file);`
    - Allocates a `CameraFile` structure in PC memory to act as a buffer.
2.  `gp_camera_file_get(..., GP_FILE_TYPE_NORMAL, file, ...);`
    - Pulls the actual binary data from the camera (using the path from `capture_photo`) into the `file` buffer.
3.  `gp_file_save(file, local_filename.c_str());`
    - Writes the buffer contents to the specified path on the disk (e.g., `captures/seq_shot_0001.jpg`).
4.  `gp_file_free(file);`
    - **Critical:** Frees the memory buffer. We do this explicitly to prevent memory leaks, even if saving fails.

---

### `delete_file_on_camera(..., const CameraFilePath &camera_file_path)`

**Purpose:** Deletes the image from the camera's RAM to prevent filling up the buffer.

**Detailed Breakdown:**
1.  **Path Normalization:**
    - Checks if `camera_file_path.folder` is `\` or empty. If so, forces it to `/` to ensure the driver understands the path.
2.  `gp_camera_file_delete(camera, folder, name, context);`
    - Commands the camera to discard the file.
    - Without this, the camera's internal SDRAM buffer fills up (usually after ~30-50 shots depending on model), causing "Disk Full" errors even if no SD card is present.

---

## 3. The Sequence Logic (`run_json_sequence`)

This is the main driver logic added to support your requirements.

**Why the "Nuclear Option"?**
During testing, we encountered segmentation faults (`free(): invalid pointer`) on the 2nd or 3rd shot of a sequence. This is a common issue with `libgphoto2` long-running sessions on certain hardware combinations, where internal state drifts or buffers get corrupted.

**The Solution:**
Instead of opening the camera once and taking 50 photos, we:
1.  **Loop:** For each shot in the sequence...
2.  **Open:** `setup_camera()` (Connect USB)
3.  **Configure:** Apply ISO, Shutter, etc. (Every time, since it's a new session).
4.  **Capture:** Take the photo.
5.  **Download:** Save to disk.
6.  **Delete:** Clear camera RAM.
7.  **Close:** `close_camera()` (Disconnect USB).
8.  **Wait:** Sleep 3 seconds.

This ensures every shot starts from a clean slate, eliminating the segfaults.

**File Naming:**
- Uses `get_next_capture_filename` to scan the `captures/` directory.
- Parses existing files (`seq_shot_XXXX.jpg`) to find the highest number.
- Increments by 1 to ensure no overwrites.

---

## 4. `main()`

**Purpose:** Entry point.
- Checks if `sequence.json` exists.
- If yes -> Calls `run_json_sequence`.
- If no -> Falls back to the legacy `test_shot` routine (single shot).

---
