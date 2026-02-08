 To take a picture with your Sony a6400 using libusb and control it via a JSON file, you need to bridge the gap between "detecting a USB device"
 and "speaking the camera's language."

 Here is the conceptual roadmap of what needs to happen:

 ### 1. Speaking the Language: PTP (Picture Transfer Protocol)

 Right now, your code just sees a generic USB device. To take a picture, you need to send specific data packets that the camera understands as
 commands. This standard is called PTP.

 - Endpoints: You need to identify the Bulk In (reading) and Bulk Out (writing) endpoints on the USB device.
 - Command Structure: A PTP command is a specific sequence of bytes (Container Length, Packet Type, Operation Code, Transaction ID, Parameters).
 - The "Capture" OpCode: For Sony cameras, you send a specific Operation Code (OpCode) like 0x9207 (Sony's custom "Capture" command) or the
 standard PTP 0x100E (InitiateCapture).

 The Flow:
 1. Open Session: Send OpenSession command with a Session ID.
 2. Send Trigger: Send the Capture command.
 3. Read Response: The camera sends back "OK" or "Busy".
 4. Await Event: The camera might send an "ObjectAdded" event (meaning the photo is saved).

 ### 2. The JSON "Live" Controller

 To achieve your goal of editing a JSON file live to control the camera, your C++ program needs to change from a "run-once script" to a "daemon"
 or "service loop."

 - The Loop: The program will run in an infinite loop (while(true)).
 - File Monitoring: inside the loop, it checks the "last modified" timestamp of your config.json file.
 - Hot-Reloading:
     1. If the file timestamp changes, read and parse the JSON.
     2. Extract camera_serial and exposure_times.
     3. If exposure_times is new, queue up a sequence of PTP commands.
 - Execution: The loop processes the queue:
     - Set Shutter Speed (PTP Property Set).
     - Trigger Capture (PTP Command).
     - Wait for duration (Sleep).
     - Stop Capture (if in Bulb mode).

 ### 3. Exposure Control Challenges

 Setting exposure isn't just sending "5 seconds."
 - Lookup Tables: Cameras use index values. You don't send 1/100, you send an ID like 0x0045.
 - Bulb Mode: For exposures longer than 30s (astrophotography), you typically set the camera to "BULB" mode, send a "Start Capture" command, wait
 for your software timer, and then send a "Stop Capture" command.

 ### Summary of Work Required

 1. Library Upgrade: You likely want to use libgphoto2 instead of raw libusb for this. Writing a raw PTP stack over libusb is complex (handling
 transaction IDs, endianness, vendor quirks). libgphoto2 already knows "Sony a6400 Capture Command."
 2. JSON Parser: Include a C++ JSON library (like nlohmann/json).
 3. State Machine: Build a loop that watches the file and manages the camera state (Idle -> Capturing -> Downloading -> Idle)
