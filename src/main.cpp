#include <Arduino.h>

#include <WiFiManager.h>

// based on https://randomnerdtutorials.com/esp32-send-email-smtp-server-arduino-ide/
#include <ESP_Mail_Client.h>

// Web Updater
#include <HTTPUpdateServer.h>
#include <WebServer.h>

#include <EEPROM.h>


#define LED_PIN LED_BUILTIN
#define PIR_PIN 16

#define SMTP_HOST "smtp.gmail.com"
#define SMTP_PORT 465

// Web page and OTA updater
#define WEBSERVER_PORT 80


WebServer web_server(WEBSERVER_PORT);
HTTPUpdateServer esp_updater;

int pir_status = HIGH;  // ignore if active after reset/boot

bool active = false;
uint32_t active_since = 0;
const uint32_t active_delay = 10000;  // keep active for /status page

const uint32_t mail_delay = 60000;  // send mails at most once each minute

volatile bool send_message = false;

/* Declare the global used SMTP objects */
SMTPSession smtp;
Session_Config config;
SMTP_Message message;

/* 
- Create new Google Mail account
- activate 2 factor auth (needs tel for verification sms)
- create app password (needs 2 factor auth)
- set in web page, stored in EEPROM/Flash
*/
char sender[100] = "";    // like my.name for my.name@gmail.com
char password[100] = "";  // 16 char app password of my.name account

/* Recipient's email*/
char receiver[100] = "";  // free text
char email[100] = "";     // any full email address


/* Callback function to get the Email sending status */
void smtpCallback( SMTP_Status status ) {
  /* Print the current status */
  Serial.println(status.info());

  /* Print the sending result */
  if (status.success()) {
    Serial.println("----------------");
    Serial.printf("Message sent success: %d\n", status.completedCount());
    Serial.printf("Message sent failed: %d\n", status.failedCount());
    Serial.println("----------------\n");

    for (size_t i = 0; i < smtp.sendingResult.size(); i++) {
      /* Get the result item */
      SMTP_Result result = smtp.sendingResult.getItem(i);

      Serial.printf("Message No: %d\n", i + 1);
      Serial.printf("Status: %s\n", result.completed ? "success" : "failed");
      Serial.printf("Send Time: %s\n", MailClient.Time.getDateTimeString(result.timestamp, "%Y-%m-%d %H:%M:%S").c_str());
      Serial.printf("Recipient: %s\n", result.recipients.c_str());
      Serial.printf("Subject: %s\n", result.subject.c_str());
    }
    Serial.println("----------------\n");

    // You need to clear sending result as the memory usage will grow up.
    smtp.sendingResult.clear();
  }
}


void sendTaskCode( void * ) {
  while (true) {
    if (send_message) {
      if (!smtp.connected() && !smtp.connect(&config)) {
        Serial.printf("Connection error, Status Code: %d, Error Code: %d, Reason: %s\n", smtp.statusCode(), smtp.errorCode(), smtp.errorReason().c_str());
      }
      else {
        if (!MailClient.sendMail(&smtp, &message) ) {
          Serial.printf("Send error, Status Code: %d, Error Code: %d, Reason: %s\n", smtp.statusCode(), smtp.errorCode(), smtp.errorReason().c_str());
        }
        else {
          smtp.closeSession();
          send_message = false;
        }
      }
    }
    delay(100);
  }
}


void sendAlarm( const char *msg ) {
  static unsigned count = 0;
  count++;
  if (sender[0] && password[0] && receiver[0] && email[0]) {
    char content[1024];
    snprintf(content, sizeof(content), "%s Alarm %u\n%s", HOSTNAME, count, msg);
    message.text.content = content;
    send_message = true;
  }
}


bool email_config(bool write = false) {
  const uint32_t magic = 0xe1ee7;
  size_t got_bytes = 0;
  size_t want_bytes = sizeof(sender) + sizeof(password) + sizeof(receiver) + sizeof(email);
  if (EEPROM.begin(want_bytes + sizeof(magic))) {
    if (write) {
      got_bytes = EEPROM.writeBytes(got_bytes, sender, sizeof(sender));
      got_bytes += EEPROM.writeBytes(got_bytes, password, sizeof(password));
      got_bytes += EEPROM.writeBytes(got_bytes, receiver, sizeof(receiver));
      got_bytes += EEPROM.writeBytes(got_bytes, email, sizeof(email));
      EEPROM.writeULong(want_bytes, magic);
      EEPROM.commit();
    }
    else {
      got_bytes = EEPROM.readBytes(got_bytes, sender, sizeof(sender));
      got_bytes += EEPROM.readBytes(got_bytes, password, sizeof(password));
      got_bytes += EEPROM.readBytes(got_bytes, receiver, sizeof(receiver));
      got_bytes += EEPROM.readBytes(got_bytes, email, sizeof(email));
      if (EEPROM.readULong(want_bytes) != magic) {
        got_bytes = 0;
        sender[0] = password[0] = receiver[0] = email[0] = '\0';
      }
    }
    EEPROM.end();
  }
  return got_bytes == want_bytes;
}


void restart( const char *msg ) {
  Serial.println(msg);
  Serial.flush();
  delay(100);
  ESP.restart();
  while (true);
}


const char *main_page() {
  static const char fmt[] =
    "<!doctype html>\n"
    "<html lang=\"en\">\n"
    " <head>\n"
    "  <title>" PROGNAME " v" VERSION "</title>\n"
    "  <meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">\n"
    "  <meta charset=\"utf-8\">\n"
    "  <link href=\"data:image/png;base64,iVBORw0KGgoAAAANSUhEUgAAABAAAAAQAgMAAABinRfyAAAADFBMVEUqYbutnpTMuq/70SQgIef5AAAAVUlEQVQIHWOAAPkvDAyM3+Y7MLA7NV5g4GVqKGCQYWowYTBhapBhMGB04GE4/0X+M8Pxi+6XGS67XzzO8FH+iz/Dl/q/8gx/2S/UM/y/wP6f4T8QAAB3Bx3jhPJqfQAAAABJRU5ErkJggg==\" rel=\"icon\" type=\"image/x-icon\" />\n"
    " </head>\n"
    " <body>\n"
    "  <h1>" PROGNAME " v" VERSION "</h1>\n"
    "  <table><form action=\"email\" method=\"post\">\n"
    "   <tr><td>Sender</td><td><input type=\"text\" id=\"sender\" name=\"sender\" value=\"%s\" /></td><td>@gmail.com</td></tr>\n"
    "   <tr><td>App Password</td><td><input type=\"password\" id=\"password\" name=\"password\" value=\"%s\" /></td><td>16 characters</td></tr>\n"
    "   <tr><td>Receiver Name</td><td><input type=\"text\" id=\"receiver\" name=\"receiver\" value=\"%s\" /></td><td>your choice</td></tr>\n"
    "   <tr><td>Receiver Email</td><td><input type=\"text\" id=\"email\" name=\"email\" value=\"%s\" /></td><td>full email</td></tr>\n"
    "   <tr><td><input type=\"submit\" name=\"config\" value=\"Set Config\" /></td><td><input type=\"submit\" name=\"test\" value=\"Test Config\" /></td><td></td></tr>\n"
    "  </form></table>\n"
    "  <p><table>\n"
    "   <tr><td>Post firmware image to</td><td><a href=\"/update\">/update</a></td></tr>\n"
    "   <tr><td>Live status on</td><td><a href=\"/status\">/status</a></td></tr>\n"
    "  </table></p>\n"
    "  <p><table><tr>\n"
    "   <td><form action=\"restart\" method=\"post\">\n"
    "    <input type=\"submit\" name=\"restart\" value=\"Restart device\" />\n"
    "   </form></td>\n"
    "   <td><form action=\"reset\" method=\"post\">\n"
    "    <input type=\"submit\" name=\"reset\" value=\"Reset WLAN\" />\n"
    "   </form></td>\n"
    "  </tr></table></p>\n"
    "  <p><small>... by <a href=\"https://github.com/joba-1/Pir_Monitor\">Joachim Banzhaf</a>, " __DATE__ " " __TIME__ "</small></p>\n"
    " </body>\n"
    "</html>\n";
  static char page[sizeof(fmt) + sizeof(sender) + sizeof(password) + sizeof(receiver) + sizeof(email)] = "";
  snprintf(page, sizeof(page), fmt, sender, password, receiver, email); 
  return page;
}


bool update_arg( const char *next, char *prev, size_t size ) {
  if (strcmp(prev, next) && strlen(next) < size) {
    strcpy(prev, next);
    return true;
  }
  return false;
}


void update_config() {
  config.login.email = MB_String(sender) + "@gmail.com";
  config.login.password = password;
  message.sender.email = email;
  message.clearRecipients();
  message.addRecipient(receiver, email);
}


void setup_webserver() {
  web_server.on("/email", HTTP_POST, []() {
    bool changed = false;
    for (int i=0; i<web_server.args(); i++) {
      if (web_server.argName(i) == "sender") {
        changed |= update_arg(web_server.arg(i).c_str(), sender, sizeof(sender));
      }
      else if (web_server.argName(i) == "password") {
        changed |= update_arg(web_server.arg(i).c_str(), password, sizeof(password));
      }
      else if (web_server.argName(i) == "receiver") {
        changed |= update_arg(web_server.arg(i).c_str(), receiver, sizeof(receiver));
      }
      else if (web_server.argName(i) == "email") {
        changed |= update_arg(web_server.arg(i).c_str(), email, sizeof(email));
      }
    }

    if (changed) {
      update_config();
      email_config(true);
    }

    if (web_server.hasArg("test")) {
      sendAlarm("Test");
      Serial.println("Send test mail initiated");
    }

    web_server.sendHeader("Location", "/", true);  
    web_server.send(302, "text/plain", "");
  });

  web_server.on("/reset", HTTP_POST, []() {
    Serial.println("Reset WLAN config on web request");
    
    WiFiManager wm;
    wm.resetSettings();
    
    web_server.send(200, "text/html",
      "<html>\n"
      " <head>\n"
      "  <title>" PROGNAME " v" VERSION " Reset WLAN Config</title>\n"
      "  <meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">\n"
      "  <meta charset=\"utf-8\">\n"
      "  <link href=\"data:image/png;base64,iVBORw0KGgoAAAANSUhEUgAAABAAAAAQAgMAAABinRfyAAAADFBMVEUqYbutnpTMuq/70SQgIef5AAAAVUlEQVQIHWOAAPkvDAyM3+Y7MLA7NV5g4GVqKGCQYWowYTBhapBhMGB04GE4/0X+M8Pxi+6XGS67XzzO8FH+iz/Dl/q/8gx/2S/UM/y/wP6f4T8QAAB3Bx3jhPJqfQAAAABJRU5ErkJggg==\" rel=\"icon\" type=\"image/x-icon\" />\n"
      "  <meta http-equiv=\"refresh\" content=\"7; url=/\"> \n"
      " </head>\n"
      " <body><h1>Reset WLAN Config</h1></body>\n"
      "</html>\n");
    restart("Reset WLAN config on web request");
  });

  web_server.on("/restart", HTTP_POST, []() {
    web_server.send(200, "text/html",
      "<html>\n"
      " <head>\n"
      "  <title>" PROGNAME " v" VERSION " Restart</title>\n"
      "  <meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">\n"
      "  <meta charset=\"utf-8\">\n"
      "  <link href=\"data:image/png;base64,iVBORw0KGgoAAAANSUhEUgAAABAAAAAQAgMAAABinRfyAAAADFBMVEUqYbutnpTMuq/70SQgIef5AAAAVUlEQVQIHWOAAPkvDAyM3+Y7MLA7NV5g4GVqKGCQYWowYTBhapBhMGB04GE4/0X+M8Pxi+6XGS67XzzO8FH+iz/Dl/q/8gx/2S/UM/y/wP6f4T8QAAB3Bx3jhPJqfQAAAABJRU5ErkJggg==\" rel=\"icon\" type=\"image/x-icon\" />\n"
      "  <meta http-equiv=\"refresh\" content=\"7; url=/\"> \n"
      " </head>\n"
      " <body><h1>Restart</h1></body>\n"
      "</html>\n");
    restart("Restart on web request");
  });

  web_server.on("/status", []() {
    const char fmt[] = 
      "<html>\n"
      " <head>\n"
      "  <title>" PROGNAME " v" VERSION " Live Status</title>\n"
      "  <meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">\n"
      "  <meta charset=\"utf-8\">\n"
      "  <link href=\"data:image/png;base64,iVBORw0KGgoAAAANSUhEUgAAABAAAAAQAgMAAABinRfyAAAADFBMVEUqYbutnpTMuq/70SQgIef5AAAAVUlEQVQIHWOAAPkvDAyM3+Y7MLA7NV5g4GVqKGCQYWowYTBhapBhMGB04GE4/0X+M8Pxi+6XGS67XzzO8FH+iz/Dl/q/8gx/2S/UM/y/wP6f4T8QAAB3Bx3jhPJqfQAAAABJRU5ErkJggg==\" rel=\"icon\" type=\"image/x-icon\" />\n"
      "  <meta http-equiv=\"refresh\" content=\"3; url=/status\"> \n"
      " </head>\n"
      " <body><h1%s>PIR %s</h1></body>\n"
      "</html>\n";
    char msg[sizeof(fmt) + 100];
    snprintf(msg, sizeof(msg), fmt, active ? " style=\"color: red\"" : "", active ? "ACTIVATED" : "inactive");
    web_server.send(200, "text/html", msg);
  });

  web_server.on("/", []() { 
    web_server.send(200, "text/html", main_page());
  });

  web_server.onNotFound( []() { 
    web_server.send(404, "text/html", main_page()); 
  });

  web_server.begin();
}


void setup() {
  Serial.begin(BAUDRATE);
  Serial.println("\nStarting " PROGNAME " v" VERSION " " __DATE__ " " __TIME__);

  String host(HOSTNAME);
  host.toLowerCase();
  WiFi.hostname(host.c_str());
  WiFi.mode(WIFI_STA);  // after WiFi.hostname() or dhcp gets default name "ESP..."!

  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, HIGH);
  pinMode(PIR_PIN, INPUT_PULLDOWN);

  email_config();

  WiFiManager wm;
  wm.setHostname(host);
  wm.setConfigPortalTimeout(180);
  if (!wm.autoConnect(WiFi.getHostname(), WiFi.getHostname())) {
    restart("Failed to connect WLAN, about to reset");
  }
  Serial.printf("\nConnected %s with IP %s\n", WiFi.getHostname(), WiFi.localIP().toString().c_str());

  configTime(3600, 3600, "pool.ntp.org");  // MESZ

  esp_updater.setup(&web_server);
  setup_webserver();

  MailClient.networkReconnect(true);
  
  smtp.debug(1);  // serial output can be useful if send does not work
  smtp.callback(smtpCallback);

  config.server.host_name = SMTP_HOST;
  config.server.port = SMTP_PORT;
  config.login.user_domain = "";

  message.sender.name = "ESP Pir Monitor";
  message.subject = "Alarm on " HOSTNAME;
  message.text.charSet = "us-ascii";
  message.text.transfer_encoding = Content_Transfer_Encoding::enc_7bit;
  message.priority = esp_mail_smtp_priority::esp_mail_smtp_priority_high;
  message.response.notify = esp_mail_smtp_notify_success | esp_mail_smtp_notify_failure | esp_mail_smtp_notify_delay;

  update_config();

  UBaseType_t prio = uxTaskPriorityGet(NULL) - 1;
  BaseType_t core = 1 - xPortGetCoreID();
  xTaskCreatePinnedToCore(sendTaskCode, "SendTask", 10000, NULL, prio, NULL, core);

  Serial.println("Init done");
}


void loop() {
  static uint32_t last_send = -mail_delay;
  static bool valid_time = false;

  if(!valid_time && time(nullptr) >= ESP_MAIL_CLIENT_VALID_TS) {
    Serial.println("Got valid time");
    valid_time = true;
  }

  uint32_t now = millis();
  int new_status = digitalRead(PIR_PIN);
  if (new_status != pir_status) {
    pir_status = new_status;
    digitalWrite(LED_PIN, pir_status);
    Serial.printf("PIR Status: %d\n", pir_status);

    if (pir_status == HIGH) {
      active = true;

      if (now - last_send > mail_delay) {
        sendAlarm("Movement detected!");
        last_send = now;
      }

      active_since = now;
    }
  }

  if (active && now - active_since > active_delay ) {
    active = false;
  }

  web_server.handleClient();

  delay(10);
}
