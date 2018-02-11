
#include <Arduino.h>

#include <TFT_eSPI.h> // Hardware-specific library
#include <SPI.h>
#include <Wire.h>      // this is needed even tho we aren't using it
#include <EEPROM.h>
#include <ESP8266WebServer.h>
#include <ESP8266WiFi.h>
#include <ESP8266mDNS.h>
#include <WiFiManager.h>
#include <ESP8266WiFiMulti.h>
#include <ESP8266HTTPClient.h>
#include <ArduinoJson.h>
#include <FS.h>

#define userAgent "MCVApp/1.5.2 (iPhone; iOS 9.1; Scale/2.00)"
// the file where your connected drive credentials are
#define configFile "/cdr.dat"
// the file where the bmw api key is stored
#define apiCredFile "/cdc.dat"

// some html for config
const char HTTP_CONFIG_OPTIONS[] PROGMEM  = "<h1>Connected Drive Credentials</h1><form action=\"/cd_save\"method=\"post\" enctype=\"multipart/form-data\">User<input type=\"text\" name=\"user\"><br />Password<input type=\"password\" name=\"pass\"><br /><input class=\"button\" type=\"submit\" value=\"Save\"></form>";
const char HTTP_API_OPTIONS[] PROGMEM  = "<h1>CD Api Key</h1><form action=\"/api_save\"method=\"post\" enctype=\"multipart/form-data\">User<input type=\"text\" name=\"user\"><br />Password<input type=\"password\" name=\"pass\"><br /><input class=\"button\" type=\"submit\" value=\"Save\"></form>";
const char HTTP_CONFIG_HOME[] PROGMEM  = "<form action=\"/\"method=\"get\"><button>Home</button></form><form action=\"/cd\"method=\"get\"><button>CD Settings</button></form><form action=\"/api\"method=\"get\"><button>API Key</button></form>";

// fingerprint of the tls certificate from b2vapi.bmwgroup.com
const char* fingerprint = "CE E1 D1 E9 D1 B6 69 E4 B9 DC 82 E0 AC 90 29 2D F6 E8 B9 BF";


class CarStatus {
public:
  CarStatus(void) : lvl(0), kmleft(0),charging(false) {}
  CarStatus(int lvl, int kmleft,bool charging) : lvl(lvl), kmleft(kmleft), charging(charging) {}
  int lvl;int kmleft;bool charging;
};

class Credentials {
public:
  Credentials(void) : user(""),pass(""),valid(false){}
  Credentials(String user,String pass,bool valid):user(user),pass(pass),valid(valid){}
  String user;String pass;bool valid;
};

class CarData {
public:
  CarData(void) : vin(""),name(""){}
  CarData(String vin,String name):vin(vin),name(name){}
  String vin;String name;
};

uint16_t _textColor;
uint16_t _backgroundColor;
uint16_t yPos = 20;
WiFiManager wifiManager;
Credentials cdCred;
Credentials apiCred;

TFT_eSPI tft = TFT_eSPI();       // library for controling the display
ESP8266WebServer httpServer;


void configModeCallback (WiFiManager *myWiFiManager);
String loginCDgetToken();
String getCarData(String token, String vin);
CarStatus parseCarData(String json);
CarData getFirstCar(String json);
void queryData();
Credentials loadCredentials(String cfgFile);
void sendHTMLResponseMessage(String message);
void handleSave(String fileName);

void setup() {
  Serial.begin(115200);
  SPIFFS.begin();
  tft.init();
  tft.setRotation(0);
  tft.fillScreen(TFT_BLACK);
  tft.setTextFont(2);
  tft.setTextSize(1);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);

  tft.drawString(String("Connecting to Wifi"), 10, yPos);
  yPos = yPos + 20;

  wifiManager.setAPCallback(configModeCallback);
  wifiManager.autoConnect();

  cdCred = loadCredentials(configFile);
  apiCred = loadCredentials(apiCredFile);

  if ((cdCred.valid == true) && (apiCred.valid==true)) {
    queryData();
  } else {
    tft.drawString( String("No credentials found."),10, yPos);
    yPos = yPos + 20;
    tft.drawString( String("Setup via Browser. Connect to:"),10, yPos);
    yPos = yPos + 20;

    IPAddress myIP = WiFi.localIP();
    String ipStr = String(myIP[0])+"."+String(myIP[1])+"."+String(myIP[2])+"."+String(myIP[3]);

    tft.drawString(ipStr,10, yPos);

  }

  httpServer.begin();
  httpServer.on("/",  [](){sendHTMLResponseMessage(FPSTR(HTTP_CONFIG_HOME));});
  httpServer.on("/cd",  [](){sendHTMLResponseMessage(FPSTR(HTTP_CONFIG_OPTIONS));});
  httpServer.on("/api",  [](){sendHTMLResponseMessage(FPSTR(HTTP_API_OPTIONS));});
  httpServer.on("/cd_save",HTTP_POST, [](){handleSave(configFile);});
  httpServer.on("/api_save",HTTP_POST, [](){handleSave(apiCredFile);});
}

void queryData() {
  yPos = 40;
  tft.drawString( String("Login to CD"),10, yPos);
  yPos = yPos + 20;

  String token = loginCDgetToken();
  if (token != "") {
    tft.drawString(String("seems legit, query car data"),10, yPos);
    yPos = yPos + 20;
    String strcars = getCarData(token,"");
    CarData car = getFirstCar(strcars);
    if (car.vin != "") {
      tft.fillScreen(TFT_BLACK);
      yPos = 20;
      tft.drawString(String(car.name),10, yPos);
      yPos = yPos + 30;

      String result = getCarData(token,car.vin);
      CarStatus cd = parseCarData(result);


      tft.setTextColor(TFT_YELLOW, TFT_BLACK);

      if (cd.lvl > 20) {
        tft.setTextColor(TFT_GREEN, TFT_BLACK);
      }

      if (cd.lvl < 5) {
        tft.setTextColor(TFT_RED, TFT_BLACK);
      }

      tft.setTextDatum(TC_DATUM);
      tft.drawString(String(cd.lvl) + " %",160, yPos,8);
      yPos = yPos + 100;

      tft.setTextColor(TFT_WHITE, TFT_BLACK);

      tft.setTextDatum(TL_DATUM);
      tft.drawString(String("remaining km: ") + String(cd.kmleft), 10, yPos);
      yPos = yPos + 20;

      if (cd.charging) {
        tft.drawString(String("Car is charging ..."), 10, yPos);
        yPos = yPos + 20;
      }
    }

  } else {
    tft.setTextColor(TFT_RED);
    tft.drawString(String("Login Error"),10, yPos);
  }
}

Credentials loadCredentials(String cfgFile) {
  File file = SPIFFS.open(cfgFile,"r");
  if (file) {
    StaticJsonBuffer<1024> jsonBuffer;
    JsonObject &root = jsonBuffer.parseObject(file);
    if (root.success()) {
      Serial.println("Config found");
      String user = root["user"];
      String pass = root["pass"];
      file.close();
      return Credentials(user,pass,true);
    } else {
      Serial.println("Cannot parse data");
    }
    file.close();
  } else {
    Serial.println("No config file found");
  }
  return Credentials("","",false);
}

CarData getFirstCar(String json) {
  Serial.print("JSON:");
  Serial.println(json);
  DynamicJsonBuffer jsonBuffer(4000);
  JsonObject &root = jsonBuffer.parseObject(json);
  String vin = root["vehicles"][0]["vin"];
  String name = root["vehicles"][0]["licensePlate"];
  return CarData(vin,name);
}

CarStatus parseCarData(String json) {
  Serial.print("JSON:");
  Serial.println(json);
  DynamicJsonBuffer jsonBuffer(4000);
  JsonObject &root = jsonBuffer.parseObject(json);
  JsonVariant _vehicleStatus = root["vehicleStatus"];
  int lvl = _vehicleStatus["chargingLevelHv"];
  int km = _vehicleStatus["remainingRangeElectric"];
  String chrg = _vehicleStatus["chargingStatus"];
  bool isCharging = (chrg == "CHARGING");
  Serial.print("Battery :");
  Serial.println(lvl);
  Serial.print("Km left: ");
  Serial.println(km);
  Serial.print("Charging :");
  Serial.println(isCharging);
  return CarStatus(lvl,km,isCharging);
}

void loop() {
  httpServer.handleClient();
}

// Called if WiFi has not been configured yet
void configModeCallback (WiFiManager *myWiFiManager) {
  tft.setTextFont(2);
  tft.setTextColor(TFT_CYAN);
  tft.drawString(String("Wifi Manager"),120, 28);
  tft.drawString(String("Please connect to AP"),120, 50);
  tft.setTextColor(TFT_WHITE);
  tft.drawString( myWiFiManager->getConfigPortalSSID(),120, 72);
  tft.setTextColor(TFT_CYAN);
  tft.drawString(String("To setup Wifi Configuration"),120, 94);
}

String getCarData(String token, String vin) {
  HTTPClient http;
  String url = "https://b2vapi.bmwgroup.com/webapi/v1/user/vehicles/";
  if (vin != "") {
    url = "https://b2vapi.bmwgroup.com/webapi/v1/user/vehicles/"+vin+"/status";
  }

  if (http.begin(url,fingerprint)) {
    http.addHeader("Content-Type", "application/x-www-form-urlencoded; charset=utf-8");
    http.addHeader("Authorization",String("Bearer " + String(token)));
    http.setUserAgent(userAgent);
    int httpCode = http.GET();
    String result = http.getString();
    return result;
  } else {
    Serial.println("Cannot connect");
  }
  return "";
}


// HTTP Methods

String loginCDgetToken() {
  String postData = "grant_type=password&username="+cdCred.user +"&scope=vehicle_data&password=" + cdCred.pass;
  HTTPClient http;
  if (http.begin("https://b2vapi.bmwgroup.com/webapi/oauth/token/",fingerprint)) {
    http.addHeader("Content-Type", "application/x-www-form-urlencoded; charset=utf-8");
    http.setAuthorization(apiCred.user.c_str(),apiCred.pass.c_str());
    http.setUserAgent(userAgent);
    int httpCode = http.POST(postData);
    String result = http.getString();
    StaticJsonBuffer<1024> jsonBuffer;
    JsonObject &root = jsonBuffer.parseObject(result);
    const char* token = root["access_token"];
    Serial.print("Token :");
    Serial.println(token);
    return String(token);
  } else {
    Serial.println("Cannot connect");
  }

  return "";
}

void handleSave(String fileName) {
  if( ! httpServer.hasArg("user") || ! httpServer.hasArg("pass")
  || httpServer.arg("user") == NULL || httpServer.arg("pass") == NULL) { // If the POST request doesn't have username and password data
    httpServer.send(400, "text/plain", "400: Invalid Request");         // The request is invalid, so send HTTP status 400
    return;
  }

StaticJsonBuffer<400> jsonBuffer;

JsonObject& root = jsonBuffer.createObject();
root["user"] = httpServer.arg("user");
root["pass"] = httpServer.arg("pass");

if (SPIFFS.exists(fileName) == true) {
  Serial.println("remove old config file");
  SPIFFS.remove(fileName);
}

File file = SPIFFS.open(fileName,"w+");
if (!file) {
  Serial.println("file creation failed");
}
Serial.println("File created");
root.printTo(file);
file.close();
Serial.println("Saved..");

httpServer.sendHeader("Location","/");      // Redirect the client to the success page
httpServer.send(303);

cdCred = loadCredentials(configFile);
apiCred = loadCredentials(apiCredFile);

if ((cdCred.valid == true) && (apiCred.valid == true)) {
  queryData();
}

}

// We reuse some stuff from WifiManager
void sendHTMLResponseMessage(String message) {
  String page = FPSTR(HTTP_HEAD);
  page.replace("{v}", "i3 Display");
  page += FPSTR(HTTP_SCRIPT);
  page += FPSTR(HTTP_STYLE);
  page += FPSTR(HTTP_HEAD_END);
  page += "<h1>i3 Display</h1>";
  page += "<br />";
  page += message;
  page += "<br />";
  page += FPSTR(HTTP_END);
  httpServer.sendHeader("Content-Length", String(page.length()));
  httpServer.send ( 200, "text/html", page);
}
