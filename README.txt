Open Sauce 2025 Badge: Persistence of Vision firmware v2
   
	************** 3 WARNINGS *****************
	1) YOU CAN ONLY PROGRAM YOUR ATTINY85 CHIP ONCE (the firmware, not custom messages)
	Otherwise you will need a High Voltage programmer to reset the hfuse, something like the 'HV Rescue Shield 2' by MightyOhm.

	2) THE PHOTO DIODE SILK SCREEN IS WRONG ON SOME OF THE BADGES
	The correct orientation on all badges is: LONG LEG THROUGH ROUND HOLE

	3) DO NOT SOLDER YOUR RESISTOR TO THE SLOT (only the small hole)
	****************************************

This software improves upon the original firmware distributed at Open Sauce 2025.
	- Improved optical data transfer for custom messages
	- Dynamic LED timing
	- Automatic mode switching for single button interface (display/programming/testing)

___________ USE INSTRUCTIONS _______________

- Display Message -
	1) Hold the button down and shake left/right to display the message.

	* Solder extra weight to the top of your resistor near the slot to make shaking easier. 

- Upload Custom Message -
	1) Draw a custom message at https://opensauce.com/badge-25/
	2) Hold the button down and point the photodiode into the black box on your screen.
	3) Keep the button down and press 'PROGRAM' on your screen.
	4) Keep holding the button down until programming is 100% and 3rd LED flashes quickly
	 
	* If 3rd LED doesn't flash quickly after programming reaches 100%, it failed. 
	* Disable dark mode
	* Turn screen brightness up
	* Try a different device
	* Go indoors
	* If you have the old firmware, enable the 'seperate flashing and programming'
		 press 'ENABLE', wait for 2nd LED to turn on then press 'PROGRAM'. 

- Error -
	1) If the top two LED's are flashing your resistor needs to be bent away from the metal contact.

	* DO NOT SOLDER YOUR RESISTOR TO THE SLOT	
	* Try to make the resistor just barely not touch the metal side of the slot

- Reset Message -
	1) Press the button quickly 5 times and hold to reset to the message to default 'SAUCE'


______________ FIRMWARE FLASHING INSTRUCTIONS ______________

This firmware was written for the attiny85

You will need a programmer like the Tiny AVR Programmer (USBTinyISP)

Compiled firmware in '../os_2025/Debug'

	************** WARNING *****************
	YOU CAN ONLY PROGRAM THE ATTINY85 ONCE
	You are going to disable the reset pin with 'hfuse', to use as normal I/O, but this disables re-programming. 
	A device like the 'HV Rescue Shield 2' by MightyOhm can reset attiny85 fuses
	****************************************

1) Insert your attiny85 into the programmer and plug it into your computer. 

2) Install AVRDUDE and run this command:
	avrdude -c usbtiny -p attiny85 -U flash:w:os_2025.srec -U eeprom:w:os_2025.eep -U lfuse:w:0x62:m -U efuse:w:0xFF:m -U hfuse:w:0x5f:m
