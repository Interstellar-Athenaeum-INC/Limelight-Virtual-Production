// LIMELIGHT ESP Node
// --
// ESP32 Artnet Node with FastLED
// Intended to be used with the Limelight plugin for Unreal Engine
//
// Script preset for SK6812 LED strips
/*
  MIT License

  Copyright (c) 2025 Interstellar Athenaeum INC.

  Permission is hereby granted, free of charge, to any person obtaining a copy
  of this software and associated documentation files (the "Software"), to deal
  in the Software without restriction, including without limitation the rights
  to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
  copies of the Software, and to permit persons to whom the Software is
  furnished to do so, subject to the following conditions:

  The above copyright notice and this permission notice shall be included in all
  copies or substantial portions of the Software.

  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
  AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
  OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
  SOFTWARE.
*/

#include <ArtnetWiFi.h>
#include <WiFi.h>
#include <WiFiUdp.h>
#include <FastLED.h>
#include "FastLED_RGBW.h"

// Hardware Setup
#define CHIPSET WS2812B // Leave as WS2812B for SK6812
#define DATA_PIN 4
#define ORDER EOrder::RGB

// WIFI Config
const char* ssid = "NAME";
const char* password = "PASSWORD";
const IPAddress ip(192, 0, 0, 1);
const IPAddress gateway(192, 168, 1, 1);
const IPAddress subnet(255, 255, 255, 0);

// LED Fixture Setup
const int numLEDs = 6; // Defines the number of LEDs on the light strip
const int ledPacketSize = 3; // Defines the number of channels in a light packet (R, G, B)

/* - Custom Mode - 
 * Useful when you want to set the fixture mode using a custom master channel
 * in Limelight. Enabling this mode will set the mode using the first two bytes
 * received by ArtNet.
 */
const bool customMode = false;

/* - ArtNet Universe -
 *  Define the universe this fixture receives data on (0-15)
 */
uint8_t universe = 0;

// FastLED RGB
CRGBW leds[numLEDs];
CRGB * ledsRGB = (CRGB *) &leds[0];

// ARTNET
ArtnetWiFiReceiver artnet;

struct ArtPollReplyMetadata
{
    uint16_t oem {0x00FF};      // OemUnknown https://github.com/tobiasebsen/ArtNode/blob/master/src/Art-NetOemCodes.h
    uint16_t esta_man {0x0000}; // ESTA manufacturer code
    uint8_t status1 {0x00};     // Unknown / Normal
    uint8_t status2 {0x08};     // sACN capable
    String short_name {"Limelight ArtNet"};
    String long_name {"Limelight ArtNet Node for Unreal Engine Virtual Production"};
    String node_report {""};
    // Four universes from Device to Controller
    // NOTE: Only low 4 bits of the universes
    // NOTE: Upper 11 bits of the universes will be
    //       shared with the subscribed universe (net, subnet)
    // e.g.) If you have subscribed universe 0x1234,
    //       you can set the device to controller universes
    //       from 0x1230 to 0x123F (sw_in will be from 0x0 to 0xF).
    //       So, I recommned subscribing only in the range of
    //       0x1230 to 0x123F if you want to set the sw_in.
    // REF: Please refer the Art-Net spec for the detail.
    //      https://art-net.org.uk/downloads/art-net.pdf
    uint8_t sw_in[4] {0};
};

// Connect
bool connectWiFi(void)
{
  bool state = true;
  int i = 0;
  WiFi.begin(ssid, password);
  WiFi.config(ip, gateway, subnet);

  Serial.println("");
  Serial.println("Connecting");
  while(WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");

    // WiFi Status Indicator
    FastLED.clear();
    leds[(i % numLEDs)] = CRGB(0,150,0);
    FastLED.show();

    if(i > 50) {
      state = false;

      // Error
      FastLED.showColor(CRGB(150,0,0));
      delay(50);
      FastLED.showColor(CRGB(0,0,0));
      delay(50);
      FastLED.showColor(CRGB(150,0,0));
      delay(50);
      FastLED.showColor(CRGB(0,0,0));

      break; // TIMEOUT
    }
    i++;
  } 
  if(state) {
    Serial.println("");
    Serial.print("Connected to ");
    Serial.println(ssid);
    Serial.print("IP address: ");
    Serial.println(WiFi.localIP());
  } else {
    Serial.println("");
    Serial.println("Connection failed.");
  }

  return state;
}

void setup() {

  delay(2000); // Safety Startup Delay

  enum FixtureMode {
    NONE, ALPHA, TEMP, FILL
  };

  FastLED.addLeds<CHIPSET, DATA_PIN, ORDER>(ledsRGB, getRGBWsize(numLEDs));
  FastLED.clear();
  FastLED.show();

  Serial.begin(115200);
  if(connectWiFi()) {
      FastLED.showColor(CRGB(0,150,0));
      delay(50);
      FastLED.showColor(CRGB(0,0,0));
      delay(50);
      FastLED.showColor(CRGB(0,150,0));
      delay(50);
      FastLED.showColor(CRGB(0,0,0));
  }

  artnet.begin();

  // Configuration
  ArtPollReplyConfig artnetConfig;
  const ArtPollReplyMetadata artnetConfigMeta;
  artnetConfig.oem = artnetConfigMeta.oem;
  artnetConfig.esta_man = artnetConfigMeta.esta_man;
  artnetConfig.status1 = artnetConfigMeta.status1;
  artnetConfig.status2 = artnetConfigMeta.status2;
  artnetConfig.short_name = artnetConfigMeta.short_name;
  artnetConfig.long_name = artnetConfigMeta.long_name;
  artnetConfig.node_report = artnetConfigMeta.node_report;
  artnet.setArtPollReplyConfig(artnetConfig);

  // Execute DMX frame on packet receive
  artnet.subscribeArtDmxUniverse(universe, [](const uint8_t *data, uint16_t size, const ArtDmxMetadata& metadata, const ArtNetRemoteInfo& remote)
  {
    // Read Universe and put into display buffer
    uint16_t dataPacketSize = customMode ? (size - 2) : size; // Size of LED specific data
    uint16_t ledsInPacket = dataPacketSize / ledPacketSize;
    if(ledsInPacket == 0) { return; } // Check if LED data exists

    // Define Mode
    uint16_t ledPacketStartIndex = 0;
    uint8_t Alpha = 0;
    uint8_t Mode = 0;
    if(customMode) { // Custom mode
        Alpha = data[0]; // Value
        Mode = data[1]; // Mode
        ledPacketStartIndex += 2;
    }

    // Define number of LEDs to fill
    uint8_t fillLeds = ((Mode == FILL) ? Alpha : 1);

    // For each LED data packet
    int pixIndex = 0;
    for (int i = 0; i < ledsInPacket; i++)
    {

      // Obtain data
      size_t idx = (i * ledPacketSize) + ledPacketStartIndex; 
      uint8_t R = (idx < size) ? data[idx] : 0;
      uint8_t G = ((idx + 1) < size) ? data[idx+1] : 0;
      uint8_t B = ((idx + 2) < size) ? data[idx+2] : 0;

      // Fill if enabled
      for (int j = 0; j < fillLeds; j++) {

        // Max Pixel Reached
        if(pixIndex >= numLEDs) {return;}

        // Mode
        if(Mode == ALPHA) { // Alpha Mode
          nscale8x3(R,G,B,Alpha);

        } else if(Mode == TEMP) { // Temperature Mode
          leds[pixIndex] = CRGBW(0, 0, 0, Alpha);
          pixIndex++;
          continue;
        }

        leds[pixIndex] = CRGBW(R, G, B, 0);
        pixIndex++;
      }
    }

    FastLED.show(); // Display buffer
  });
}

void loop() {
  artnet.parse();
}