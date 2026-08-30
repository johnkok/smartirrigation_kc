#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <WiFiManager.h>
#include <Preferences.h>
#include <ArduinoJson.h>
#include <ModbusMaster.h>
#include <PubSubClient.h>
#include <time.h>
#include "config.h"

enum class ValveReason : uint8_t { NONE, MANUAL, MOISTURE, SCHEDULE, HOME_ASSISTANT };

struct SensorState {
  bool valid=false;
  float moisturePct=NAN;
  float temperatureC=NAN;
  uint16_t ec=0;
  float ph=NAN;
  uint32_t lastOkMs=0;
  uint8_t lastModbusResult=0xFF;
};

struct ScheduleEntry {
  bool enabled=false;
  uint8_t daysMask=0x7F;
  uint16_t startMinute=420;
  uint16_t durationMinutes=10;
};

struct ZoneConfig {
  bool autoEnabled=true;
  float startBelowPct=28.0f;
  float stopAbovePct=36.0f;
  uint16_t maxRunMinutes=20;
  ScheduleEntry schedules[MAX_SCHEDULES_PER_ZONE];
};

struct ZoneRuntime {
  bool valveOn=false;
  ValveReason reason=ValveReason::NONE;
  uint32_t valveStartedMs=0;
  uint32_t scheduledStopMs=0;
  int lastScheduleYDay[MAX_SCHEDULES_PER_ZONE] = {-1,-1,-1,-1};
};

HardwareSerial RS485Serial(2);
ModbusMaster node;
WebServer server(80);
Preferences prefs;
WiFiClient wifiClient;
PubSubClient mqtt(wifiClient);

SensorState sensors[SENSOR_COUNT];
ZoneConfig zones[ZONE_COUNT];
ZoneRuntime runtime[ZONE_COUNT];

volatile uint32_t flowPulses=0;
uint32_t lastFlowCalcMs=0;
uint32_t lastFlowPulses=0;
float flowLpm=0.0f;
float totalLiters=0.0f;

bool rainDetected=false;
bool weatherBlocked=WEATHER_BLOCK_DEFAULT;
bool safetyFault=false;
String safetyFaultReason="";
bool masterOn=false;
uint32_t masterCloseDueMs=0;

String mqttHost="";
uint16_t mqttPort=1883;
String mqttUser="";
String mqttPass="";
String deviceId="";
uint32_t lastSensorPoll=0;
uint32_t lastControlTick=0;
uint32_t lastMqttPublish=0;

void IRAM_ATTR flowISR(){ flowPulses++; }

void rs485PreTransmission(){ digitalWrite(RS485_DE_RE_PIN,HIGH); delayMicroseconds(200); }
void rs485PostTransmission(){ RS485Serial.flush(); delayMicroseconds(200); digitalWrite(RS485_DE_RE_PIN,LOW); }

void writeActive(int pin, bool on, bool activeHigh){
  if(pin<0) return;
  digitalWrite(pin, (on==activeHigh)?HIGH:LOW);
}

void setMaster(bool on){
  if(MASTER_VALVE_PIN<0) return;
  masterOn=on;
  writeActive(MASTER_VALVE_PIN,on,MASTER_ACTIVE_HIGH);
}

bool anyValveOn(){
  for(int z=0; z<ZONE_COUNT; z++) if(runtime[z].valveOn) return true;
  return false;
}

const char* reasonToString(ValveReason r){
  switch(r){
    case ValveReason::MANUAL:return "manual";
    case ValveReason::MOISTURE:return "moisture";
    case ValveReason::SCHEDULE:return "schedule";
    case ValveReason::HOME_ASSISTANT:return "home_assistant";
    default:return "none";
  }
}

void setValveHardware(uint8_t z,bool on){ writeActive(VALVE_PINS[z],on,VALVE_ACTIVE_HIGH); }

bool sensorHealthyIndex(int idx){
  return idx>=0 && idx<SENSOR_COUNT && sensors[idx].valid &&
         (millis()-sensors[idx].lastOkMs)<=SENSOR_STALE_MS;
}

bool zoneSensorHealthy(uint8_t z){
  int idx=ZONE_SENSOR_MAP[z];
  return idx<0 ? true : sensorHealthyIndex(idx);
}

float zoneMoisture(uint8_t z){
  int idx=ZONE_SENSOR_MAP[z];
  return (idx>=0 && idx<SENSOR_COUNT) ? sensors[idx].moisturePct : NAN;
}

void stopValve(uint8_t z){
  setValveHardware(z,false);
  runtime[z].valveOn=false;
  runtime[z].reason=ValveReason::NONE;
  runtime[z].scheduledStopMs=0;
  if(!anyValveOn()) masterCloseDueMs=millis()+MASTER_POSTCLOSE_MS;
}

void stopAll(const String& why){
  for(int z=0;z<ZONE_COUNT;z++) stopValve(z);
  safetyFault=true;
  safetyFaultReason=why;
}

void startValve(uint8_t z, ValveReason reason, uint32_t requestedDurationMs=0){
  if(z>=ZONE_COUNT || runtime[z].valveOn || safetyFault) return;
  if(rainDetected && RAIN_BLOCKS_AUTOMATION && reason!=ValveReason::MANUAL && reason!=ValveReason::HOME_ASSISTANT) return;
  if(weatherBlocked && reason!=ValveReason::MANUAL && reason!=ValveReason::HOME_ASSISTANT) return;

  if(!masterOn){
    setMaster(true);
    delay(MASTER_PREOPEN_MS);
  }
  runtime[z].valveOn=true;
  runtime[z].reason=reason;
  runtime[z].valveStartedMs=millis();
  uint32_t maxMs=(uint32_t)zones[z].maxRunMinutes*60000UL;
  runtime[z].scheduledStopMs=(requestedDurationMs>0)?millis()+min(maxMs,requestedDurationMs):0;
  setValveHardware(z,true);
}

bool readSensor(uint8_t idx){
  node.begin(SENSOR_IDS[idx],RS485Serial);
  node.preTransmission(rs485PreTransmission);
  node.postTransmission(rs485PostTransmission);
  uint8_t result=node.readHoldingRegisters(REG_MOISTURE,SENSOR_REGISTER_COUNT);
  sensors[idx].lastModbusResult=result;
  if(result!=node.ku8MBSuccess){ sensors[idx].valid=false; return false; }
  sensors[idx].moisturePct=node.getResponseBuffer(0)/10.0f;
  sensors[idx].temperatureC=(int16_t)node.getResponseBuffer(1)/10.0f;
  sensors[idx].ec=node.getResponseBuffer(2);
  sensors[idx].ph=SENSOR_REGISTER_COUNT>=4 ? node.getResponseBuffer(3)/10.0f : NAN;
  sensors[idx].valid=true;
  sensors[idx].lastOkMs=millis();
  return true;
}

void pollSensors(){
  for(int i=0;i<SENSOR_COUNT;i++){ readSensor(i); delay(30); }
}

void updateFlow(){
  uint32_t now=millis();
  if(lastFlowCalcMs==0){ lastFlowCalcMs=now; lastFlowPulses=flowPulses; return; }
  uint32_t dt=now-lastFlowCalcMs;
  if(dt<1000) return;
  noInterrupts(); uint32_t p=flowPulses; interrupts();
  uint32_t delta=p-lastFlowPulses;
  float liters=delta/FLOW_PULSES_PER_LITER;
  totalLiters += liters;
  flowLpm = liters * (60000.0f/dt);
  lastFlowPulses=p; lastFlowCalcMs=now;
}

void updateInputs(){
  rainDetected = RAIN_ACTIVE_LOW ? digitalRead(RAIN_PIN)==LOW : digitalRead(RAIN_PIN)==HIGH;
  updateFlow();

  if(anyValveOn()){
    uint32_t earliest=millis();
    for(int z=0;z<ZONE_COUNT;z++) if(runtime[z].valveOn) earliest=min(earliest,runtime[z].valveStartedMs);
    uint32_t runningFor=millis()-earliest;
    if(runningFor>FLOW_START_GRACE_MS && flowLpm>MAX_FLOW_LPM) stopAll("excessive_flow");
    if(runningFor>NO_FLOW_TRIP_MS && flowLpm<MIN_FLOW_LPM_WHEN_WATERING) stopAll("no_flow");
  }

  if(masterCloseDueMs && (int32_t)(millis()-masterCloseDueMs)>=0 && !anyValveOn()){
    setMaster(false); masterCloseDueMs=0;
  }
}

void saveConfig(){
  StaticJsonDocument<8192> d;
  JsonArray a=d["zones"].to<JsonArray>();
  for(int z=0;z<ZONE_COUNT;z++){
    JsonObject o=a.add<JsonObject>();
    o["autoEnabled"]=zones[z].autoEnabled;
    o["startBelowPct"]=zones[z].startBelowPct;
    o["stopAbovePct"]=zones[z].stopAbovePct;
    o["maxRunMinutes"]=zones[z].maxRunMinutes;
    JsonArray sa=o["schedules"].to<JsonArray>();
    for(int i=0;i<MAX_SCHEDULES_PER_ZONE;i++){
      JsonObject s=sa.add<JsonObject>();
      s["enabled"]=zones[z].schedules[i].enabled;
      s["daysMask"]=zones[z].schedules[i].daysMask;
      s["startMinute"]=zones[z].schedules[i].startMinute;
      s["durationMinutes"]=zones[z].schedules[i].durationMinutes;
    }
  }
  d["mqtt"]["host"]=mqttHost; d["mqtt"]["port"]=mqttPort; d["mqtt"]["user"]=mqttUser; d["mqtt"]["pass"]=mqttPass;
  String out; serializeJson(d,out); prefs.putString("config",out);
}

void loadConfig(){
  String raw=prefs.getString("config","");
  if(raw.isEmpty()) return;
  StaticJsonDocument<8192> d;
  if(deserializeJson(d,raw)) return;
  JsonArray a=d["zones"].as<JsonArray>();
  for(int z=0;z<ZONE_COUNT && z<(int)a.size();z++){
    JsonObject o=a[z];
    zones[z].autoEnabled=o["autoEnabled"]|zones[z].autoEnabled;
    zones[z].startBelowPct=o["startBelowPct"]|zones[z].startBelowPct;
    zones[z].stopAbovePct=o["stopAbovePct"]|zones[z].stopAbovePct;
    zones[z].maxRunMinutes=o["maxRunMinutes"]|zones[z].maxRunMinutes;
    JsonArray sa=o["schedules"].as<JsonArray>();
    for(int i=0;i<MAX_SCHEDULES_PER_ZONE && i<(int)sa.size();i++){
      JsonObject s=sa[i];
      zones[z].schedules[i].enabled=s["enabled"]|false;
      zones[z].schedules[i].daysMask=s["daysMask"]|127;
      zones[z].schedules[i].startMinute=s["startMinute"]|420;
      zones[z].schedules[i].durationMinutes=s["durationMinutes"]|10;
    }
  }
  mqttHost=String((const char*)(d["mqtt"]["host"]|""));
  mqttPort=d["mqtt"]["port"]|1883;
  mqttUser=String((const char*)(d["mqtt"]["user"]|""));
  mqttPass=String((const char*)(d["mqtt"]["pass"]|""));
}

void publishAvailability(bool online){
  if(!mqtt.connected()) return;
  String t=String(MQTT_BASE)+"/"+deviceId+"/availability";
  mqtt.publish(t.c_str(),online?"online":"offline",true);
}

String stateTopic(){ return String(MQTT_BASE)+"/"+deviceId+"/state"; }

void publishState(){
  if(!mqtt.connected()) return;
  StaticJsonDocument<4096> d;
  d["rain"]=rainDetected;
  d["weather_blocked"]=weatherBlocked;
  d["flow_lpm"]=flowLpm;
  d["total_liters"]=totalLiters;
  d["fault"]=safetyFault;
  d["fault_reason"]=safetyFaultReason;
  d["master_on"]=masterOn;

  for(int i=0;i<SENSOR_COUNT;i++){
    String k="sensor"+String(i+1);
    d[k]["valid"]=sensorHealthyIndex(i);
    d[k]["moisture"]=sensors[i].moisturePct;
    d[k]["temperature"]=sensors[i].temperatureC;
    d[k]["ec"]=sensors[i].ec;
    d[k]["ph"]=sensors[i].ph;
  }
  for(int z=0;z<ZONE_COUNT;z++){
    String k="zone"+String(z+1);
    d[k]["valve"]=runtime[z].valveOn?"ON":"OFF";
    d[k]["reason"]=reasonToString(runtime[z].reason);
    d[k]["auto"]=zones[z].autoEnabled;
    d[k]["start_below"]=zones[z].startBelowPct;
    d[k]["stop_above"]=zones[z].stopAbovePct;
    d[k]["max_run"]=zones[z].maxRunMinutes;
  }
  String out; serializeJson(d,out);
  mqtt.publish(stateTopic().c_str(),out.c_str(),true);
}

String deviceJson(){
  StaticJsonDocument<512> d;
  d["identifiers"]=deviceId;
  d["name"]="ESP32 Soil Irrigation";
  d["manufacturer"]="DIY";
  d["model"]="ESP32 RS485 Irrigation Controller";
  d["sw_version"]=FW_VERSION;
  String out; serializeJson(d,out); return out;
}

void publishSingleDiscovery(const String& component,const String& objectId,JsonDocument& d){
  String topic=String(MQTT_DISCOVERY_PREFIX)+"/"+component+"/"+deviceId+"_"+objectId+"/config";
  String out; serializeJson(d,out); mqtt.publish(topic.c_str(),out.c_str(),true);
}

void addCommon(JsonDocument& d,const String& name,const String& unique){
  d["name"]=name;
  d["unique_id"]=deviceId+"_"+unique;
  d["availability_topic"]=String(MQTT_BASE)+"/"+deviceId+"/availability";
  JsonObject dev=d["device"].to<JsonObject>();
  dev["identifiers"]=deviceId;
  dev["name"]="ESP32 Soil Irrigation";
  dev["manufacturer"]="DIY";
  dev["model"]="ESP32 RS485 Irrigation Controller";
  dev["sw_version"]=FW_VERSION;
}

void publishDiscovery(){
  if(!mqtt.connected()) return;
  String st=stateTopic();

  for(int i=0;i<SENSOR_COUNT;i++){
    {
      StaticJsonDocument<1024> d; addCommon(d,"Soil "+String(i+1)+" Moisture","soil"+String(i+1)+"_moisture");
      d["state_topic"]=st; d["unit_of_measurement"]="%"; d["device_class"]="moisture";
      d["value_template"]="{{ value_json.sensor"+String(i+1)+".moisture }}";
      publishSingleDiscovery("sensor","soil"+String(i+1)+"_moisture",d);
    }
    {
      StaticJsonDocument<1024> d; addCommon(d,"Soil "+String(i+1)+" Temperature","soil"+String(i+1)+"_temperature");
      d["state_topic"]=st; d["unit_of_measurement"]="°C"; d["device_class"]="temperature";
      d["value_template"]="{{ value_json.sensor"+String(i+1)+".temperature }}";
      publishSingleDiscovery("sensor","soil"+String(i+1)+"_temperature",d);
    }
    {
      StaticJsonDocument<1024> d; addCommon(d,"Soil "+String(i+1)+" EC","soil"+String(i+1)+"_ec");
      d["state_topic"]=st; d["unit_of_measurement"]="µS/cm";
      d["value_template"]="{{ value_json.sensor"+String(i+1)+".ec }}";
      publishSingleDiscovery("sensor","soil"+String(i+1)+"_ec",d);
    }
  }

  {
    StaticJsonDocument<1024> d; addCommon(d,"Irrigation Flow","flow");
    d["state_topic"]=st; d["unit_of_measurement"]="L/min";
    d["value_template"]="{{ value_json.flow_lpm }}";
    publishSingleDiscovery("sensor","flow",d);
  }
  {
    StaticJsonDocument<1024> d; addCommon(d,"Rain Detected","rain");
    d["state_topic"]=st; d["payload_on"]=true; d["payload_off"]=false;
    d["value_template"]="{{ value_json.rain }}";
    publishSingleDiscovery("binary_sensor","rain",d);
  }
  {
    StaticJsonDocument<1024> d; addCommon(d,"Irrigation Fault","fault");
    d["state_topic"]=st; d["payload_on"]=true; d["payload_off"]=false;
    d["value_template"]="{{ value_json.fault }}";
    publishSingleDiscovery("binary_sensor","fault",d);
  }

  for(int z=0;z<ZONE_COUNT;z++){
    String zn=String(z+1);
    {
      StaticJsonDocument<1024> d; addCommon(d,"Zone "+zn+" Valve","zone"+zn+"_valve");
      d["state_topic"]=st; d["command_topic"]=String(MQTT_BASE)+"/"+deviceId+"/zone/"+zn+"/set";
      d["value_template"]="{{ value_json.zone"+zn+".valve }}";
      d["payload_on"]="ON"; d["payload_off"]="OFF";
      publishSingleDiscovery("switch","zone"+zn+"_valve",d);
    }
    for(auto pair : {std::pair<const char*,const char*>("start_below","Start Below"),
                     std::pair<const char*,const char*>("stop_above","Stop Above"),
                     std::pair<const char*,const char*>("max_run","Max Run")}){
      StaticJsonDocument<1024> d;
      String key=pair.first; String label=pair.second;
      addCommon(d,"Zone "+zn+" "+label,"zone"+zn+"_"+key);
      d["state_topic"]=st;
      d["value_template"]="{{ value_json.zone"+zn+"."+key+" }}";
      d["command_topic"]=String(MQTT_BASE)+"/"+deviceId+"/zone/"+zn+"/"+key+"/set";
      d["min"]= key=="max_run" ? 1 : 0;
      d["max"]= key=="max_run" ? 240 : 100;
      d["step"]=1;
      if(key!="max_run") d["unit_of_measurement"]="%";
      else d["unit_of_measurement"]="min";
      publishSingleDiscovery("number","zone"+zn+"_"+key,d);
    }
  }
}

void clearFault(){
  safetyFault=false;
  safetyFaultReason="";
}

void mqttCallback(char* topic, byte* payload, unsigned int len){
  String t(topic), p; for(unsigned int i=0;i<len;i++) p+=(char)payload[i];
  if(t=="homeassistant/status" && p=="online"){ publishDiscovery(); publishState(); return; }

  String base=String(MQTT_BASE)+"/"+deviceId;
  if(t==base+"/weather_block/set"){ weatherBlocked=(p=="ON"||p=="1"||p=="true"); publishState(); return; }
  if(t==base+"/fault/clear"){ clearFault(); publishState(); return; }

  for(int z=0;z<ZONE_COUNT;z++){
    String zn=String(z+1);
    if(t==base+"/zone/"+zn+"/set"){
      if(p=="ON") startValve(z,ValveReason::HOME_ASSISTANT); else stopValve(z);
      publishState(); return;
    }
    if(t==base+"/zone/"+zn+"/start_below/set"){ zones[z].startBelowPct=constrain(p.toFloat(),0,100); saveConfig(); publishState(); return; }
    if(t==base+"/zone/"+zn+"/stop_above/set"){ zones[z].stopAbovePct=constrain(p.toFloat(),0,100); saveConfig(); publishState(); return; }
    if(t==base+"/zone/"+zn+"/max_run/set"){ zones[z].maxRunMinutes=constrain(p.toInt(),1,240); saveConfig(); publishState(); return; }
  }
}

void connectMqtt(){
  if(mqttHost.isEmpty() || mqtt.connected()) return;
  mqtt.setServer(mqttHost.c_str(),mqttPort);
  mqtt.setCallback(mqttCallback);
  String avail=String(MQTT_BASE)+"/"+deviceId+"/availability";
  bool ok;
  if(mqttUser.isEmpty()) ok=mqtt.connect(deviceId.c_str(),avail.c_str(),0,true,"offline");
  else ok=mqtt.connect(deviceId.c_str(),mqttUser.c_str(),mqttPass.c_str(),avail.c_str(),0,true,"offline");
  if(ok){
    publishAvailability(true);
    mqtt.subscribe("homeassistant/status");
    String base=String(MQTT_BASE)+"/"+deviceId;
    mqtt.subscribe((base+"/weather_block/set").c_str());
    mqtt.subscribe((base+"/fault/clear").c_str());
    for(int z=1;z<=ZONE_COUNT;z++){
      String zn=String(z);
      mqtt.subscribe((base+"/zone/"+zn+"/set").c_str());
      mqtt.subscribe((base+"/zone/"+zn+"/start_below/set").c_str());
      mqtt.subscribe((base+"/zone/"+zn+"/stop_above/set").c_str());
      mqtt.subscribe((base+"/zone/"+zn+"/max_run/set").c_str());
    }
    publishDiscovery(); publishState();
  }
}

void sendJson(const String& s,int code=200){ server.sendHeader("Access-Control-Allow-Origin","*"); server.send(code,"application/json",s); }

void handleStatus(){
  StaticJsonDocument<6144> d;
  d["wifi"]["ip"]=WiFi.localIP().toString(); d["wifi"]["rssi"]=WiFi.RSSI();
  d["mqtt"]["connected"]=mqtt.connected(); d["rain"]=rainDetected; d["weatherBlocked"]=weatherBlocked;
  d["flowLpm"]=flowLpm; d["totalLiters"]=totalLiters; d["fault"]=safetyFault; d["faultReason"]=safetyFaultReason;
  JsonArray s=d["sensors"].to<JsonArray>();
  for(int i=0;i<SENSOR_COUNT;i++){
    JsonObject o=s.add<JsonObject>(); o["id"]=i+1; o["valid"]=sensorHealthyIndex(i);
    o["moisturePct"]=sensors[i].moisturePct; o["temperatureC"]=sensors[i].temperatureC; o["ec"]=sensors[i].ec; o["ph"]=sensors[i].ph;
  }
  JsonArray v=d["valves"].to<JsonArray>();
  for(int z=0;z<ZONE_COUNT;z++){
    JsonObject o=v.add<JsonObject>(); o["id"]=z+1; o["on"]=runtime[z].valveOn; o["reason"]=reasonToString(runtime[z].reason);
  }
  String out; serializeJson(d,out); sendJson(out);
}

void handleValve(){
  int id=server.arg("id").toInt(); bool on=server.arg("on")=="true"||server.arg("on")=="1";
  if(id<1||id>ZONE_COUNT){ sendJson(R"({"error":"invalid zone"})",400); return; }
  if(on) startValve(id-1,ValveReason::MANUAL); else stopValve(id-1);
  sendJson(R"({"ok":true})");
}

void setupApi(){
  server.on("/",HTTP_GET,[](){server.send(200,"text/plain","ESP32 Soil Irrigation v2\n/api/status\n/api/valve?id=1&on=true\n");});
  server.on("/api/status",HTTP_GET,handleStatus);
  server.on("/api/valve",HTTP_POST,handleValve);
  server.on("/api/fault/clear",HTTP_POST,[](){clearFault();sendJson(R"({"ok":true})");});
  server.begin();
}

void processSchedules(uint8_t z,const struct tm& now){
  int minute=now.tm_hour*60+now.tm_min; uint8_t dayBit=1u<<now.tm_wday;
  for(int i=0;i<MAX_SCHEDULES_PER_ZONE;i++){
    auto &s=zones[z].schedules[i];
    if(!s.enabled || !(s.daysMask&dayBit) || s.startMinute!=minute || runtime[z].lastScheduleYDay[i]==now.tm_yday || runtime[z].valveOn) continue;
    if(ZONE_SENSOR_MAP[z]>=0 && !zoneSensorHealthy(z)) continue;
    if(SCHEDULE_REQUIRES_DRY_SOIL && !isnan(zoneMoisture(z)) && zoneMoisture(z)>=zones[z].startBelowPct) continue;
    runtime[z].lastScheduleYDay[i]=now.tm_yday;
    startValve(z,ValveReason::SCHEDULE,(uint32_t)s.durationMinutes*60000UL);
  }
}

void controlZone(uint8_t z){
  uint32_t now=millis();
  if(runtime[z].valveOn && now-runtime[z].valveStartedMs >= (uint32_t)zones[z].maxRunMinutes*60000UL){ stopValve(z); return; }
  if(runtime[z].valveOn && runtime[z].scheduledStopMs && (int32_t)(now-runtime[z].scheduledStopMs)>=0){ stopValve(z); return; }
  if(runtime[z].valveOn && runtime[z].reason!=ValveReason::MOISTURE) return;
  if(ZONE_SENSOR_MAP[z]<0) return;
  if(!zones[z].autoEnabled || rainDetected || weatherBlocked || !zoneSensorHealthy(z)){ if(runtime[z].reason==ValveReason::MOISTURE) stopValve(z); return; }
  float m=zoneMoisture(z);
  if(!runtime[z].valveOn && m<zones[z].startBelowPct) startValve(z,ValveReason::MOISTURE);
  else if(runtime[z].valveOn && runtime[z].reason==ValveReason::MOISTURE && m>=zones[z].stopAbovePct) stopValve(z);
}

void setup(){
  Serial.begin(115200);
  for(int z=0;z<ZONE_COUNT;z++){ pinMode(VALVE_PINS[z],OUTPUT); setValveHardware(z,false); }
  if(MASTER_VALVE_PIN>=0){ pinMode(MASTER_VALVE_PIN,OUTPUT); setMaster(false); }
  pinMode(RS485_DE_RE_PIN,OUTPUT); digitalWrite(RS485_DE_RE_PIN,LOW);
  pinMode(RAIN_PIN,INPUT_PULLUP);
  pinMode(FLOW_PIN,INPUT_PULLUP); attachInterrupt(digitalPinToInterrupt(FLOW_PIN),flowISR,RISING);
  RS485Serial.begin(RS485_BAUD,SERIAL_8N1,RS485_RX_PIN,RS485_TX_PIN);

  prefs.begin("irrigation",false); loadConfig();

  WiFi.mode(WIFI_STA);
  WiFiManager wm;
  char mqttHostBuf[64]="";
  char mqttPortBuf[8]="1883";
  char mqttUserBuf[32]="";
  char mqttPassBuf[64]="";
  strncpy(mqttHostBuf,mqttHost.c_str(),sizeof(mqttHostBuf)-1);
  snprintf(mqttPortBuf,sizeof(mqttPortBuf),"%u",mqttPort);
  strncpy(mqttUserBuf,mqttUser.c_str(),sizeof(mqttUserBuf)-1);
  strncpy(mqttPassBuf,mqttPass.c_str(),sizeof(mqttPassBuf)-1);

  WiFiManagerParameter pHost("mqtt_host","MQTT broker IP/host",mqttHostBuf,63);
  WiFiManagerParameter pPort("mqtt_port","MQTT port",mqttPortBuf,7);
  WiFiManagerParameter pUser("mqtt_user","MQTT username",mqttUserBuf,31);
  WiFiManagerParameter pPass("mqtt_pass","MQTT password",mqttPassBuf,63);
  wm.addParameter(&pHost); wm.addParameter(&pPort); wm.addParameter(&pUser); wm.addParameter(&pPass);

  if(!wm.autoConnect("SoilIrrigation-Setup")) ESP.restart();

  mqttHost=pHost.getValue(); mqttPort=atoi(pPort.getValue()); mqttUser=pUser.getValue(); mqttPass=pPass.getValue();
  saveConfig();

  uint64_t chip=ESP.getEfuseMac();
  char idbuf[24]; snprintf(idbuf,sizeof(idbuf),"esp32_irrig_%08X",(uint32_t)(chip&0xFFFFFFFF));
  deviceId=idbuf;

  configTzTime(TZ_INFO,NTP_1,NTP_2);
  setupApi();
  pollSensors();
}

void loop(){
  server.handleClient();
  updateInputs();

  if(!mqtt.connected()){
    static uint32_t lastTry=0;
    if(millis()-lastTry>5000){ lastTry=millis(); connectMqtt(); }
  } else mqtt.loop();

  uint32_t now=millis();
  if(now-lastSensorPoll>=SENSOR_POLL_MS){lastSensorPoll=now;pollSensors();}
  if(now-lastControlTick>=CONTROL_TICK_MS){
    lastControlTick=now;
    struct tm t;
    if(getLocalTime(&t,20)) for(int z=0;z<ZONE_COUNT;z++) processSchedules(z,t);
    for(int z=0;z<ZONE_COUNT;z++) controlZone(z);
  }
  if(now-lastMqttPublish>=MQTT_PUBLISH_MS){lastMqttPublish=now;publishState();}
  delay(2);
}
