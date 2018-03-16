
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
#include <NtpClientLib.h>
#include <Time.h>
#include <TimeLib.h>
#include <Timezone.h>

#define userAgent "MCVApp/1.5.2 (iPhone; iOS 9.1; Scale/2.00)"
// the file where your connected drive credentials are
#define configFile "/cdr.dat"
// the file where the bmw api key is stored
#define apiCredFile "/cdc.dat"

// some html for config
const char HTTP_CONFIG_OPTIONS[] PROGMEM  = "<h1>Connected Drive Credentials</h1><form action=\"/cd_save\"method=\"post\" enctype=\"multipart/form-data\">User<input type=\"text\" name=\"user\"><br />Password<input type=\"password\" name=\"pass\"><br /><input class=\"button\" type=\"submit\" value=\"Save\"></form>";
const char HTTP_API_OPTIONS[] PROGMEM  = "<h1>CD Api Key</h1><form action=\"/api_save\"method=\"post\" enctype=\"multipart/form-data\">User<input type=\"text\" name=\"user\"><br />Password<input type=\"password\" name=\"pass\"><br /><input class=\"button\" type=\"submit\" value=\"Save\"></form>";
const char HTTP_CONFIG_HOME[] PROGMEM  = "<form action=\"/\"method=\"get\"><button>Home</button></form><form action=\"/cd\"method=\"get\"><button>CD Settings</button></form><form action=\"/api\"method=\"get\"><button>API Key</button></form>";

const char deDayShortNames_P[] PROGMEM = "FeSoMoDiMiDoFrSa";
#define dedt_SHORT_STR_LEN  2
int UPDATE_INTERVAL_SECS = 1800; // Update Intervall

// fingerprint of the tls certificate from b2vapi.bmwgroup.com
const char* fingerprint = "CE E1 D1 E9 D1 B6 69 E4 B9 DC 82 E0 AC 90 29 2D F6 E8 B9 BF";

#define button1Pin D0

class CarStatus {
public:
  CarStatus(void) : vin(""), name(""), lvl(0), kmleft(0),charging(false),chargingTimeRemaining(0) {}
  CarStatus(String vin,String name, int lvl, int kmleft,bool charging, int chargingTimeRemaining) : vin(vin),name(name) , lvl(lvl), kmleft(kmleft), charging(charging), chargingTimeRemaining(chargingTimeRemaining) {}
  String vin;String name ; int lvl;int kmleft;bool charging;int chargingTimeRemaining;
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
int lastDay = 0;
int lastSecond = -1;
boolean flipDisplay = false;

TFT_eSPI tft = TFT_eSPI();       // library for controling the display
ESP8266WebServer httpServer;
boolean initial = 1;
int timei3 = 0;
long lastUpdate = 0;
long lasti3Update = millis() - 90000;
long lastPressButton1 = 0;
boolean shouldDrawClockFace = false;
CarStatus cd;
CarData car;
CarStatus currentStatus;

float sx = 0, sy = 1, mx = 1, my = 0, hx = -1, hy = 0;    // Saved H, M, S x & y multipliers
float sdeg=0, mdeg=0, hdeg=0;
uint16_t osx=120, osy=120, omx=120, omy=120, ohx=120, ohy=120;  // Saved H, M, S x & y coords
uint16_t x0=0, x1=0, yy0=0, yy1=0;
uint32_t targetTime = 0;                    // for next 1 second timeout

// Central European Time (Frankfurt, Paris)
TimeChangeRule CEST = {"CEST", Last, Sun, Mar, 2, 120};     // Central European Summer Time
TimeChangeRule CET = {"CET ", Last, Sun, Oct, 3, 60};       // Central European Standard Time
Timezone tz_cet(CEST, CET);


char* deDayShortStr(uint8_t day)
{
   static char debuffer[dt_MAX_STRING_LEN+1];
   uint8_t index = day*dedt_SHORT_STR_LEN;
   for (int i=0; i < dedt_SHORT_STR_LEN; i++) {
      debuffer[i] = pgm_read_byte(&(deDayShortNames_P[index + i]));
   }
   debuffer[dedt_SHORT_STR_LEN] = 0;
   return debuffer;
}

void configModeCallback (WiFiManager *myWiFiManager);
String loginCDgetToken();
String getCarData(String token, String vin);
CarStatus parseCarData(CarData car, String json);
CarData getFirstCar(String json);
void queryData();
Credentials loadCredentials(String cfgFile);
void sendHTMLResponseMessage(String message);
void handleSave(String fileName);
void drawClock();
void updateClock();
void showData();

void setup() {
  Serial.begin(115200);
  SPIFFS.begin();
  tft.init();
  tft.setRotation(0);
  tft.fillScreen(TFT_BLACK);
  tft.setTextFont(2);
  tft.setTextSize(1);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  NTP.begin("es.pool.ntp.org", 0 , true); // get time from NTP server pool.
  NTP.setInterval(63);
  String hostname("PEN_BOX");
  hostname += String(ESP.getChipId(), HEX);

  MDNS.begin(hostname.c_str());

  pinMode(button1Pin, INPUT_PULLUP);

  tft.drawString(String("Connecting to Wifi"), 10, yPos);
  yPos = yPos + 20;

  wifiManager.setAPCallback(configModeCallback);
  wifiManager.autoConnect();

  cdCred = loadCredentials(configFile);
  apiCred = loadCredentials(apiCredFile);

  if ((cdCred.valid == true) && (apiCred.valid==true)) {



  } else {
      tft.setTextDatum(TC_DATUM);
    tft.drawString( String("No credentials found."),110, yPos);
    yPos = yPos + 20;
    tft.drawString( String("Setup via Browser. Connect to:"),110, yPos);
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

  drawClock();
}

void drawClock() {

  int yStart = 120;

  if (flipDisplay == true) {
      yStart = 200;
  }


  tft.fillScreen(TFT_BLACK);
  tft.fillCircle(120, yStart, 118, TFT_BLUE);
  tft.fillCircle(120, yStart, 110, TFT_BLACK);
  // Draw 12 lines
  for(int i = 0; i<360; i+= 30) {
    sx = cos((i-90)*0.0174532925);
    sy = sin((i-90)*0.0174532925);
    x0 = sx*114+120;
    yy0 = sy*114+yStart;
    x1 = sx*100+120;
    yy1 = sy*100+yStart;

    tft.drawLine(x0, yy0, x1, yy1, TFT_BLUE);
  }

  // Draw 60 dots
  for(int i = 0; i<360; i+= 6) {
    sx = cos((i-90)*0.0174532925);
    sy = sin((i-90)*0.0174532925);
    x0 = sx*102+120;
    yy0 = sy*102+yStart;
    // Draw minute markers
    tft.drawPixel(x0, yy0, TFT_WHITE);

    // Draw main quadrant dots
    if(i==0 || i==180) tft.fillCircle(x0, yy0, 2, TFT_WHITE);
    if(i==90 || i==270) tft.fillCircle(x0, yy0, 2, TFT_WHITE);
  }

  tft.fillCircle(120, yStart + 1 , 3, TFT_WHITE);

  targetTime = millis() + 1000;
  shouldDrawClockFace = false;
  lastDay = 0;

}

void updateClock() {

    time_t local = tz_cet.toLocal(now());

    int ss = second(local);
    int mm = minute(local);
    int hh = hour(local);

    int yStart = 120;
    if (flipDisplay == true) {
      yStart = 200;
    }

    if (lastSecond == ss) {
      return;
    }
    // Pre-compute hand degrees, x & y coords for a fast screen update
    sdeg = ss*6;                  // 0-59 -> 0-354
    mdeg = mm*6+sdeg*0.01666667;  // 0-59 -> 0-360 - includes seconds
    hdeg = hh*30+mdeg*0.0833333;  // 0-11 -> 0-360 - includes minutes and seconds
    hx = cos((hdeg-90)*0.0174532925);
    hy = sin((hdeg-90)*0.0174532925);
    mx = cos((mdeg-90)*0.0174532925);
    my = sin((mdeg-90)*0.0174532925);
    sx = cos((sdeg-90)*0.0174532925);
    sy = sin((sdeg-90)*0.0174532925);

    if (ss==0 || initial) {
      initial = 0;
      // Erase hour and minute hand positions every minute
      tft.drawLine(ohx, ohy, 120, yStart+1, TFT_BLACK);
      ohx = hx*62+121;
      ohy = hy*62+yStart+1;
      tft.drawLine(omx, omy, 120, yStart+1, TFT_BLACK);
      omx = mx*84+120;
      omy = my*84+yStart+1;
    }

      // Redraw new hand positions, hour and minute hands not erased here to avoid flicker
      tft.drawLine(osx, osy, 120, yStart+1, TFT_BLACK);
      osx = sx*90+121;
      osy = sy*90+yStart+1;
      tft.drawLine(osx, osy, 120, yStart+1, TFT_RED);
      tft.drawLine(ohx, ohy, 120, yStart+1, TFT_WHITE);
      tft.drawLine(omx, omy, 120, yStart+1, TFT_WHITE);
      tft.drawLine(osx, osy, 120, yStart+1, TFT_RED);

    tft.fillCircle(120, yStart+1, 3, TFT_RED);


  if (day()!=lastDay) {
    int dy = 260;
    if (flipDisplay == true) {
      dy = 20;
    }
    //tft.fillRect(0, dy, 20, 240, TFT_BLACK);
    tft.setTextColor(TFT_YELLOW, TFT_BLACK);
    tft.setTextDatum(TC_DATUM);
    tft.drawString(String(deDayShortStr(weekday())) + ", " + String(day(local)) + "." + String(month(local)) +"." + String(year(local)),120,dy,4);
  }
}

void queryData() {
  tft.fillScreen(TFT_BLACK);
  tft.setTextDatum(TC_DATUM);
  yPos = 40;
  tft.drawString( String("Login to CD"),110, yPos);
  yPos = yPos + 20;

  String token = loginCDgetToken();
  if (token != "") {
    tft.drawString(String("seems legit, query car data"),110, yPos);
    yPos = yPos + 20;

    String strcars = getCarData(token,"");
    CarData car = getFirstCar(strcars);

    if (car.vin != "") {
      String result = getCarData(token,car.vin);
      currentStatus = parseCarData(car,result);
      showData();
    }

  } else {
    tft.setTextColor(TFT_RED);
    tft.drawString(String("Login Error"),110, yPos);
  }
  timei3 = 30;
  lastUpdate = millis();
  lasti3Update = millis();

}

void showData() {
  shouldDrawClockFace = true;
  tft.fillScreen(TFT_BLACK);
  tft.setTextDatum(TC_DATUM);
  yPos = 20;
  tft.drawString(String(currentStatus.name),120, yPos);
  yPos = yPos + 30;

  tft.setTextColor(TFT_YELLOW, TFT_BLACK);

  if (currentStatus.lvl > 20) {
    tft.setTextColor(TFT_GREEN, TFT_BLACK);
  }

  if (currentStatus.lvl < 5) {
    tft.setTextColor(TFT_RED, TFT_BLACK);
  }

  if (currentStatus.charging) {
    tft.setTextColor(TFT_BLUE, TFT_BLACK);
    UPDATE_INTERVAL_SECS = random(300,600);
  } else {
    UPDATE_INTERVAL_SECS = 1800 + random(100,400);
  }

  tft.drawString(String(currentStatus.lvl) + " %",140, yPos,8);
  yPos = yPos + 100;
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.drawString(String("remaining km: ") + String(currentStatus.kmleft), 120, yPos);
  yPos = yPos + 20;

  if (currentStatus.charging) {
    tft.drawString(String("Car is charging ... ") + currentStatus.chargingTimeRemaining + String(" min left"), 120, yPos);
    yPos = yPos + 20;
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

CarStatus parseCarData(CarData car, String json) {
  Serial.print("JSON:");
  Serial.println(json);
  DynamicJsonBuffer jsonBuffer(4000);
  JsonObject &root = jsonBuffer.parseObject(json);
  JsonVariant _vehicleStatus = root["vehicleStatus"];
  int lvl = _vehicleStatus["chargingLevelHv"];
  int km = _vehicleStatus["remainingRangeElectric"];
  String chrg = _vehicleStatus["chargingStatus"];
  bool isCharging = (chrg == "CHARGING");
  int chargingTimeRemaining = _vehicleStatus["chargingTimeRemaining"];
  Serial.print("Battery :");
  Serial.println(lvl);
  Serial.print("Km left: ");
  Serial.println(km);
  Serial.print("Charging :");
  Serial.println(isCharging);
  return CarStatus(car.vin,car.name,lvl,km,isCharging,chargingTimeRemaining);
}

void loop() {
  httpServer.handleClient();

  if ((digitalRead(button1Pin) == 0) && (lastPressButton1 > 0)) {
    Serial.println("B1 Release last Update ");
    long tm = millis() - lasti3Update;
    Serial.println(tm);
    lastPressButton1 = 0;
    timei3 = 30;

    if (tm > (1000 * 90)) {
      queryData();
    } else {
      showData();
    }
  }

  if ((digitalRead(button1Pin) == 1) && (timei3 < 1) && (lastPressButton1 == 0)) {
    Serial.println("B1 Touch");
    lastPressButton1 = millis();
  }

  if ((millis() - lastUpdate > 1000)) {
    if (timei3 < 1) {
      if (shouldDrawClockFace == true) {
        drawClock();
      }

      time_t local = tz_cet.toLocal(now());
      if ((second(local) == 0) && ((minute(local) % 2) == 0)) {
        flipDisplay = !flipDisplay;
        drawClock();
      }

      updateClock();
    } else {
      Serial.print(timei3);
      Serial.println(" decrease");
      timei3 = timei3 - 1;
    }
    lastUpdate = millis();
  }
}

// Called if WiFi has not been configured yet
void configModeCallback (WiFiManager *myWiFiManager) {
  tft.setTextFont(2);
  tft.setTextColor(TFT_CYAN);
  tft.setTextDatum(TC_DATUM);
  tft.drawString(String("Wifi Manager"),160, 28);
  tft.drawString(String("Please connect to AP"),160, 50);
  tft.setTextColor(TFT_WHITE);
  tft.drawString( myWiFiManager->getConfigPortalSSID(),160, 72);
  tft.setTextColor(TFT_CYAN);
  tft.drawString(String("To setup Wifi Configuration"),160, 94);
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
