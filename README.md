# overkill-xiaoxiang-jbd-bms-ble-reader

This simple code reads a Overkill Solar (XiaoXiang) BMS's cell voltages and summary information (overall voltage, current, SOC% and other fields) through BLE.   It works on Adafruit's nrf52 based devices.

I've keep code splatter to a minimum, and this single C++ file (it can be renamed as a .ino) does this:
- Scans for devices 
- Finds BMSes named XiaoXiang and with the required service (0000ff00-0000-1000-8000-00805f9b34fb)
- Initiates a connection with them
- On connection, discovers the service and a Rx then Tx characteristic that will allow communication with the BMS
- Every five seconds send a command (which alternates between 0x03 and 0x04) to the BMS
- Receives notifications
- Concatenates messages requiring more than the 20 bytes each notification provides
- If no errors, it Serial.prints out the data received in human readable form

Copyright Neil Shepherd 2022
Released under MIT license
 
I acknowledge several other sources of information and inspiration: 

https://overkillsolar.com/support-downloads/ - overkill solar's respository of docs and code
https://github.com/FurTrader/OverkillSolarBMS/blob/master/Comm_Protocol_Documentation/JBD%20Protocol%20English%20version.pdf - a detailed doc behind the protocol used to read and write data

https://wnsnty.xyz/entry/jbd-xiaoyang-smart-bluetooth-bms-information - a central repository of a number of links

https://github.com/kolins-cz/Smart-BMS-Bluetooth-ESP32 - a version using an ESP32

https://learn.adafruit.com/bluefruit-nrf52-feather-learning-guide/custom-central-hrm - Adafruit's hrm example, which is similar in methodology
