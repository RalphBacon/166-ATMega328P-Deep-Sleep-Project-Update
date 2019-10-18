/*
 * Simple Light Activated Alarm. Alarm sounds if light is present for > X seconds.
 *
 * Uses Deep Sleep (WDT controlled).
 */
#include "Arduino.h"
#include <avr/sleep.h>
#include <avr/wdt.h>
#include <avr/power.h>
#include <BatteryVoltageReader.h>

#define pwrLDR 7
#define ldrIN A0
#define beepPin 5
#define lightOnDelay 7 // n cycles of 8 seconds as millis don't work whilst asleep
#define lightLevelMin 100

#define disable_bod sleep_bod_disable();

// Serial output inhibited if line below is commented out
//#define DEBUG true

// State of light
int lightLevel;

// Keep track of previous A2D converter state
volatile byte prevADCSRA = ADCSRA;

// Advance declarations
void alienBeep();
void goToSleep();
uint16_t readLDR();
void lowBattery();

// Declare our battery object here
BatteryVoltageReader BVR;

//==================================
//SETUP    SETUP     SETUP     SETUP
//==================================
void setup()
{
#ifdef DEBUG
	Serial.begin(9600);
#endif
	// Set all [unused?] pin to INPUT mode to reduce power
	for (auto cnt = 0; cnt < 20; cnt++) {
		pinMode(cnt, INPUT);
	}

	// Disable unused analog inputs, saves power
	bitSet(DIDR0, ADC1D);// disable digital buffer on A1
	bitSet(DIDR0, ADC2D);  // disable digital buffer on A2
	bitSet(DIDR0, ADC3D);  // disable digital buffer on A3
	bitSet(DIDR0, ADC4D);  // disable digital buffer on A4
	bitSet(DIDR0, ADC5D);  // disable digital buffer on A5

	// Power for the LDR / VR
	pinMode(pwrLDR, OUTPUT);
	digitalWrite(pwrLDR, LOW);

	// Just to test
	readLDR();

	// 1.1V internal reference voltage. Measure it on pin 21.
	BVR.begin();

	// All done
	alienBeep();

#ifdef DEBUG
	Serial.println("Setup completed");
#endif
}

//==============================
// LOOP    LOOP    LOOP     LOOP
//==============================
void loop()
{
	static uint16_t lightOn = 0;

	// Count how many times we go to sleep (ie darkness reigns)
	unsigned long sleepCount = 0;

	// We stay in this loop whilst the analog input indicates darkness
	do {
		sleepCount++;
		goToSleep();

		// Read the LDR
		lightLevel = readLDR();
	}
	// Escape from sleep mode if light is on
	while (lightLevel < lightLevelMin);

#ifdef DEBUG
	// The light is ON!
	Serial.println("Light is ON!");
	Serial.print("Sleep Count: ");
	Serial.println(sleepCount);
#endif

	// Snapshop start of light going on time, if we didn't stay in sleep loop
	if (sleepCount == 1) {
		lightOn++;
	} else {
		lightOn = 1;
	}

	// Has it been light for longer than X seconds?
	Serial.print("LightOn: ");
	Serial.println(lightOn);
	if (lightOn > lightOnDelay) {
#ifdef DEBUG
		Serial.println("Alert!");
#endif
		alienBeep();

		// Reset alert timer
		lightOn = 1;
	}

	// If the battery is below 3.29v (329) beep a longer series of beeps
	uint16_t batVolts = BVR.readVCC();
#ifdef DEBUG
	Serial.print("Bat: ");
	Serial.println(batVolts);
#endif

	if (batVolts < 329) {
#ifdef DEBUG
		Serial.println("Battery low");
#endif
		lowBattery();
	}

	// So we can debug without a brain meltdown
	delay(250);
}

// Sleep routine, awoken by timer
void goToSleep() {

	// 1. Turn off all current output pins
	pinMode(pwrLDR, INPUT);

	// 2. Disable the ADC (Analog to digital converter, pins A0 [14] to A5 [19])
	prevADCSRA = ADCSRA;
	ADCSRA = 0;

	// 3. Power Reduction Register settings MUST be made after ADCSRA set to 0
	// and only apply in active (non-sleep) and idle modes
	//power_all_disable();

	// 4. Clear various "reset" flags
	MCUSR = 0; 	// allow changes, disable reset
	WDTCSR = bit (WDCE) | bit(WDE); // set Watchdog Config Mode so we can do the next line
	WDTCSR = bit (WDIE) | bit(WDP3) | bit(WDP0); // set WDIE, and 8 second delay

	// 4a. Reset the WDT now (just in case)
	wdt_reset();

	// 5. Set Sleep mode
	set_sleep_mode(SLEEP_MODE_PWR_DOWN);

	// 6. Ensure we can wake up again by first disabling interrupts (temporarily) so
	// the wakeISR does not run before we are asleep and then prevent interrupts,
	// and then defining the ISR (Interrupt Service Routine) to run when poked awake by the timer
	noInterrupts();

	// 7. Enable sleep mode (or we won't sleep)
	//sleep_enable();
	_SLEEP_CONTROL_REG |= (uint8_t) _SLEEP_ENABLE_MASK;

	// 8. Turn off Brown Out Detection (low voltage). This is automatically re-enabled
	// upon timer interrupt. Better to do this with fuses rather than in code.
	//sleep_bod_disable();
	disable_bod

// Send a message just to show we are about to sleep
#ifdef DEBUG
			Serial.println("Good night!");
			Serial.flush();
#endif

// 9. Allow interrupts now
	interrupts();

// 10. And enter sleep mode as set above. Whilst asleep we are not resetting the
// Watch Dog Timer so it will try and reset the board after 8 seconds
	//sleep_cpu();
	__asm__ __volatile__ ( "sleep" "\n\t" :: );

// ---------------------------------------------------------------
// µController is now asleep until woken up by the WDT (8 seconds)
// ---------------------------------------------------------------

// 1. AWAKE! Disable sleep mode now (belt and braces)
	//sleep_disable();
	_SLEEP_CONTROL_REG &= (uint8_t) (~_SLEEP_ENABLE_MASK);

#ifdef DEBUG
	Serial.println("WDT Awoken!");
#endif

// 2. Re-enable ADC if it was previously running, we need to read LDR
	ADCSRA = prevADCSRA;

// 3. Turn on all output pins, previously set to INPUT to save power
	pinMode(pwrLDR, OUTPUT);
	digitalWrite(pwrLDR, LOW);

// 4. Turn on everything else eg UART only relevant in IDLE or non-sleep states
//power_all_enable();
}

// When WatchDog timer causes µC to wake it comes here after X seconds
ISR (WDT_vect) {

// Turn off watchdog, we don't want it to do anything (like resetting this sketch)
	wdt_disable();

// Now we continue running the main Loop() just after we went to sleep
#ifdef DEBUG
	Serial.println("ISR");
#endif
}

// Close encounters of the thrid kind
void alienBeep() {
	static uint16_t tones[] = { 2349, 2637, 2093, 1046, 1567 };
	static uint16_t delays[] { 550, 550, 550, 575, 1200 };

// debugging beep intead of long tune
//	tone(beepPin, 2500, 50);
//	delay(50);
//	return;

	for (auto cnt = 0; cnt < 5; cnt++) {
		tone(beepPin, tones[cnt], 1000);
		delay(delays[cnt]);
	}
	delay(250);
}

// Read the light level
uint16_t readLDR()
{
// Read the LDR
	digitalWrite(pwrLDR, HIGH);
	lightLevel = analogRead(ldrIN);

#ifdef DEBUG
	Serial.print("Light level: ");
	Serial.println(lightLevel);
#endif

	digitalWrite(pwrLDR, LOW);
	return lightLevel;
}

// Low battery warning
void lowBattery() {
	for (auto cnt = 1; cnt < 10; cnt++) {
		tone(beepPin, 1800 + (100 * cnt), 500);
		delay(150);
	}
	delay(250);
}
