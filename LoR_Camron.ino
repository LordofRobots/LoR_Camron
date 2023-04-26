/* LORD of ROBOTS - CAMRON - 202304080040
  This code is an ESP32-based camera robot controller that enables users to control a robot's movement and an LED light via a web interface while streaming live video from the onboard camera. The code includes:
    1. Necessary libraries and pin definitions for the ESP32 camera and robot control.
    2. WiFi configuration to connect to a network.
    3. An HTML web page with a control interface for robot movement (forward, backward, left, right) and LED light control (on/off).
    4. JavaScript code for sending commands to the robot based on user input and keyboard events.
    5. HTTP handlers for serving the web page, processing robot movement commands, and streaming the camera feed.
    6. Functions for starting the camera server and converting MAC addresses to string format.
    7. The Arduino setup function, which initializes the serial communication, camera configuration, camera sensor settings, and WiFi connection.
    8. The Arduino loop function, which listens for incoming connections and broadcasts the device's IP address.
  Overall, this code allows users to control a robot with an ESP32-based camera over a web interface, while also viewing the live video stream from the robot's camera.
*/
#include "esp_camera.h"
#include <WiFi.h>
#include "img_converters.h"
#include "soc/soc.h"           // disable brownout problems
#include "soc/rtc_cntl_reg.h"  // disable brownout problems
#include "esp_http_server.h"
#include <ESPmDNS.h>
#include <esp_system.h>

// Version control
const String Version = "0.1.0";

// Pin definitions and other constants
#define PART_BOUNDARY "123456789000000000000987654321"
#define PWDN_GPIO_NUM 32
#define RESET_GPIO_NUM -1
#define XCLK_GPIO_NUM 0
#define SIOD_GPIO_NUM 26
#define SIOC_GPIO_NUM 27
#define Y9_GPIO_NUM 35
#define Y8_GPIO_NUM 34
#define Y7_GPIO_NUM 39
#define Y6_GPIO_NUM 36
#define Y5_GPIO_NUM 21
#define Y4_GPIO_NUM 19
#define Y3_GPIO_NUM 18
#define Y2_GPIO_NUM 5
#define VSYNC_GPIO_NUM 25
#define HREF_GPIO_NUM 23
#define PCLK_GPIO_NUM 22

#define LED_OUTPUT 4

// WiFi configuration of SSID
const char *ssid = "CAMRON-";

// Function to convert MAC address to string format. MAC address of the ESP32 in format "XX:XX:XX:XX:XX:XX"
String UniqueID() {
  // Get unique MAC address
  uint8_t mac[6];
  esp_read_mac(mac, ESP_MAC_WIFI_STA);

  // Save MAC address to string
  char buffer[13];
  sprintf(buffer, "%02X%02X%02X%02X%02X%02X", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
  String uniqueID = buffer;
  uniqueID = uniqueID.substring(6, 12);  // limit to last 6 digits

  return uniqueID;
}

const String SystemPassword = String(PasswordGen());
String PasswordGen() {
  String uniqueID = UniqueID();
  String mixedString = "";
  for (int i = 0; i < 6; i++) {
    char uniqueChar = uniqueID.charAt(i);
    char camronChar = "CAMRON"[i];
    int mixedValue = (uniqueChar - '0') + (camronChar - 'A') + i;
    mixedValue %= 16;  // limit to single hex digit
    mixedString += (char)((mixedValue < 10) ? ('0' + mixedValue) : ('A' + mixedValue - 10));
  }
  
  mixedString = "LoR" + mixedString;
  return mixedString;
}

// Global variables for HTTP server instances
static const char *_STREAM_CONTENT_TYPE = "multipart/x-mixed-replace;boundary=" PART_BOUNDARY;
static const char *_STREAM_BOUNDARY = "\r\n--" PART_BOUNDARY "\r\n";
static const char *_STREAM_PART = "Content-Type: image/jpeg\r\nContent-Length: %u\r\n\r\n";

httpd_handle_t camera_httpd = NULL;
httpd_handle_t stream_httpd = NULL;

// Web page (HTML, CSS, JavaScript) for controlling the robot
static const char PROGMEM INDEX_HTML[] = R"rawliteral(
  <html>
    <head>
      <title>LORD of ROBOTS</title>
      <meta name="viewport" content="width=device-width, height=device-height, initial-scale=1" >
      <style>
        body { font-family: Arial; text-align: center; margin:0px auto; padding-top: 30px;}
        .button {
          background-color: #2f4468;
          width: 100px;
          height: 80px;
          border: none;
          color: white;
          font-size: 20px;
          font-weight: bold;
          text-align: center;
          text-decoration: none;
          border-radius: 10px;
          display: inline-block;
          margin: 6px 6px;
          cursor: pointer;
          -webkit-touch-callout: none;
          -webkit-user-select: none;
          -khtml-user-select: none;
          -moz-user-select: none;
          -ms-user-select: none;
          user-select: none;
          -webkit-tap-highlight-color: rgba(0,0,0,0);
        }
        img { 
          width: auto ;
          max-width: 100% ;
          height: auto ; 
        }
      </style>
    </head>
    <body bgcolor="black">
      <h1><p style="color:#FFFFFF";>CAMRON MiniBot</p></h1>
      <img src="" id="photo" >
      <center>
        <table>
          <tr><td colspan="3" align="center"><button class="button" onmousedown="sendData('forward');" ontouchstart="sendData('forward');" onmouseup="sendData('stop');" ontouchend="sendData('stop');">Forward</button></td></tr>
          <tr><td align="center"><button class="button" onmousedown="sendData('left');" ontouchstart="sendData('left');" onmouseup="sendData('stop');" ontouchend="sendData('stop');">Left</button></td><td align="center"><button class="button" onmousedown="sendData('stop');" ontouchstart="sendData('stop');">Stop</button></td><td align="center"><button class="button" onmousedown="sendData('right');" ontouchstart="sendData('right');" onmouseup="sendData('stop');" ontouchend="sendData('stop');">Right</button></td></tr>
          <tr><td align="center"><button class="button" onmousedown="sendData('ledon');" ontouchstart="sendData('ledon');" onmouseup="sendData('stop');" ontouchend="sendData('stop');">LED ON</button></td><td align="center"><button class="button" onmousedown="sendData('backward');" ontouchstart="sendData('backward');" onmouseup="sendData('stop');" ontouchend="sendData('stop');">Backward</button></td><td align="center"><button class="button" onmousedown="sendData('ledoff');" ontouchstart="sendData('ledoff');" onmouseup="sendData('stop');" ontouchend="sendData('stop');">LED OFF</button></td></tr>                   
        </table>
      </center>
      <script>
        function sendData(x) {
          var xhr = new XMLHttpRequest();
          xhr.open("GET", "/action?go=" + x, true);
          xhr.send();
        }

        document.addEventListener('keydown', function(event) {
          if (event.code === 'ArrowUp') {
            sendData('forward');
          } else if (event.code === 'ArrowLeft') {
            sendData('left');
          } else if (event.code === 'ArrowDown') {
            sendData('backward');
          } else if (event.code === 'ArrowRight') {
            sendData('right');
          } else if (event.code === 'KeyW') {
            sendData('forward');
          } else if (event.code === 'KeyA') {
            sendData('left');
          } else if (event.code === 'KeyS') {
            sendData('backward');
          } else if (event.code === 'KeyD') {
            sendData('right');
          } else if (event.code === 'KeyL') {
            sendData('ledon');
          } else if (event.code === 'KeyO') {
            sendData('ledoff');
          }
        });
        document.addEventListener('keyup', function(event) {
          if (event.code.startsWith('Arrow')) {
            sendData('stop');
          } else if (event.code === 'KeyW') {
            sendData('stop');
          } else if (event.code === 'KeyA') {
            sendData('stop');
          } else if (event.code === 'KeyS') {
            sendData('stop');
          } else if (event.code === 'KeyD') {
            sendData('stop');
          }
        });      
        window.onload = document.getElementById("photo").src = window.location.href.slice(0, -1) + ":81/stream";
      </script>
    </body>
  </html>
)rawliteral";

// HTTP handler for serving the web page
static esp_err_t index_handler(httpd_req_t *req) {
  httpd_resp_set_type(req, "text/html");
  return httpd_resp_send(req, (const char *)INDEX_HTML, strlen(INDEX_HTML));
}

// HTTP handler for streaming the camera feed
static esp_err_t stream_handler(httpd_req_t *req) {
  camera_fb_t *fb = NULL;
  esp_err_t res = ESP_OK;
  size_t _jpg_buf_len = 0;
  uint8_t *_jpg_buf = NULL;
  char *part_buf[64];

  res = httpd_resp_set_type(req, _STREAM_CONTENT_TYPE);
  if (res != ESP_OK) {
    return res;
  }

  while (true) {
    fb = esp_camera_fb_get();
    if (!fb) {
      Serial.println("Camera capture failed");
      res = ESP_FAIL;
    } else {
      if (fb->width > 400) {
        if (fb->format != PIXFORMAT_JPEG) {
          bool jpeg_converted = frame2jpg(fb, 80, &_jpg_buf, &_jpg_buf_len);
          esp_camera_fb_return(fb);
          fb = NULL;
          if (!jpeg_converted) {
            Serial.println("JPEG compression failed");
            res = ESP_FAIL;
          }
        } else {
          _jpg_buf_len = fb->len;
          _jpg_buf = fb->buf;
        }
      }
    }
    if (res == ESP_OK) {
      size_t hlen = snprintf((char *)part_buf, 64, _STREAM_PART, _jpg_buf_len);
      res = httpd_resp_send_chunk(req, (const char *)part_buf, hlen);
    }
    if (res == ESP_OK) {
      res = httpd_resp_send_chunk(req, (const char *)_jpg_buf, _jpg_buf_len);
    }
    if (res == ESP_OK) {
      res = httpd_resp_send_chunk(req, _STREAM_BOUNDARY, strlen(_STREAM_BOUNDARY));
    }
    if (fb) {
      esp_camera_fb_return(fb);
      fb = NULL;
      _jpg_buf = NULL;
    } else if (_jpg_buf) {
      free(_jpg_buf);
      _jpg_buf = NULL;
    }
    if (res != ESP_OK) {
      break;
    }
    //delay(20);
  }
  return res;
}

// HTTP handler for processing robot movement commands
static esp_err_t cmd_handler(httpd_req_t *req) {
  char *buf;
  size_t buf_len;
  char variable[32] = {
    0,
  };

  buf_len = httpd_req_get_url_query_len(req) + 1;
  if (buf_len > 1) {
    buf = (char *)malloc(buf_len);
    if (!buf) {
      httpd_resp_send_500(req);
      return ESP_FAIL;
    }
    if (httpd_req_get_url_query_str(req, buf, buf_len) == ESP_OK) {
      if (httpd_query_key_value(buf, "go", variable, sizeof(variable)) == ESP_OK) {
      } else {
        free(buf);
        httpd_resp_send_404(req);
        return ESP_FAIL;
      }
    } else {
      free(buf);
      httpd_resp_send_404(req);
      return ESP_FAIL;
    }
    free(buf);
  } else {
    httpd_resp_send_404(req);
    return ESP_FAIL;
  }

  int res = 0;
  int LED_Max = 50;
  if (!strcmp(variable, "forward")) {
    Serial.println("Forward");
  } else if (!strcmp(variable, "left")) {
    Serial.println("Left");
  } else if (!strcmp(variable, "right")) {
    Serial.println("Right");
  } else if (!strcmp(variable, "backward")) {
    Serial.println("Backward");
  } else if (!strcmp(variable, "stop")) {
    Serial.println("Stop");
  } else if (!strcmp(variable, "ledon")) {
    Serial.println("xON");
    analogWrite(LED_OUTPUT, LED_Max);
  } else if (!strcmp(variable, "ledoff")) {
    Serial.println("xOFF");
    analogWrite(LED_OUTPUT, 0);
  } else {
    Serial.println("Stop");
    res = -1;
  }
  Serial.println("\n");

  if (res) {
    return httpd_resp_send_500(req);
  }

  httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
  return httpd_resp_send(req, NULL, 0);
}

// Function to start the camera server
void startCameraServer() {
  httpd_config_t config = HTTPD_DEFAULT_CONFIG();
  config.server_port = 80;
  httpd_uri_t index_uri = {
    .uri = "/",
    .method = HTTP_GET,
    .handler = index_handler,
    .user_ctx = NULL
  };

  httpd_uri_t cmd_uri = {
    .uri = "/action",
    .method = HTTP_GET,
    .handler = cmd_handler,
    .user_ctx = NULL
  };
  httpd_uri_t stream_uri = {
    .uri = "/stream",
    .method = HTTP_GET,
    .handler = stream_handler,
    .user_ctx = NULL
  };
  if (httpd_start(&camera_httpd, &config) == ESP_OK) {
    httpd_register_uri_handler(camera_httpd, &index_uri);
    httpd_register_uri_handler(camera_httpd, &cmd_uri);
  }
  config.server_port += 1;
  config.ctrl_port += 1;
  if (httpd_start(&stream_httpd, &config) == ESP_OK) {
    httpd_register_uri_handler(stream_httpd, &stream_uri);
  }
}

// Arduino setup function
void setup() {
  // Disable brownout detector
  WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0);  //disable brownout detector

  // Set up LED output pin
  pinMode(LED_OUTPUT, OUTPUT);

  // Setup serial comms
  Serial.begin(115200);
  Serial.setDebugOutput(false);

  // Configure the camera
  camera_config_t config;
  config.ledc_channel = LEDC_CHANNEL_0;
  config.ledc_timer = LEDC_TIMER_0;
  config.pin_d0 = Y2_GPIO_NUM;
  config.pin_d1 = Y3_GPIO_NUM;
  config.pin_d2 = Y4_GPIO_NUM;
  config.pin_d3 = Y5_GPIO_NUM;
  config.pin_d4 = Y6_GPIO_NUM;
  config.pin_d5 = Y7_GPIO_NUM;
  config.pin_d6 = Y8_GPIO_NUM;
  config.pin_d7 = Y9_GPIO_NUM;
  config.pin_xclk = XCLK_GPIO_NUM;
  config.pin_pclk = PCLK_GPIO_NUM;
  config.pin_vsync = VSYNC_GPIO_NUM;
  config.pin_href = HREF_GPIO_NUM;
  config.pin_sscb_sda = SIOD_GPIO_NUM;
  config.pin_sscb_scl = SIOC_GPIO_NUM;
  config.pin_pwdn = PWDN_GPIO_NUM;
  config.pin_reset = RESET_GPIO_NUM;
  config.xclk_freq_hz = 20000000;
  config.pixel_format = PIXFORMAT_JPEG;

  // Set frame size and quality based on available memory
  if (psramFound()) {
    config.frame_size = FRAMESIZE_XGA;  // FRAMESIZE_ + QVGA|CIF|VGA|SVGA|XGA|SXGA|UXGA
    config.jpeg_quality = 40;           //10-63 lower number means higher quality
    config.fb_count = 2;
  } else {
    config.frame_size = FRAMESIZE_XGA;
    config.jpeg_quality = 40;
    config.fb_count = 1;
  }

  // Initialize the camera
  esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK) {
    Serial.printf("Camera init failed with error 0x%x", err);
    return;
  }

  // Get camera sensor settings
  sensor_t *s = esp_camera_sensor_get();
  // Set camera sensor parameters
  s->set_brightness(s, 0);                  // -2 to 2
  s->set_contrast(s, 0);                    // -2 to 2
  s->set_saturation(s, 0);                  // -2 to 2
  s->set_special_effect(s, 0);              // 0 to 6 (0 - No Effect, 1 - Negative, 2 - Grayscale, 3 - Red Tint, 4 - Green Tint, 5 - Blue Tint, 6 - Sepia)
  s->set_whitebal(s, 1);                    // 0 = disable , 1 = enable
  s->set_awb_gain(s, 1);                    // 0 = disable , 1 = enable
  s->set_wb_mode(s, 0);                     // 0 to 4 - if awb_gain enabled (0 - Auto, 1 - Sunny, 2 - Cloudy, 3 - Office, 4 - Home)
  s->set_exposure_ctrl(s, 1);               // 0 = disable , 1 = enable
  s->set_aec2(s, 0);                        // 0 = disable , 1 = enable
  s->set_ae_level(s, 0);                    // -2 to 2
  s->set_aec_value(s, 300);                 // 0 to 1200
  s->set_gain_ctrl(s, 1);                   // 0 = disable , 1 = enable
  s->set_agc_gain(s, 0);                    // 0 to 30
  s->set_gainceiling(s, (gainceiling_t)0);  // 0 to 6
  s->set_bpc(s, 0);                         // 0 = disable , 1 = enable
  s->set_wpc(s, 1);                         // 0 = disable , 1 = enable
  s->set_raw_gma(s, 1);                     // 0 = disable , 1 = enable
  s->set_lenc(s, 1);                        // 0 = disable , 1 = enable
  s->set_hmirror(s, 0);                     // 0 = disable , 1 = enable
  s->set_vflip(s, 0);                       // 0 = disable , 1 = enable
  s->set_dcw(s, 1);                         // 0 = disable , 1 = enable
  s->set_colorbar(s, 0);                    // 0 = disable , 1 = enable


  // Wi-Fi connection
  // Set up access point with SSID "Camron" + MAC address

  WiFi.mode(WIFI_AP);
  String ssid_with_mac = ssid + UniqueID();
  WiFi.softAP(ssid_with_mac.c_str(), SystemPassword);

  // Set up mDNS responder
  if (!MDNS.begin("robot")) {
    Serial.println("Error setting up MDNS responder!");
  }
  MDNS.addService("http", "tcp", 80);

  Serial.println("WiFi start");
  delay(3000);



  Serial.println("System Ready! Version = " + Version + " : Robot ID = " + ssid_with_mac + " : Password = " + SystemPassword + "\n");

  // Start streaming web server
  startCameraServer();
}

// Arduino main loop
void loop() {
}
