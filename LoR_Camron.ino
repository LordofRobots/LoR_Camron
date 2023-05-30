/* LORD of ROBOTS - LoR CAMRON - 202304080040 
    - connect to "CAMRON-FFFFFF" wifi access point ("FFFFFF" = the unique ID of the board)
    - use password obtained in serial monitor at startup
    - in a brower on pc or mobile navigate to "http://robot.local"
    - view robot POV and teleop with keys or buttons
*/

// Include necessary libraries
#include "esp_camera.h"
#include "img_converters.h"
#include "soc/soc.h"           // disable brownout problems
#include "soc/rtc_cntl_reg.h"  // disable brownout problems
#include "esp_http_server.h"
#include <ESPmDNS.h>
#include <WiFi.h>
#include <esp_system.h>
#include "esp_wifi.h"

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

// Version control
const String Version = "V0.2.0";

// WiFi configuration of SSID
const char *ssid = "CAMRON-";

// Global variables for HTTP server instances
static const char *_STREAM_CONTENT_TYPE = "multipart/x-mixed-replace;boundary=" PART_BOUNDARY;
static const char *_STREAM_BOUNDARY = "\r\n--" PART_BOUNDARY "\r\n";
static const char *_STREAM_PART = "Content-Type: image/jpeg\r\nContent-Length: %u\r\n\r\n";

httpd_handle_t camera_httpd = NULL;
httpd_handle_t stream_httpd = NULL;

// Function to convert MAC address to string format. MAC address of the ESP32 in format "XX:XX:XX:XX:XX:XX"
String UniqueID() {
  uint8_t mac[6];
  esp_read_mac(mac, ESP_MAC_WIFI_STA);  // Get unique MAC address
  char buffer[13];                      // Save MAC address to string
  sprintf(buffer, "%02X%02X%02X%02X%02X%02X", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
  String uniqueID = buffer;
  uniqueID = uniqueID.substring(6, 12);  // limit to last 6 digits
  return uniqueID;
}

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
const String SystemPassword = String(PasswordGen());

// Web page (HTML, CSS, JavaScript) for controlling the robot
static const char PROGMEM INDEX_HTML[] = R"rawliteral(
  <html>
  <head>
    <title>LORD of ROBOTS</title>
    <meta name="viewport" content="width=device-width, height=device-height, initial-scale=1" >
    <style>
      body { font-family: Arial; text-align: center; margin:0 auto; padding-top: 30px;}
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
        -webkit-tap-highlight-color: rgba(0,0,0,0);
        -webkit-user-select: none; /* Chrome, Safari, Opera */
        -moz-user-select: none; /* Firefox all */
        -ms-user-select: none; /* IE 10+ */
        user-select: none; /* Likely future */
      }
      img { width: auto; max-width: 100%; height: auto; }
      #buttons { text-align: center; }
    </style>
  </head>
  <body style="background-color:black;" oncontextmenu="return false;">
    <h1 style="color:white">CAMRON MiniBot</h1>
    <img src="" id="photo">
    <div id="buttons">
      <button class="button" onpointerdown="sendData('forward')" onpointerup="releaseData()">Forward</button><br>
      <button class="button" onpointerdown="sendData('left')" onpointerup="releaseData()">Left</button>
      <button class="button" onpointerdown="sendData('stop')" onpointerup="releaseData()">Stop</button>
      <button class="button" onpointerdown="sendData('right')" onpointerup="releaseData()">Right</button><br>
      <button class="button" onpointerdown="sendData('ledon')" onpointerup="releaseData()">LED ON</button>
      <button class="button" onpointerdown="sendData('backward')" onpointerup="releaseData()">Backward</button>
      <button class="button" onpointerdown="sendData('ledoff')" onpointerup="releaseData()">LED OFF</button>
 </div>
    <script>
      var isButtonPressed = false; // Add this flag

      function sendData(x) {
        var xhr = new XMLHttpRequest();
        xhr.open("GET", "/action?go=" + x, true);
        xhr.send();
      }

      function releaseData() {
        isButtonPressed = false; // A button has been released
        sendData('stop');
      }

      const keyMap = {
        'ArrowUp': 'forward',
        'ArrowLeft': 'left',
        'ArrowDown': 'backward',
        'ArrowRight': 'right',
        'KeyW': 'forward',
        'KeyA': 'left',
        'KeyS': 'backward',
        'KeyD': 'right',
        'KeyL': 'ledon',
        'KeyO': 'ledoff'
      };

      document.addEventListener('keydown', function(event) {
        if (!isButtonPressed) { // Only send data if no button is being pressed
          const action = keyMap[event.code];
          if (action) sendData(action);
          isButtonPressed = true; // A button has been pressed
        }
      });

      document.addEventListener('keyup', function(event) {
         releaseData();
      });

      window.onload = function() {
        document.getElementById("photo").src = window.location.href.slice(0, -1) + ":81/stream";
      }
    </script>
  </body>
</html>
)rawliteral";

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
    vTaskDelay(40 / portTICK_PERIOD_MS);
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
  //Serial.println("\n");

  if (res) {
    return httpd_resp_send_500(req);
  }

  httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
  return httpd_resp_send(req, NULL, 0);
}


void WifiSetup() {
  // Wi-Fi connection
  // Set up access point with SSID "Camron" + MAC address
  WiFi.mode(WIFI_AP);
  esp_wifi_set_max_tx_power(78);

  String ssid_with_mac = ssid + UniqueID();
  unsigned long seed = UniqueID().substring(UniqueID().length() - 6).toInt();  // Assuming UniqueID has at least 8 characters
  randomSeed(seed);
  int Channel = random(1, 11);
  WiFi.softAP(ssid_with_mac.c_str(), SystemPassword, Channel);
  // Set up mDNS responder
  if (!MDNS.begin("robot")) Serial.println("Error setting up MDNS responder!");
  MDNS.addService("http", "tcp", 80);
  Serial.println("WiFi start");
  delay(3000);
  Serial.println("CAMRON System Ready! Version = " + Version + " : Robot ID = " + ssid_with_mac + " : Password = " + SystemPassword + "\n");
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


  if (psramFound()) {                   // Set frame size and quality based on available memory
    config.frame_size = FRAMESIZE_SVGA;  // FRAMESIZE_ + QVGA|CIF|VGA|SVGA|XGA|SXGA|UXGA
    config.jpeg_quality = 40;           //10-63 lower number means higher quality
    config.fb_count = 1;                // frame buffer count
  } else {
    config.frame_size = FRAMESIZE_SVGA;
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
  s->set_bpc(s, 1);                         // 0 = disable , 1 = enable
  s->set_wpc(s, 1);                         // 0 = disable , 1 = enable
  s->set_raw_gma(s, 1);                     // 0 = disable , 1 = enable
  s->set_lenc(s, 1);                        // 0 = disable , 1 = enable
  s->set_hmirror(s, 0);                     // 0 = disable , 1 = enable
  s->set_vflip(s, 0);                       // 0 = disable , 1 = enable
  s->set_dcw(s, 1);                         // 0 = disable , 1 = enable
  s->set_colorbar(s, 0);                    // 0 = disable , 1 = enable


  WifiSetup();


  // Start streaming web server
  startCameraServer();
}

void loop() {
  // Loop function here...
}
