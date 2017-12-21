/**
 *	ESP8266 interactive command processor.
 *	It is implemented several APIs provided by Arduino's ESP8266 core so that 
 *	they can be executed with interactive commands.
 *	@file	ESPShaker.ino
 *	@author	hieromon@gmail.com
 *	@version	1.03
 *	@date	2017-12-21
 *	@copyright	MIT license.
 */

#include <functional>
#include <stdio.h>
#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <DNSServer.h>
#include <ESP8266mDNS.h>
#include <ESP8266HTTPClient.h>
#include <FS.h>
#include "PseudoPWM.h"
#include "SerialCommand.h"
#include "PageBuilder.h"
extern "C" {
    #include <user_interface.h>
}

#define _VERSION    "1.03"

class httpHandler : public RequestHandler {
public:
    httpHandler() {};
    ~httpHandler() {};

    bool canHandle(HTTPMethod method, String uri) override {
        Serial.println();
        Serial.print("http request:");
        switch (method) {
        case HTTP_ANY:
            Serial.print("ANY");
            break;
        case HTTP_GET:
            Serial.print("GET");
            break;
        case HTTP_POST:
            Serial.print("POST");
            break;
        case HTTP_PUT:
            Serial.print("PUT");
            break;
        case HTTP_PATCH:
            Serial.print("PATCH");
            break;
        case HTTP_DELETE:
            Serial.print("DELETE");
            break;
        case HTTP_OPTIONS:
            Serial.print("OPTIONS");
            break;
        default:
            Serial.print("Unknown");
        };
        Serial.println("," + uri);
        Serial.print("> ");
        return false;
    }

    bool canUpload(String uri) override {
        Serial.println("http upload request:" + uri);
        Serial.print("> ");
        return false;
    }
};

#define	ONBOARD_LED	2
//#define	ONBOARD_LED	D4	2
#define	ONBOARD_LED_OFF	HIGH
#define BAUDRATE  115200

SerialCommand	Cmd;
PseudoPWM	PilotLED(ONBOARD_LED);
wl_status_t	WiFiStatus = WL_NO_SHIELD;
WiFiMode_t	WiFiMode;
String	MDNSHostName = "";
String	MDNSServiceName = "";
String	MDNSProtocolName = "";
bool	onDnsserver = false;
bool    notifyEvent = false;
bool    inSmartConfig = false;

DNSServer			DnsServer;
ESP8266WebServer	*WebServer = nullptr;
HTTPClient			client;
httpHandler*		webHandler = nullptr;

bool isNumber(String str) {
    uint8_t varLength = (uint8_t)str.length();
    if (varLength > 0) {
        for (uint8_t i = 0; i < varLength; i++)
            if (!isDigit(str.charAt(i)))
                return false;
        return true;
    }
    return false;
}

void autoConnect() {
    String	onoff = String(Cmd.next());
    if (onoff.length() > 0) {
        onoff.toLowerCase();
        if (onoff == "on" || onoff == "off") {
            if (WiFi.getMode() == WIFI_AP)
                Serial.print("In softAP mode, command ignore:");
            bool sw = onoff == "on" ? true : false;
            Serial.println("WiFi.setAutoConnect(" + onoff + ")");
            Serial.println(WiFi.setAutoConnect(sw) ? "OK" : "Fail");
        }
    }
    Serial.print("> ");
}

void beginWiFi() {
    bool    beginWait = false;
    char*	c_ssid;
    char*   c_pass;
    char*   c_op = Cmd.next();
    String  op = String(c_op);

    Serial.print("WiFi.begin(");
    if (op == "#wait") {
        beginWait = true;
        WiFiStatus = WiFi.begin();
    }
    else {
        c_ssid = c_op;
        Serial.print(c_ssid);
        c_pass = Cmd.next();
        op = String(c_pass);
        if (op == "#wait") {
            c_pass = nullptr;
            beginWait = true;
        }
        else {
            if (String(Cmd.next()) == "#wait")
                beginWait = true;
        }
        if (c_pass)
            Serial.print("," + String(c_pass));
        WiFiStatus = WiFi.begin(c_ssid, c_pass);
    }
    Serial.print(") ");
    if (beginWait) {
        Serial.println("wait");
    } else
        Serial.println("nowait");
    PilotLED.Start(200, 100);
    if (beginWait) {
        WiFi.waitForConnectResult();
    }
}

void beginWPS() {
    Serial.println("WiFi.beginWPSConfig");
    Serial.println(WiFi.beginWPSConfig() ? "OK" : "Fail");
    Serial.print("> ");
}

void disconnWiFi() {
    String  ap = String(Cmd.next());
    if (ap.length() == 0) {
        WiFiStatus = WiFi.status();
        Serial.println("WiFi.disconnect");
        if (WiFi.disconnect()) {
            Serial.println("OK");
        }
        else {
            Serial.println("Fail");
        }
    }
    else {
        if (ap == "ap") {
            Serial.println("WiFi.softAPdisconnect");
            Serial.println(WiFi.softAPdisconnect() ? "OK" : "Fail");
        }
    }
    Serial.print("> ");
}

void doDelay() {
    String  s_ms = String(Cmd.next());
    if (s_ms.length() > 0) {
        unsigned long ms = s_ms.toInt();
        Serial.println("delay " + String(ms));
        delay(ms);
    }
    Serial.print("> ");
}

void doEvent() {
    String	onoff = String(Cmd.next());
    if (onoff.length() > 0) {
        onoff.toLowerCase();
        if (onoff == "on" || onoff == "off") {
            notifyEvent = onoff == "on" ? true : false;
            Serial.println("WiFi.onEvent(" + onoff + ")");
        }
    }
    Serial.print("> ");
}

void fileSystem() {
    String  op = String(Cmd.next());
    op.toLowerCase();
    if (op == "start" || op == "stop" || op == "dir" || op == "file" || op == "format" || op == "info" || op == "remove" || op == "rename") {
        PilotLED.Start(200, 100);
        FSInfo  fsInfo;
        Serial.println(String("fs ") + op);
        if (op == "start") {
            Serial.print("SPIFFS ");
            if (SPIFFS.begin()) {
                SPIFFS.info(fsInfo);
                Serial.println("mounted. " + String(fsInfo.totalBytes) + " bytes/" + String(fsInfo.usedBytes) + " used.");
            }
            else { Serial.println("could not start."); }
        }
        else if (op == "stop") {
            Serial.println("SPIFFS " + op);
            SPIFFS.end();
            Serial.println("OK");
        }
        else if (op == "format") {
            Serial.println("SPIFFS " + op);
            Serial.println(SPIFFS.format() ? "OK" : "Fail");
        }
        else {
            if (SPIFFS.info(fsInfo)) {
                if (op == "dir") {
                    Serial.println("Dir:");
                    uint16_t    fn = 0;
                    Dir dir = SPIFFS.openDir("/");
                    while (dir.next()) {
                        fn++;
                        Serial.print(String(fn) + ":" + dir.fileName() + " ");
                        File f = dir.openFile("r");
                        Serial.println(f.size());
                        f.close();
                    }
                }
                else if (op == "info") {
                    Serial.println("FS:");
                    Serial.println("Size:" + String(fsInfo.totalBytes));
                    Serial.println("Used:" + String(fsInfo.usedBytes));
                    Serial.println("Block size:" + String(fsInfo.blockSize));
                    Serial.println("Page size:" + String(fsInfo.pageSize));
                    Serial.println("Max open files:" + String(fsInfo.maxOpenFiles));
                    Serial.println("Max path length:" + String(fsInfo.maxPathLength));
                }
                else if (op == "file" || op == "remove" || op == "rename") {
                    const char* c_path = Cmd.next();
                    if (c_path) {
                        String path = String(c_path);
                        if (path.charAt(0) != '/')
                            path = String("/") + path;
                        Serial.print(path + ":");
                        if (SPIFFS.exists(path)) {
                            if (op == "file") {
                                char buff[128] = { 0 };
                                File f = SPIFFS.open(path, "r");
                                if (f) {
                                    int fileSize = f.size();
                                    Serial.println(String(fileSize) + "bytes");
                                    while (fileSize > 0 || fileSize == -1) {
                                        size_t  availSize = f.available();
                                        if (availSize) {
                                            int blkSize = f.readBytes(buff, ((availSize > sizeof(buff)) ? sizeof(buff) : availSize));
                                            Serial.write(buff, blkSize);
                                            if (fileSize > 0)
                                                fileSize -= blkSize;
                                        }
                                        delay(1);
                                    }
                                }
                                else { Serial.println("open failed."); }
                            }
                            else if (op == "remove") {
                                Serial.println();
                                Serial.print(SPIFFS.remove(path) ? "OK" : "Fail");
                            }
                            else if (op == "rename") {
                                const char* c_newPath = Cmd.next();
                                if (c_newPath) {
                                    String newPath = String(c_newPath);
                                    if (!SPIFFS.exists(newPath)) {
                                        Serial.println("-> " + newPath);
                                        Serial.print(SPIFFS.rename(path, newPath) ? "OK" : "Fail");
                                    }
                                    else { Serial.print("[error] file already exists."); }
                                }
                                else { Serial.print("[error] new file name missing."); }
                            }
                        }
                        else { Serial.print(" not found."); }
                        Serial.println();
                    }
                }
            }
            else { Serial.println("[error] FS not mounted."); }
        }
        PilotLED.Start(800, 600);
    }
    Serial.print(" >");
}

void freeHeap() {
    Serial.println("Free heap size:" + String(system_get_free_heap_size()));
    Serial.print("> ");
}

void gpio() {
    String  op = String(Cmd.next());
    op.toLowerCase();
    if (op == "get" || op == "set") {
        String  s_portNum = String(Cmd.next());
        if (isNumber(s_portNum)) {
            uint8_t portNum = (uint8_t)s_portNum.toInt();
            if (portNum >= 0 && portNum <= 16) {
                if (portNum >= 6 && portNum <= 11) {
                    Serial.println("[error] GPIO" + String(portNum) + " cannot be specified.");
                }
                else {
                    if (op == "get") {
                        Serial.print("Get GPIO" + String(portNum) + ":");
                        Serial.println(digitalRead(portNum) == LOW ? String("LOW") : String("HIGH"));
                    }
                    else {
                        String  s_output = String(Cmd.next());
                        s_output.toLowerCase();
                        if (s_output == "high" || s_output == "low") {
                            uint8_t output = s_output == "low" ? LOW : HIGH;
                            Serial.print("Set GPIO" + String(portNum) + ":");
                            pinMode(portNum, OUTPUT);
                            digitalWrite(portNum, output);
                            Serial.println(digitalRead(portNum) == LOW ? String("LOW") : String("HIGH"));
                        }
                    }
                }
            }
        }
    }
    Serial.print("> ");
}

void reset() {
    Serial.println("ESP.reset");
    ESP.reset();
}

void scan() {
    Serial.println("WiFi.scanNetworks");
    PilotLED.Start(200, 100);
    int8_t	nn = WiFi.scanNetworks(false, true);
    PilotLED.Start(800, 600);
    for (uint8_t i = 0; i < nn; i++) {
        String encType;
        switch (WiFi.encryptionType(i)) {
        case ENC_TYPE_TKIP:
            encType = "ENC_TYPE_TKIP";
            break;
        case ENC_TYPE_CCMP:
            encType = "ENC_TYPE_CCMP";
            break;
        case ENC_TYPE_WEP:
            encType = "ENC_TYPE_WEP";
            break;
        case ENC_TYPE_NONE:
            encType = "ENC_TYPE_NONE";
            break;
        case ENC_TYPE_AUTO:
            encType = "ENC_TYPE_AUTO";
            break;
        }
        String	ssid = WiFi.SSID(i).length() > 0 ? WiFi.SSID(i) : "?";
        Serial.println("BSSID<" + WiFi.BSSIDstr(i) + "> SSID:" + ssid + " RSSI(" + String(WiFi.RSSI(i)) + ") CH(" + String(WiFi.channel(i)) + ") " + encType);
    }
    Serial.print("> ");
}

void setPersistent() {
    String sw = String(Cmd.next());
    sw.toLowerCase();
    if (sw == "on" || sw == "off") {
        Serial.println("WiFi.persistent(" + sw + ")");
        WiFi.persistent(sw == "on" ? true : false);
        Serial.println("OK");
    }
    Serial.print("> ");
}

void setSleep() {
    WiFiSleepType   mode;
    String  direction = String(Cmd.next());
    direction.toLowerCase();
    if (direction == "deep") {
        char*   c_us = Cmd.next();
        if (c_us) {
            String  us = String(c_us);
            Serial.println("ESP.deepSleep(" + us + ")");
            ESP.deepSleep(us.toInt());
        }
    }
    else {
        bool valid = true;
        Serial.print("WiFi.setSleepMode(");
        if (direction == "none") {
            Serial.print("WIFI_NONE_SLEEP");
            mode = WIFI_NONE_SLEEP;
        }
        else if (direction == "light") {
            Serial.print("WIFI_LIGHT_SLEEP");
            mode = WIFI_LIGHT_SLEEP;
        }
        else if (direction == "modem") {
            Serial.print("WIFI_MODEM_SLEEP");
            mode = WIFI_MODEM_SLEEP;
        }
        else
            valid = false;
        Serial.println(')');
        if (valid)
            Serial.println(WiFi.setSleepMode(mode) ? "OK" : "Fail");
    }
    Serial.print("> ");
}

void setWiFiMode() {
    char*	c_mode = Cmd.next();
    String	mode = (c_mode);
    mode.toLowerCase();
    bool	t;
    Serial.print("WiFi.mode(");
    if (mode == "ap") {
        Serial.print("WIFI_AP");
        t = WiFi.mode(WIFI_AP);
    } 
    else if (mode == "sta") {
        Serial.print("WIFI_STA");
        t = WiFi.mode(WIFI_STA);
    }
    else if (mode == "apsta") {
        Serial.print("WIFI_AP_STA");
        t = WiFi.mode(WIFI_AP_STA);
    }
    else if (mode == "off") {
        Serial.print("WIFI_OFF");
        WiFi.disconnect();
        t = WiFi.mode(WIFI_OFF);
    }
    else {
        Serial.println("Unknown)");
        Serial.print("> ");
        return;
    }
    Serial.println(")");
    Serial.println(t ? "OK" : "Fail");
    Serial.print("> ");
}

void showConfig() {
    uint8_t	macAddress[6];
    Serial.println("Config");
    showWiFiMode();
    Serial.print("WiFi.AutoConnect:");
    Serial.println(WiFi.getAutoConnect() ? "True" : "False");
    Serial.print("WiFi.hostname:");
    Serial.println(WiFi.hostname());
    WiFi.softAPmacAddress(macAddress);
    Serial.println("WiFi.SoftAPMAC:" + toMacAddress(macAddress));
    Serial.print("WiFi.softAPIP:");
    Serial.println(WiFi.softAPIP());
    WiFi.macAddress(macAddress);
    Serial.println("WiFi.MAC:" + toMacAddress(macAddress));
    Serial.print("WiFi.LocalIP:");
    Serial.println(WiFi.localIP());
    Serial.print("WiFi.gateway:");
    Serial.println(WiFi.gatewayIP());
    Serial.print("WiFi.subnetMask:");
    Serial.println(WiFi.subnetMask());
    Serial.println("WiFi.SSID:" + WiFi.SSID());
    Serial.println("WiFi.PSK:" + WiFi.psk());
    Serial.print("WiFi.sleepMode:");
    switch (WiFi.getSleepMode()) {
    case WIFI_NONE_SLEEP:
        Serial.println("NONE");
        break;
    case WIFI_LIGHT_SLEEP:
        Serial.println("LIGHT");
        break;
    case WIFI_MODEM_SLEEP:
        Serial.println("MODEM");
        break;
    }
    Serial.println("SDK version:" + String(ESP.getSdkVersion()));
    Serial.println("CPU Freq.:" + String(ESP.getCpuFreqMHz()) + "MHz");
    Serial.println("Flash size:" + String(ESP.getFlashChipRealSize()));
    Serial.println("Flash Freq.:" + String(ESP.getFlashChipSpeed()/1000000) + "MHz");
    Serial.print("Flash mode:");
    switch (ESP.getFlashChipMode()) {
    case 0:
        Serial.println("QIO");
        break;
    case 1:
        Serial.println("QOUT");
        break;
    case 2:
        Serial.println("DIO");
        break;
    case 3:
        Serial.println("DOUT");
        break;
    default:
        Serial.println();
    }
    Serial.print("> ");
}

void showStatus() {
    static struct _stEnum {
        wl_status_t	st;
        const char*	mnemonic;
    } const stEnum[] = {
        { WL_NO_SHIELD, "WL_NO_SHIELD" },
        { WL_IDLE_STATUS, "WL_IDLE_STATUS" },
        { WL_NO_SSID_AVAIL, "WL_NO_SSID_AVAIL" },
        { WL_SCAN_COMPLETED, "WL_SCAN_COMPLETED" },
        { WL_CONNECTED, "WL_CONNECTED" },
        { WL_CONNECT_FAILED, "WL_CONNECT_FAILED" },
        { WL_CONNECTION_LOST, "WL_CONNECTION_LOST" },
        { WL_DISCONNECTED, "WL_DISCONNECTED" }
    };
    const char*	stMnemonic = "";
    wl_status_t	wst = WiFi.status();
    for (uint8_t i = 0; i < sizeof(stEnum) / sizeof(struct _stEnum); i++)
        if (wst == stEnum[i].st) {
            stMnemonic = stEnum[i].mnemonic;
            break;
        }
    Serial.println("WiFi.Status(" + String(stMnemonic)+ ")");
    Serial.print("> ");
}

void showWiFiMode() {
    Serial.print("WiFi.mode(");
    switch (wifi_get_opmode()) {
    case WIFI_STA:
        Serial.print("STA");
        break;
    case WIFI_AP:
        Serial.print("AP");
        break;
    case WIFI_AP_STA:
        Serial.print("AP_STA");
        break;
    case WIFI_OFF:
        Serial.print("OFF");
        break;
    default:
        Serial.print("?");
    }
    Serial.println(")");
}

void smartConfig() {
    if (wifi_get_opmode() != WIFI_STA) {
        Serial.println("[warning] WIFI_STA not available.");
    }
    String	direction(Cmd.next());
    direction.toLowerCase();
    if (direction == "start") {
        Serial.println("WiFi.beginSmartConfig");
        if (WiFi.beginSmartConfig()) {
            Serial.println("SmartConfig started, confirm on your smart-device.");
            PilotLED.Start(200, 100);
            inSmartConfig = true;
        }
        else { Serial.println("Fail"); }
    }
    else if (direction == "stop") {
        Serial.println("WiFi.stopSmartConfig");
        if (WiFi.stopSmartConfig()) {
            Serial.println("SmartConfig stopped.");
            inSmartConfig = false;
        }
        else { Serial.println("Fail"); }
    }
    else if (direction == "done") {
        Serial.println("WiFi.smartConfigDone");
        if (WiFi.smartConfigDone()) {
            Serial.println("true");
            inSmartConfig = false;
        }
        else { Serial.println("false"); }
    }
    else { Serial.println(); }
    Serial.print("> ");
}

void softAP() {
    char*	c_ssid = Cmd.next();
    String	ssid(c_ssid);
    if (ssid == "disconn" || ssid == "discon" || ssid == "disconnect") {
        Serial.println("WiFi.softAPdisconnect");
        Serial.println(WiFi.softAPdisconnect() ? "OK" : "Fail");
    }
    else {
        Serial.print("WiFi.softAP(");
        Serial.print(ssid);
        char*	c_pass = Cmd.next();
        String	pass = String(c_pass);
        if (pass.length())
            Serial.print("," + pass);
        Serial.println(")");
        if (WiFi.softAP(ssid.c_str(), pass.c_str())) {
            unsigned long ts = millis();
            while (WiFi.softAPIP() == IPAddress(0, 0, 0, 0)) {
                if (millis() - ts > 5000)
                    break;
                yield();
                delay(100);
            }
#ifdef ARDUINO_ESP8266_RELEASE_2_4_0
            // This callback is available only esp8266 core 2.4.0
            WiFi.onSoftAPModeStationConnected([](const WiFiEventSoftAPModeStationConnected& e) {
                Serial.println();
                Serial.println("Station:<" + toMacAddress(e.mac) + "> connected");
                Serial.print("> "); });
            // This callback is available only esp8266 core 2.4.0
            WiFi.onSoftAPModeStationDisconnected([](const WiFiEventSoftAPModeStationDisconnected& e) {
                Serial.println();
                Serial.println("Station:<" + toMacAddress(e.mac) + "> disconnected");
                Serial.print("> "); });
#endif
            Serial.print("[info] softAPIP:");
            Serial.println(WiFi.softAPIP());
            Serial.println("OK");
        }
        else { Serial.println("Fail"); }
    }
    Serial.print("> ");
}

void softAPConfig() {
    IPAddress	apIP(192,168,4,1);
    IPAddress	gwIP(192,168,4,1);
    IPAddress	nmIP(255,255,255,0);
    String	s_apIP = String(Cmd.next());
    String	s_gwIP = String(Cmd.next());
    String	s_nmIP = String(Cmd.next());
    if (s_apIP.length() > 0)
        apIP.fromString(s_apIP);
    if (s_gwIP.length() > 0)
        gwIP.fromString(s_gwIP);
    if (s_nmIP.length() > 0)
        nmIP.fromString(s_nmIP);
    Serial.println("nm");
    Serial.print("WiFi.softAIPConfig(");
    Serial.print(s_apIP + ",");
    Serial.print(s_gwIP + ",");
    Serial.println(s_nmIP + ")");
    Serial.println(WiFi.softAPConfig(apIP, gwIP, nmIP) ? "OK" : "Fail");
    Serial.print("> ");
}

void startServer() {
    char*	s_type = Cmd.next();
    String	sType = String(s_type);
    sType.toLowerCase();
    if (sType == "web") {
        Serial.println("WebServer.begin");
        if (WebServer == nullptr) {
            WebServer = new ESP8266WebServer(80);
        }
        if (webHandler == nullptr) {
            webHandler = new httpHandler();
            WebServer->addHandler(webHandler);
        }
        WebServer->begin();
        Serial.println("OK");
    }
    else if (sType == "dns") {
        uint16_t	port = 53;
        String		domain = String(Cmd.next());
        Serial.print("DnsServer.start(");
        Serial.print(port);
        Serial.print("," + domain + ",");
        Serial.print(WiFi.softAPIP());
        Serial.println(")");
        DnsServer.setErrorReplyCode(DNSReplyCode::NoError);
        if (DnsServer.start(port, domain, WiFi.softAPIP())) {
            onDnsserver = true;
            Serial.println("OK");
        }
        else {
            onDnsserver = false;
            Serial.println("Fail");
        }
    }
    else if (sType == "mdns") {
        char*	c_mdnsname = Cmd.next();
        if (strlen(c_mdnsname))
            MDNSHostName = String(c_mdnsname);
        Serial.print("mDNS.begin(");
        Serial.print(MDNSHostName);
        Serial.println(")");
        if (MDNS.begin(MDNSHostName.c_str())) {
            Serial.println("OK");
            char*	c_mdnsservice = Cmd.next();
            if (c_mdnsservice)
                MDNSServiceName = String(c_mdnsservice);
            char*	c_mdnsprotocol = Cmd.next();
            if (c_mdnsprotocol)
                MDNSProtocolName = String(c_mdnsprotocol);
            char*	c_port = Cmd.next();
            int	mDNSServicePort = 0;
            if (c_port)
                mDNSServicePort = String(c_port).toInt();
            else if (MDNSServiceName == "http")
                mDNSServicePort = 80;
            Serial.print("> mDNS.addService(");
            Serial.print(MDNSServiceName);
            Serial.print(",");
            Serial.print(MDNSProtocolName);
            Serial.print(',');
            Serial.println(String(mDNSServicePort) + ")");
            if (MDNSServiceName.length() && MDNSProtocolName.length()) {
                MDNS.addService(MDNSServiceName, MDNSProtocolName, mDNSServicePort);
                Serial.println("OK");
            }
            else {
                Serial.println("Fail, Server or Protocol no specified");
            }
        }
        else {
            Serial.println("Fail");
        }
    }
    else
        Serial.println("Unknown server");
    Serial.print("> ");
}

void station() {
    Serial.println("WiFi.softAPgetStationNum");
    Serial.println("Soft AP station num:" + String(WiFi.softAPgetStationNum()));
    struct station_info* station = wifi_softap_get_station_info();
    while (station) {
        Serial.print("BSSID<" + toMacAddress(station->bssid) + "> IP:");
        IPAddress stationIP(IP2STR(&station->ip));
        Serial.println(stationIP);
        station = STAILQ_NEXT(station, next);
    }
    wifi_softap_free_station_info();
    Serial.print("> ");
}

void stopServer() {
    char*	c_type = Cmd.next();
    String	type = String(c_type);
    type.toLowerCase();
    if (type == "web") {
        if (WebServer != nullptr) {
            Serial.println("Stop web server");
            delete WebServer;
            WebServer = nullptr;
            webHandler = nullptr;
            Serial.println("OK");
        }
    }
    else if (type == "dns") {
        if (onDnsserver) {
            Serial.println("Stop dns server");
            DnsServer.stop();
            Serial.println("OK");
            onDnsserver = false;
        }
    }
    else {
        Serial.println("Unknown server");
    }
    Serial.print("> ");
}

String toMacAddress(const uint8_t mac[]) {
    String	macAddr = "";
    for (uint8_t i = 0; i < 6; i++) {
        char	buf[3];
        sprintf(buf, "%02X", mac[i]);
        macAddr += buf;
        if (i < 5)
            macAddr += ':';
    }
    return macAddr;
}

static const char* httpHeaders[] PROGMEM = {
    "Access-Control-Allow-Credentials",
    "Access-Control-Allow-Headers",
    "Access-Control-Allow-Methods",
    "Access-Control-Allow-Origin",
    "Access-Control-Expose-Headers",
    "Access-Control-Max-Age",
    "Access-Patch",
    "Access-Ranges",
    "Age",
    "Allow",
    "Alt-Svc",
    "Cache-Control",
    "Connection",
    "Content-Disposition",
    "Content-Encoding",
    "Content-Length",
    "Content-Location",
    "Content-MD5",
    "Content-Range",
    "Content-Security-Policy",
    "Content-Type",
    "Date",
    "ETag",
    "Expires",
    "Keep-Alive"
    "Last-Modified",
    "Link",
    "Location",
    "Pragma",
    "Proxy-Authenticate",
    "Public-Key-Pins",
    "Refresh",
    "Retry-After",
    "Server",
    "Set-Cookie",
    "Strict-Transport-Security",
    "Trailer",
    "Transfer-Encoding",
    "Tk",
    "Upgrade",
    "Upgrade-Insecure-Requests",
    "Vary",
    "Via",
    "Warning",
    "WWW-Authenticate",
    "X-Frame-Options",
    "X-Content-Duration",
    "X-Content-Security-Policy",
    "X-Content-Type-Options",
    "X-Powered-By",
    "X-Request-ID",
    "X-Correlation-ID",
    "X-UA-Compatible",
    "X-XSS-Protection",
    "X-WebKit-CSP"
};

void http() {
    String	req = String(Cmd.next());
    req.toLowerCase();
    if (req == "get") {
        String	url = String(Cmd.next());
        Serial.print("GET ");
        Serial.println(url);
        PilotLED.Start(200, 100);
        if (client.begin(url)) {
            client.collectHeaders(httpHeaders, sizeof(httpHeaders) / sizeof(const char*));
            int code = client.GET();
            Serial.println(String(code) + ':');
            for (uint8_t i = 0; i < (uint8_t)client.headers(); i++) {
                if (client.hasHeader(client.headerName(i).c_str()))
                    Serial.println(client.headerName(i) + ':' + client.header(i));
            }
            if (code == HTTP_CODE_OK) {
                uint8_t	buff[128] = { 0 };
                WiFiClient* contentStream = client.getStreamPtr();
                int contentLength = client.getSize();
                while (client.connected() && (contentLength > 0 || contentLength == -1)) {
                    size_t	availSize = contentStream->available();
                    if (availSize) {
                        int	blkSize = contentStream->readBytes(buff, ((availSize > sizeof(buff)) ? sizeof(buff) : availSize));
                        Serial.write(buff, blkSize);
                        if (contentLength > 0)
                            contentLength -= blkSize;
                    }
                    delay(1);
                }
            }
            client.end();
        }
        else { Serial.print("Fail"); }
        PilotLED.Start(800, 600);
        Serial.println();
    }
    else if (req == "on") {
        if (webHandler == nullptr) {
            webHandler = new httpHandler();
            if (WebServer == nullptr)
                WebServer = new ESP8266WebServer(80);
            WebServer->addHandler(webHandler);
        }
        String	uri = String(Cmd.next());
        Serial.print("ON ");
        Serial.print(uri);
        if (uri.length() > 0) {
            char*	c_uriPool = (char*)malloc(uri.length() + 1);
            if (c_uriPool) {
                uri.toCharArray(c_uriPool, uri.length() + 1);
                String	content = "";
                char*	c_content;
                while ((c_content = Cmd.next())) {
                    if (content.length() > 0)
                        content += ' ';
                    content.concat(c_content);
                }
                char*	c_contentPool = (char*)malloc(content.length() + 1);
                if (c_contentPool) {
                    content.toCharArray(c_contentPool, content.length() + 1);
                    PageElement	*content;
                    content = new PageElement(c_contentPool);
                    PageBuilder *page;
                    page = new PageBuilder();
                    page->setUri(c_uriPool);
                    page->addElement(*content);
                    page->insert(*WebServer);
                    Serial.print(' ');
                    Serial.println(c_contentPool);
                }
            }
        }
    }
    Serial.print("> ");
}

typedef struct _eventType {
    const WiFiEvent_t event;
    const char* desc;
} eventTypeS;
static const eventTypeS wifiEvents[] = {
    { WIFI_EVENT_STAMODE_CONNECTED, "WIFI_EVENT_STAMODE_CONNECTED" },
    { WIFI_EVENT_STAMODE_DISCONNECTED, "WIFI_EVENT_STAMODE_DISCONNECTED" },
    { WIFI_EVENT_STAMODE_AUTHMODE_CHANGE, "WIFI_EVENT_STAMODE_AUTHMODE_CHANGE" },
    { WIFI_EVENT_STAMODE_GOT_IP, "WIFI_EVENT_STAMODE_GOT_IP" },
    { WIFI_EVENT_STAMODE_DHCP_TIMEOUT, "WIFI_EVENT_STAMODE_DHCP_TIMEOUT" },
    { WIFI_EVENT_SOFTAPMODE_STACONNECTED, "WIFI_EVENT_SOFTAPMODE_STACONNECTED" },
    { WIFI_EVENT_SOFTAPMODE_STADISCONNECTED, "WIFI_EVENT_SOFTAPMODE_STADISCONNECTED" },
    { WIFI_EVENT_SOFTAPMODE_PROBEREQRECVED, "WIFI_EVENT_SOFTAPMODE_PROBEREQRECVED" },
    { WIFI_EVENT_MAX, "WIFI_EVENT_MAX" },
    { WIFI_EVENT_ANY, "WIFI_EVENT_ANY" },
    { WIFI_EVENT_MODE_CHANGE, "WIFI_EVENT_MODE_CHANGE" }
};

void wifiEvent(WiFiEvent_t event) {
    if (notifyEvent) {
        Serial.print("[event] " + String((int)event));
        for (uint8_t i = 0; i <= sizeof(wifiEvents) / sizeof(eventTypeS); i++) {
            if (wifiEvents[i].event == event) {
                Serial.println(" (" + String(wifiEvents[i].desc) + ")");
                break;
            }
        }
        Serial.print("> ");
    }
}

typedef struct _command {
    const char*	name;
    const char*	option;
    void(*func)();
} commandS;
static const commandS	commands[] = {
    { "apconfig", "[AP_IP] [GW_IP] [NETMASK]", softAPConfig },
    { "autoconnect", "on|off", autoConnect },
    { "begin", "[SSID [PASSPHRASE]] [#wait]", beginWiFi },
    { "delay", "MILLISECONDS", doDelay },
    { "discon", "[ap]", disconnWiFi },
    { "event", "on|off", doEvent },
    { "fs", "start|dir|{file PATH}|format|info|{remove PATH}|{rename PATH NEW_PATH}", fileSystem },
    { "gpio", "{get PORT_NUM}|{set PORT_NUM high|low}", gpio },
    { "heap", "", freeHeap },
    { "http", "{get url}|{on uri PAGE_CONTENT}", http },
    { "mode", "ap|sta|apsta|off", setWiFiMode },
    { "persistent", "on|off", setPersistent },
    { "reset", "", reset },
    { "scan", "", scan },
    { "sleep", "none|light|modem|{deep SLEEP_TIME}", setSleep },
    { "smartconfig", "start|stop|done", smartConfig },
    { "show", "", showConfig },
    { "softap", "{SSID [PASSPHRASE]}|discon", softAP },
    { "start", "web|{dns domain}|{mdns HOST_NAME SERVICE PROTOCOL [PORT]}", startServer },
    { "station", "", station },
    { "status", "", showStatus },
    { "stop", "dns|web", stopServer },
    { "wps", "", beginWPS }
};

void unrecognized(const char* cmd) {
    Serial.println();
    Serial.print("> ");
}

void help() {
    Serial.println("Commands:");
    for (uint8_t i = 0; i < sizeof(commands) / sizeof(struct _command); i++)
        Serial.println(String(commands[i].name) + ' ' + String(commands[i].option));
    Serial.print("> ");
}

void setup() {
    delay(1000);
    Serial.begin(BAUDRATE);
    Serial.println();

    for (uint8_t i = 0; i < sizeof(commands) / sizeof(struct _command); i++)
        Cmd.addCommand(commands[i].name, commands[i].func);
    Cmd.addCommand("help", help);
    Cmd.addCommand("?", help);
    Cmd.setDefaultHandler(unrecognized);
    Serial.printf("ESPShaker %s - %08x,Flash:%u,SDK%s\r\n", _VERSION, ESP.getChipId(), ESP.getFlashChipRealSize(), system_get_sdk_version());
    Serial.println("Type \"help\", \"?\" for commands list.");

    showWiFiMode();
    WiFi.onEvent(wifiEvent, WIFI_EVENT_ANY);
}

void loop() {
    wl_status_t	cs = WiFi.status();
    if (cs != WiFiStatus) {
        PilotLED.Start(800, 600);
        WiFiStatus = cs;
        showStatus();
        if (WiFiStatus == WL_CONNECTED) {
            if (inSmartConfig) {
                Serial.println("Smart Config completed, SSID:" + WiFi.SSID());
                Serial.print("WiFi.LocalIP:");
                Serial.println(WiFi.localIP());
                inSmartConfig = false;
            }
        }
    }
    
    WiFiMode_t	cm = WiFi.getMode();
    if (cm != WiFiMode) {
        if (cm == WIFI_OFF) {
            PilotLED.Stop();
            digitalWrite(ONBOARD_LED, ONBOARD_LED_OFF);
        }
        else { PilotLED.Start(); }
        WiFiMode = cm;
    }

    Cmd.readSerial();

    if (onDnsserver)
        DnsServer.processNextRequest();
    if (WebServer != nullptr)
        WebServer->handleClient();
}
