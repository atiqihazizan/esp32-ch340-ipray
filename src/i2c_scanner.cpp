/*
 * I2C Scanner untuk ESP32
 * Scan semua I2C devices yang disambung
 */

 #include <Arduino.h>
 #include <Wire.h>
 
 void setup() {
   Serial.begin(115200);
   delay(2000);
   
   Serial.println("\n=== I2C Scanner ===");
   Serial.println("Scanning I2C bus...\n");
   
   Wire.begin(21, 22);  // SDA=21, SCL=22 (default ESP32)
   
   byte error, address;
   int nDevices = 0;
   
   for(address = 1; address < 127; address++) {
     Wire.beginTransmission(address);
     error = Wire.endTransmission();
     
     if (error == 0) {
       Serial.print("I2C device found at address 0x");
       if (address < 16) Serial.print("0");
       Serial.print(address, HEX);
       Serial.print(" (");
       Serial.print(address);
       Serial.print(")");
       
       // Identify common devices
       switch(address) {
         case 0x3C:
         case 0x3D:
           Serial.print(" - OLED Display (SSD1306)");
           break;
         case 0x68:
           Serial.print(" - RTC DS3231 / MPU6050");
           break;
         case 0x57:
           Serial.print(" - RTC DS3231 EEPROM");
           break;
         case 0x27:
         case 0x3F:
           Serial.print(" - LCD I2C");
           break;
         case 0x76:
         case 0x77:
           Serial.print(" - BMP280 / BME280");
           break;
         case 0x48:
           Serial.print(" - ADS1115 ADC");
           break;
         case 0x20:
         case 0x21:
           Serial.print(" - PCF8574 I/O Expander");
           break;
       }
       Serial.println();
       nDevices++;
     }
     else if (error == 4) {
       Serial.print("Unknown error at address 0x");
       if (address < 16) Serial.print("0");
       Serial.println(address, HEX);
     }
   }
   
   Serial.println();
   if (nDevices == 0)
     Serial.println("No I2C devices found");
   else {
     Serial.print("Found ");
     Serial.print(nDevices);
     Serial.println(" device(s)");
   }
   
   Serial.println("\n=== Scan Complete ===");
 }
 
 void loop() {
   // Scan setiap 5 saat
   delay(5000);
   Serial.println("\n--- Scanning again ---");
   setup();
 }
 