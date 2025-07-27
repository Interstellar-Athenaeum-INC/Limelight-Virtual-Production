// LIMELIGHT ESP Node
// --
// ESP8266 Artnet Node with FastLED
// Intended to be used with the Limelight plugin for Unreal Engine

#include <ArtnetWiFi.h>
#include <ESP8266WiFi.h>
#include <WiFiUdp.h>
#include <FastLED.h>
#include "FastLED_RGBW.h"

// WIFI Config
const char* ssid = "Galaxy";
const char* password = "andromeda304";

// LED Fixture Setup
const int numLEDs = 4;
const int numberOfChannels = numLEDs * 3; // Number of DMX channels (R, G, B = 3)

// FastLED RGB
CRGBW leds[numLEDs];
CRGB * ledsRGB = (CRGB *) &leds[0];

// ARTNET
ArtnetWiFiReceiver artnet;
uint8_t universe = 5;  // 0 - 15

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
  Serial.println("");
  Serial.println("Connecting");
  while(WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
    if(i > 50) {
      state = false;
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

  enum FixtureMode {
    NONE, ALPHA, TEMP
  };

  Serial.begin(115200);
  connectWiFi();

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

  FastLED.addLeds<WS2812B, D1, EOrder::RGB>(ledsRGB, getRGBWsize(numLEDs));

  // Execute DMX frame on packet receive
  artnet.subscribeArtDmxUniverse(universe, [](const uint8_t *data, uint16_t size, const ArtDmxMetadata& metadata, const ArtNetRemoteInfo& remote)
  {
    // Brightness
    if(universe == 15) {
      FastLED.setBrightness(data[0]);
    }

    // Read Universe and put into display buffer
    for (int i = 0; i < numLEDs; i++)
    {
      size_t idx = i * ((size % 5 == 0) ? 5 : 3); 
      uint8_t R = 0;
      uint8_t G = 0;
      uint8_t B = 0;

      if(idx < size) {R = data[idx+0];} else {R = 0;}
      if((idx + 1) < size) {G = data[idx+1];} else {G = 0;}
      if((idx + 2) < size) {B = data[idx+2];} else {B = 0;}

      // Mode Enabled
      if(size % 5 == 0) {
        uint8_t Alpha = data[idx+3]; // Value
        uint8_t Mode = data[idx+4]; // Mode

        if(Mode == ALPHA) { // Alpha Mode
          uint8_t AlphaInv = 255 - Alpha;
          leds[i] = CRGB(R - AlphaInv, G - AlphaInv, B - AlphaInv);
          continue;

        } else if(Mode == TEMP) { // Temperature Mode
          leds[i] = CRGBW(0, 0, 0, Alpha);
          continue;
        }
      }

      leds[i] = CRGBW(R, G, B, 0);
    }
  });
}

void loop() {
  artnet.parse();
  FastLED.show(); // Display buffer
}
