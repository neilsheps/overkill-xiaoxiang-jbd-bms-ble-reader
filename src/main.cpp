#include <bluefruit.h>

/**	Adafruit nrf52 code for communicating with XiaoXiang BMS systems through BLE.  A.k.a. JBD BMS
 * It's read only and can read 0x03 and 0x04 commands (see XiaoXiang command spreadsheet referenced below)
 * but cannot write to the BMS to change settings (probably a good thing).  
 * Copyright Neil Shepherd 2022
 * Released under MIT license
 * 
 * 
 * I acknowledge several other sources of information and inspiration:
 * 
 * https://wnsnty.xyz/entry/jbd-xiaoyang-smart-bluetooth-bms-information - a central repository of a number of links
 * 
 * https://www.dropbox.com/s/03vfqklw97hziqr/%E9%80%9A%E7%94%A8%E5%8D%8F%E8%AE%AE%20V2%20%28%E6%94%AF%E6%8C%8130%E4%B8%B2%29%28Engrish%29.xlsx?dl=0
 * ^^^ a spreadsheet that references all the commands the BMS can accept, the format of commands and 
 * corresponding responses, and the checksum calculation
 * 
 * https://github.com/kolins-cz/Smart-BMS-Bluetooth-ESP32 - a version using an ESP32
 * 
 * https://learn.adafruit.com/bluefruit-nrf52-feather-learning-guide/custom-central-hrm - Adafruit's hrm example, which is similar in methodology
 */

#define MAX_BMS_DATA_CAPACITY		100

BLEClientService bmsService = BLEClientService("0000ff00-0000-1000-8000-00805f9b34fb");					//xiaoxiang bms original module
BLEClientCharacteristic bmsTx = BLEClientCharacteristic("0000ff02-0000-1000-8000-00805f9b34fb");		//xiaoxiang bms original module
BLEClientCharacteristic bmsRx = BLEClientCharacteristic("0000ff01-0000-1000-8000-00805f9b34fb");		//xiaoxiang bms original module

const uint8_t uuidDashes[16] = {0,0,0,1,0,1,0,1,0,1,0,0,0,0,0,0};			//inserts dashes at different positions when printing uuids
int bmsConnectionHandle = BLE_CONN_HANDLE_INVALID;							//connection handle reference
int ticker = 0;																//increments every 5 seconds
int bmsDataLengthReceived = 0;												//sum of bytes received per message.  
																			//Sometimes messages are >20 bytes and get split
int bmsDataLengthExpected = 0;												//Data length expected (usually in byte 3 of received message)
boolean bmsDataError = false;												//if checksum error, buffer overrun, message error, discards message
uint8_t bmsDataReceived[MAX_BMS_DATA_CAPACITY];								//where we put received data

void connectCallback(uint16_t conn_handle);
void disconnectCallback(uint16_t conn_handle, uint8_t reason);
void scanCallback(ble_gap_evt_adv_report_t* report);
void bmsNotifyCallback(BLEClientCharacteristic* chr, uint8_t* data, uint16_t len);
boolean appendBmsPacket(uint8_t * data, int dataLen);
boolean getIsChecksumValidForReceivedData(uint8_t * data);
uint16_t getChecksumForReceivedData(uint8_t * data);
void printBmsDataReceived(uint8_t * data);
void printHex(uint8_t * data, int datalen);
void printHex(uint8_t * data, int datalen, boolean reverse);
void printUuid(uint8_t * data, int datalen);


void setup() {
	Serial.begin(115200);
	delay(100);
	Serial.println("Adafruit nrf52 connecting to XiaoXiang BMS code example");
	Serial.println("-------------------------------------------------------\n");
	Bluefruit.begin(0, 1); 										// only using one central connection here, no peripheral connections
	Bluefruit.setName("BMS client device");						// the XiaoXiang BMS is the server in this context

	// essential that services and characteristics are set up in this order.  refer to Adafruit documentation
	bmsService.begin();
	bmsRx.setNotifyCallback(bmsNotifyCallback);
	bmsRx.begin();
	bmsTx.begin();

	Bluefruit.setConnLedInterval(250);
	Bluefruit.Central.setConnectCallback(connectCallback);
	Bluefruit.Central.setDisconnectCallback(disconnectCallback);

	Bluefruit.Scanner.setRxCallback(scanCallback);
	Bluefruit.Scanner.restartOnDisconnect(true);
	Bluefruit.Scanner.setInterval(160, 80); 					// in unit of 0.625 ms
	Bluefruit.Scanner.useActiveScan(true);
	Bluefruit.Scanner.start(0);                   				// 0 = Don't stop scanning after n seconds
}

/** Scan callback method
 * Kept simple here.  Checks for the name "xiaoxi"... and for the BMS's advertised service 
 * If so, it connects
 */
void scanCallback(ble_gap_evt_adv_report_t* report) {

	uint8_t buffer[32];
	memset(buffer, 0, sizeof(buffer));

	if(Bluefruit.Scanner.parseReportByType(report, BLE_GAP_AD_TYPE_COMPLETE_LOCAL_NAME, buffer, sizeof(buffer))) {

		Serial.printf("%14s %s\n", "Complete name:", buffer);
			
		if (strstr((char *)buffer, "xiaoxi") == (char *)buffer 
				&& Bluefruit.Scanner.checkReportForService(report, bmsService)) {
			Serial.printf("%14s\n", "BMS service found: ");
			printUuid((uint8_t *)bmsService.uuid._uuid128,16);
			Bluefruit.Central.connect(report);
		}
	}
	Bluefruit.Scanner.resume();
}


/** Connect callback method.  Here it's important to discover first the service and then
 * both the Tx and Rx characteristics, else you can't receive a callback when data is 
 * received, or transmit.
 * 
 * Frequently after connection, the BLE device cannot discover the services or characteristics
 * supposedly advertised; the success rate seems to be about 60% for me, so multiple
 * retries are sometimes needed 
 */ 
void connectCallback(uint16_t conn_handle) {

	//delay(100);												// discovering devices seems finicky; delay(100) seemed to work
																// better than connecting immediately for me, but I have not
																// found any documentation on why.  YMMV

	if (bmsService.discover(conn_handle)) {
		Serial.print("BMS service discovered:");
		printUuid((uint8_t * )bmsService.uuid._uuid128, 16);
	} else {
		Serial.println("BMS service not discovered, disconnecting");
		Bluefruit.disconnect(conn_handle);
		return;
	}

	if (bmsRx.discover()) {
		Serial.print("BMS Rx characteristic discovered:");
		printUuid((uint8_t * )bmsRx.uuid._uuid128, 16);
		bmsRx.enableNotify();
	} else {
		Serial.printf("BMS Rx characteristic not discovered, disconnecting\n");
		Bluefruit.disconnect(conn_handle);
		return;
	}

	if (bmsTx.discover()) {
		Serial.print("BMS Tx characteristic discovered:");
		printUuid((uint8_t * )bmsTx.uuid._uuid128, 16);
	} else {
		Serial.printf("BMS Tx characteristic not discovered, disconnecting\n");
		Bluefruit.disconnect(conn_handle);
		return;
	}
	bmsConnectionHandle = conn_handle;
}

/** Disconnect callback.  Obvious functionality
 */
void disconnectCallback(uint16_t conn_handle, uint8_t reason) {
	(void) conn_handle;
	(void) reason;

	bmsConnectionHandle = BLE_CONN_HANDLE_INVALID;
	Serial.printf("Disconnected, reason = 0x%02X\n", reason);
}


/** When data is received, this callback is called.  Typically up to 20 bytes is received at a time,
 * so larger packets need to be reconstructed
 */
void bmsNotifyCallback(BLEClientCharacteristic * chr, uint8_t* data, uint16_t len) {
	//Serial.println("Received BMS notification!");
	//printHex(data, len);

	if (bmsDataError) { return; }								// don't append anything if there's already an error

	if (bmsDataLengthReceived == 0)  {
		if (data[0] == 0xDD) {									// start of packet always starts with 0xDD
			bmsDataError = (data[2] != 0);						// 0x00 for all OK, 0x80 if error occurred
			bmsDataLengthExpected = data[3];					// Data length is always in byte [3] (zero indexed)
			if (!bmsDataError) {
				bmsDataError = !appendBmsPacket(data, len);		// if no errors, append first packet
			}
		}
	} else {
		bmsDataError = !appendBmsPacket(data, len);				// append second or greater packet
	}

	if (!bmsDataError) {
		if (bmsDataLengthReceived == bmsDataLengthExpected + 7) {// dataLengthExpected is always 7 less than total 
																// datagram if properly formed (additional bytes are for
																// start byte, status, length, checksum and end byte)

			Serial.println("Complete packet received, now must validate checksum");
			printHex(bmsDataReceived, bmsDataLengthReceived);

			if (getIsChecksumValidForReceivedData(bmsDataReceived)) {
				Serial.println("Checksums match");
				printBmsDataReceived(bmsDataReceived);
			} else {
				uint16_t checksum = getChecksumForReceivedData(bmsDataReceived);
				Serial.printf("Checksum error: received is 0x%04X, calculated is 0x%04X\n", 
					checksum, 256 * bmsDataReceived[bmsDataLengthExpected+4] + bmsDataReceived[bmsDataLengthExpected+5]);
			}
		}
	} else {
		Serial.printf("Data error: data[2] contains 0x%02d, bmsDataLengthReceived is %d\n", data[2], bmsDataLengthReceived);
	}
}

/** Appends received data.  Returns false if there's potential for buffer overrun, true otherwise
 */
boolean appendBmsPacket(uint8_t * data, int dataLen) {
	if (dataLen + bmsDataLengthReceived >= MAX_BMS_DATA_CAPACITY -1) { return false; }
	for (int i = 0; i < dataLen; i++) { bmsDataReceived[bmsDataLengthReceived++] = data[i]; }
	return true;
}

/** Returns true if calculated checksum matches checksum sent by BMS in last few bytes of message
 */
boolean getIsChecksumValidForReceivedData(uint8_t * data) {
	int checksumIndex = (int)((data[3]) + 4);
	return (getChecksumForReceivedData(data) == (data[checksumIndex] * 256 + data[checksumIndex + 1]));
}

/** Calculates checksum of a given BMS message.  Checksum runs from length byte [3] to last data byte, so 
 * is always byte[3] + 1
 */
uint16_t getChecksumForReceivedData(uint8_t * data) {
	int checksum = 0x10000;
	int dataLengthProvided = (int)data[3];
	//Serial.print("         ");
	
	for (int i = 0; i < dataLengthProvided + 1; i++) {
		checksum -= data[i + 3];								// offset to the data length byte is 3, checksum is calculated from there
		//Serial.printf("%02X ", data[i + 3]);
	}
	//Serial.println();
	return ((uint16_t)checksum);
}

void printHex(uint8_t * data, int datalen) { printHex(data, datalen, false); }
void printHex(uint8_t * data, int datalen, boolean reverse) {
	for (int i = 0; i < datalen; i++) { Serial.printf("%2d%s", (reverse ? datalen - i: i), (i < datalen-1 ? " " : "\n")); }
	for (int i = 0; i < datalen; i++) { Serial.printf("%02X%s", (uint8_t)data[(reverse ? datalen - i: i)], (i < datalen-1 ? " " : "\n")); }
}

void printUuid(uint8_t * data, int datalen) {
	for (int i = datalen - 1; i >= 0; i--) {
		Serial.printf("%02X%s", (uint8_t)data[i], (uuidDashes[datalen - i - 1] == 1 ? "-" : ""));
	}
	Serial.println();
}


void loop() {

	Serial.printf("Tick %03d: ",ticker++);

	if (bmsConnectionHandle != BLE_CONN_HANDLE_INVALID) {
		Serial.print(" bms connected, sending request for ");
		if (ticker % 2 == 0) {
			Serial.print("overall data");
			uint8_t data[7] = {0xdd, 0xa5, 0x3, 0x0, 0xff, 0xfd, 0x77};	// requests data of entire battery pack - see below
			bmsTx.write(data, sizeof(data));
		
		} else {
			Serial.print("by cell data");
			uint8_t data[7] = {0xdd, 0xa5, 0x4, 0x0, 0xff, 0xfc, 0x77}; // requests individual cell voltages only
			bmsTx.write(data, sizeof(data));
		}
		bmsDataLengthReceived = 0;
		bmsDataLengthExpected = 0;
		bmsDataError = false;

	}
	Serial.println();
	delay(5000);

}
/** Prints bms data received.  Just follow the Serial.printf code to reconstruct any voltages, temperatures etc.
 *  https://www.dropbox.com/s/03vfqklw97hziqr/%E9%80%9A%E7%94%A8%E5%8D%8F%E8%AE%AE%20V2%20%28%E6%94%AF%E6%8C%8130%E4%B8%B2%29%28Engrish%29.xlsx?dl=0
 *	^^^ has details on the data formats
 */
void printBmsDataReceived(uint8_t * data) {

	if (data[1] == 0x03) {
		
		Serial.printf("Total Volts: %4.2fV\n", ((float)(data[4] * 256 + data[5]))/100);
		Serial.printf("Current: %4.2fA\n", ((float)(data[6] * 256 + data[7]))/100);
		Serial.printf("Remaining Capacity: %4.2fAh\n", ((float)(data[8] * 256 + data[9]))/100);
		Serial.printf("Nominal Capacity: %4.2fAh\n", ((float)(data[10] * 256 + data[11]))/100);
		Serial.printf("Total cycles: %d\n", data[12] * 256 + data[13]);
		uint16_t date = data[14] * 256 + data[15];
		Serial.printf("Production date YYYY/MM/DD: %04d/%02d/%02d\n", (date >> 9)+ 2000, (date >> 5) & 0x0F, date &0x1F);

		int bmsNumberOfCells = data[25];
		for (int i = 0; i < bmsNumberOfCells; i++) {
			uint8_t b = data[16 + i / 8];
			int shift = 7 - i % 8;								// not clear is ls or ms bit is lower in index; not tested
			Serial.printf("Cell %2d %s\n", i, ((b >> shift) & 0x01) == 1 ? "balancing" : "not balancing");
		}

		Serial.print("Protection status:");
		uint16_t protectionStatus = data[20] * 256 + data[21];
		for (int i = 15; i >= 0; i --) { Serial.print((int)((protectionStatus >> i) & 0x01)); }
		Serial.println();

		Serial.printf("Software version: %7.1f\n", ((float)data[22])/10);
		Serial.printf("Remaining percent (SOC): %d%%\n", data[23]);
		Serial.printf("MOSFET state: charge %s, discharge %s\n", ((data[24] & 0x01) == 1 ? "ON" : "OFF"), ((data[24] & 0x02) == 2 ? "ON" : "OFF"));
		Serial.printf("Number of battery strings: %d\n", bmsNumberOfCells);
		
		int numberOfTemperatureSensors = data[26];
		Serial.printf("Number of temperature sensors: %d\n", numberOfTemperatureSensors);
		for (int i = 0; i < numberOfTemperatureSensors; i++) {
			float temperature = ((float)(data[27 + i *2] * 256 + data[28 + i * 2] - 2731)) / 10;
			Serial.printf("Temperature sensor %d: %4.1fC\n", i + 1, temperature);
		}
		Serial.println();

	}

	if (data[1] == 0x04) {
		bmsNumberOfCells = data[3]/2;
		for (int i = 0; i < bmsNumberOfCells; i++) {
			float millivolts = data[4 + 2 * i] * 256 + data[5 + 2 * i];
			Serial.printf("Cell %d: %1.3fV\n", i+1, (float)(millivolts / 1000));
		}
	}
}


/* Example Serial output of previous program iterations

23:14:39.947 -> Received notification!
23:14:39.947 ->  0  1  2  3  4  5  6  7  8  9 10 11 12 13 14 15 16 17 18 19
23:14:39.947 -> DD 03 00 1B 05 57 00 00 2E D6 2E E0 00 02 2A A7 00 00 00 00
23:14:39.947 -> Received notification!
23:14:39.947 ->  0  1  2  3  4  5  6  7  8  9 10 11 12 13
23:14:39.947 -> 00 01 20 64 02 04 02 0B 5F 0B 5B FB 47 77
23:14:40.986 -> Tick 014:  bms connected, sending request for overall data
23:14:41.097 -> Received notification!
23:14:41.097 ->  0  1  2  3  4  5  6  7  8  9 10 11 12 13 14
23:14:41.097 -> DD 04 00 08 0D C7 0D 2A 0D 54 0D 21 FE 5E 77
23:14:45.995 -> Tick 015:  bms connected, sending request for cell data
23:14:46.063 -> Received notification!
23:14:46.063 ->  0  1  2  3  4  5  6  7  8  9 10 11 12 13 14 15 16 17 18 19
23:14:46.098 -> DD 03 00 1B 05 56 00 00 2E D6 2E E0 00 02 2A A7 00 00 00 00
23:14:46.098 -> Received notification!
23:14:46.098 ->  0  1  2  3  4  5  6  7  8  9 10 11 12 13
23:14:46.098 -> 00 01 20 64 02 04 02 0B 5E 0B 5B FB 49 77
23:14:51.012 -> Tick 016:  bms connected, sending request for overall data
23:14:51.058 -> Received notification!
23:14:51.058 ->  0  1  2  3  4  5  6  7  8  9 10 11 12 13 14
23:14:51.085 -> DD 04 00 08 0D C7 0D 2A 0D 55 0D 21 FE 5D 77

*/
