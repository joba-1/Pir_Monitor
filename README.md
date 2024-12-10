# Pir Monitor

Monitor status pin of a PIR sensor and send mail on movement with a gmail account.

If you need help with setting up a gmail account and an app password for this,
see https://randomnerdtutorials.com/esp32-send-email-smtp-server-arduino-ide/ 

## Parts

any ESP32 and any PIR sensor with a 3.3V status pin will do. I used these:
* https://doc.riot-os.org/group__boards__esp32__mh-et-live-minikit.html
* https://eckstein-shop.de/HC-SR501PIRInfrarotBewegungsmelderMotionSensorModulArduinoRaspberryPi

## Connections

| ESP32  | PIR |
|--------|-----|
| Vcc 5V | Vcc |
| Gnd    | Gnd |
| IO16*  | Out |

\* any free IO pin will do, just match PIR_PIN in main.cpp