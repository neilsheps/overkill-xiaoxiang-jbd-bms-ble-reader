# xiaoxiang-jbd-bms-ble-reader

This simple code reads a XiaoXiang BMS's cell voltages and summary information (overall voltage, current, SOC% and other fields) through BLE.   It works on Adafruit's nrf52 based devices.

I've keep code splatter to a minimum, and this single C++ file (it can be renamed as a .ino) Does this:
- Scans for devices 
- Finds BMSes named XiaoXiang and with the required service (0000ff00-0000-1000-8000-00805f9b34fb)
- Initiates a connection with them
- On connection, discovers the service and a Rx then Tx characteristic that will allow communication with the BMS
- Every five seconds send a command (which alternates between 0x03 and 0x04) to the BMS
- Receives notifications
- Concatenates messages requiring more than the 20 bytes each notification provides
- Prints out the data received in human readable form

Copyright Neil Shepherd 2022
Released under MIT license
 
I acknowledge several other sources of information and inspiration: 

https://wnsnty.xyz/entry/jbd-xiaoyang-smart-bluetooth-bms-information - a central repository of a number of links

https://www.dropbox.com/s/03vfqklw97hziqr/%E9%80%9A%E7%94%A8%E5%8D%8F%E8%AE%AE%20V2%20%28%E6%94%AF%E6%8C%8130%E4%B8%B2%29%28Engrish%29.xlsx?dl=0
^^^ a spreadsheet that references all the commands the BMS can accept, the format of commands and corresponding responses, and the checksum calculation

https://github.com/kolins-cz/Smart-BMS-Bluetooth-ESP32 - a version using an ESP32

https://learn.adafruit.com/bluefruit-nrf52-feather-learning-guide/custom-central-hrm - Adafruit's hrm example, which is similar in methodology
