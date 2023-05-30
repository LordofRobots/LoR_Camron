# LoR_Camron

This Arduino code is for a module that is a companion to the LoR_Core module (with LoR_Core_SerialControl loaded) designed to run on an ESP32cam board for controlling a robot (named "CAMRON") via a web interface. It serves a webpage over a WiFi access point that enables viewing the robot's camera feed and teleoperating the robot through on-screen buttons or keyboard input.

1. **Libraries and Definitions**: It begins by including necessary libraries related to the ESP32, HTTP server, WiFi, and the ESP32's camera. It also defines various pin numbers used to interface with the camera and the LED.

2. **HTML/CSS/JavaScript**: An HTML webpage is embedded as a string within the program, which serves as the user interface. This webpage includes CSS for style definitions and JavaScript for sending control commands to the ESP32 when buttons are pressed or keyboard keys are hit. The webpage also includes an image element that is used to display the live feed from the robot's camera.

3. **WiFi Setup**: The ESP32 sets up a WiFi access point with an SSID composed of "CAMRON-" and the board's unique ID (the last 6 digits of its MAC address). The password is a mix of the unique ID and a string "CAMRON", mapped to hex digits and prefixed with "LoR". It also configures the Multicast DNS (mDNS) to respond to "robot" and add a service for HTTP on port 80.

4. **Camera and HTTP Server Configuration**: Functions are defined to start an HTTP server that handles incoming requests for the root webpage, the camera's video stream, and robot movement commands. The stream handler gets the camera frame, possibly converts it to a smaller JPEG image, and sends it as an HTTP multipart response. The command handler interprets query parameters as commands to control the robot's movement or LED status.

5. **Arduino Setup and Loop**: In the `setup()` function, it initializes the camera, the LED, and the serial interface, then starts the WiFi access point and the HTTP server. The `loop()` function is empty because the HTTP server runs on its own task, separate from the loop. All action is driven by web interactions. 

Commands from the web interface (either button presses or key presses) can instruct the robot to move in four directions or control an onboard LED. The key commands are handled through the JavaScript embedded in the webpage, and the button commands are handled by corresponding HTML button elements.

It's important to note that the motor control is not implemented here. The 'cmd_handler' function is the place to add code for driving the motors, but in this code, it only sends messages to the serial monitor. For a real application, you would replace these messages with calls to functions that drive the robot's motors.
