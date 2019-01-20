# ESP8266 Arduino IDE Template
This is an ESP8266 template with some (hopefully) useful functions:  
- ESP DeepSleep Mode
- Watchdog
- OTA Updates through Arduino IDE
- MQTT
- Monitoring VCC

This sketch does actually nothing than blinking the LED, publish VCC to an MQTT topic and providing the functions mentioned above, so you can use it as a template for whatever you want to do.  

I think the code is well documented, some hints in advance:  
- You will *require* a MQTT broker (auth not implemented, so you will probably want to use a local broker)
- maximum DeepSleep time for ESP8266 ~ 1 hour (hardware limitation)
- If you want to use DeepSleep, don't forget to connect D0 (Pin 16) to RESET, else the ESP will never wake up
- For reading VCC, the A0 pin must be floating (unconnected)

Have fun,  
Juergen
