#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <OneWire.h>
#include <DallasTemperature.h>

// ================================
// Wi-Fi configuration
// Replace with your network values
// ================================
const char* WIFI_SSID = "Gp16";
const char* WIFI_PASSWORD = "JAFGGP16";

WebServer server(80);

// Example GPIO assignments (adjust to your hardware)
constexpr uint8_t LEVEL_SENSOR_COUNT = 3;
constexpr int LEVEL_SENSOR_PINS[LEVEL_SENSOR_COUNT] = {34, 35, 36};
constexpr int LEVEL_SENSOR_THRESHOLD = 2000; // ADC threshold for Odseven water sensor modules
constexpr int TEMP_ONEWIRE_PIN = 4;
constexpr int FLOW_SENSOR_PIN = 32;
constexpr float FLOW_PULSES_PER_SECOND_PER_L_MIN = 98.0f; // YF-S401 nominal factor

// 4-channel relay module pins
constexpr int RELAY_MAIN_PUMP_PIN = 25;      // Relay 1
constexpr int RELAY_AIR_PUMP_PIN = 26;       // Relay 2
constexpr int RELAY_SMALL_PUMP_1_PIN = 27;   // Relay 3
constexpr int RELAY_SMALL_PUMP_2_PIN = 14;   // Relay 4

// Physical toggle buttons (connect between pin and GND)
constexpr int BUTTON_MAIN_PUMP_PIN    = 13;  // Button 1 → Main Pump
constexpr int BUTTON_AIR_PUMP_PIN     = 16;  // Button 2 → Air Pump
constexpr int BUTTON_SMALL_PUMP_1_PIN = 17;  // Button 3 → Water Pump 1
constexpr int BUTTON_SMALL_PUMP_2_PIN = 18;  // Button 4 → Water Pump 2
constexpr uint8_t BUTTON_COUNT = 4;
constexpr unsigned long BUTTON_DEBOUNCE_MS = 50;

// Emergency Stop button (connect between pin and GND)
// Use a non-strapping pin to avoid boot issues.
constexpr int ESTOP_BUTTON_PIN = 33;
constexpr int ESTOP_LED_PIN = 23; // LED indicator for E-Stop status
constexpr bool ESTOP_LED_ACTIVE_HIGH = true;
bool eStopActive = false;
bool eStopLastButtonState = HIGH;
bool eStopStableButtonState = HIGH;
unsigned long eStopLastDebounce = 0;
constexpr bool RELAY_ACTIVE_LOW = true;
constexpr uint8_t RELAY_COUNT = 4;

constexpr uint8_t RELAY_MAIN_PUMP_INDEX = 0;
constexpr uint8_t RELAY_AIR_PUMP_INDEX = 1;
constexpr uint8_t RELAY_SMALL_PUMP_1_INDEX = 2;
constexpr uint8_t RELAY_SMALL_PUMP_2_INDEX = 3;
constexpr const char* RELAY_NAMES[RELAY_COUNT] = {
	"mainPump",
	"airPump",
	"smallPump1",
	"smallPump2"
};

int relayPins[RELAY_COUNT] = {
	RELAY_MAIN_PUMP_PIN,
	RELAY_AIR_PUMP_PIN,
	RELAY_SMALL_PUMP_1_PIN,
	RELAY_SMALL_PUMP_2_PIN
};

bool relayStates[RELAY_COUNT] = {false, false, false, false};

int buttonPins[BUTTON_COUNT] = {
	BUTTON_MAIN_PUMP_PIN,
	BUTTON_AIR_PUMP_PIN,
	BUTTON_SMALL_PUMP_1_PIN,
	BUTTON_SMALL_PUMP_2_PIN
};
bool buttonLastState[BUTTON_COUNT]     = {HIGH, HIGH, HIGH, HIGH};
bool buttonStableState[BUTTON_COUNT]   = {HIGH, HIGH, HIGH, HIGH};
unsigned long buttonLastDebounce[BUTTON_COUNT] = {0, 0, 0, 0};

OneWire oneWire(TEMP_ONEWIRE_PIN);
DallasTemperature tempBus(&oneWire);
constexpr uint8_t MAX_THERMOMETERS = 3;
DeviceAddress thermometerAddresses[MAX_THERMOMETERS];
uint8_t thermometerCount = 0;
unsigned long lastThermometerDiscovery = 0;
constexpr unsigned long THERMOMETER_REDISCOVERY_INTERVAL_MS = 15000;

unsigned long lastSensorUpdate = 0;
volatile uint32_t flowPulseCount = 0;
float level = 0.0f;
bool levelFull[LEVEL_SENSOR_COUNT] = {false, false, false};
constexpr unsigned long AUTO_PUMP_RUN_MS = 15000; // Return pump runtime when small tank is full
enum AutoPumpState : uint8_t { AUTO_PUMP_IDLE = 0, AUTO_PUMP_RUNNING = 1, AUTO_PUMP_WAITING_FOR_CLEAR = 2 };
AutoPumpState autoPump1State = AUTO_PUMP_IDLE;
AutoPumpState autoPump2State = AUTO_PUMP_IDLE;
unsigned long autoPump1StopMs = 0;
unsigned long autoPump2StopMs = 0;
bool mainPumpBlocked = false;
const char* levelActionText[LEVEL_SENSOR_COUNT] = {"Ready", "Ready", "Ready"};
float temperature = 24.0f;
float temperature1 = 24.0f;
float temperature2 = 24.0f;
float temperature3 = 24.0f;
float mainPump = 0.0f;
float airPump = 0.0f;
float flowSensor = 0.0f;
float smallPump1 = 0.0f;
float smallPump2 = 0.0f;
float waterPump1 = 0.0f;
float waterPump2 = 0.0f;

const char* jsonBool(bool value) {
	return value ? "true" : "false";
}

float temperatureByIndex(uint8_t index) {
	switch (index) {
		case 0: return temperature1;
		case 1: return temperature2;
		case 2: return temperature3;
		default: return NAN;
	}
}

void IRAM_ATTR onFlowPulse() {
	flowPulseCount++;
}

int relayOutputLevel(bool on) {
	if (RELAY_ACTIVE_LOW) {
		return on ? LOW : HIGH;
	}
	return on ? HIGH : LOW;
}

void setRelayState(uint8_t relayIndex, bool on) {
	if (relayIndex >= RELAY_COUNT) {
		return;
	}
	// Block turning ON any relay while E-Stop is active.
	if (eStopActive && on) {
		return;
	}
	relayStates[relayIndex] = on;
	digitalWrite(relayPins[relayIndex], relayOutputLevel(on));
}

// Forward declaration — defined after relay telemetry helpers.
void updatePumpTelemetryFromRelays();

void updateEStopLed() {
	const int level = ESTOP_LED_ACTIVE_HIGH
		? (eStopActive ? HIGH : LOW)
		: (eStopActive ? LOW : HIGH);
	digitalWrite(ESTOP_LED_PIN, level);
}

void activateEStop() {
	eStopActive = true;
	for (uint8_t i = 0; i < RELAY_COUNT; i++) {
		setRelayState(i, false);
	}
	updatePumpTelemetryFromRelays();
	updateEStopLed();
	Serial.println("E-STOP ACTIVATED — all relays OFF");
}

void clearEStop() {
	eStopActive = false;
	updateEStopLed();
	Serial.println("E-Stop cleared");
}

bool parseOnOffArg(const String& value, bool& parsedState) {
	if (value == "1" || value.equalsIgnoreCase("on") || value.equalsIgnoreCase("true")) {
		parsedState = true;
		return true;
	}
	if (value == "0" || value.equalsIgnoreCase("off") || value.equalsIgnoreCase("false")) {
		parsedState = false;
		return true;
	}
	return false;
}

bool applyRelayArgIfPresent(const char* argName, uint8_t relayIndex, const char* errorLabel, bool& changed) {
	if (!server.hasArg(argName)) {
		return true;
	}

	bool state = false;
	if (!parseOnOffArg(server.arg(argName), state)) {
		String payload = "{\"error\":\"Invalid ";
		payload += errorLabel;
		payload += " state\"}";
		server.send(400, "application/json", payload);
		return false;
	}

	setRelayState(relayIndex, state);
	changed = true;
	return true;
}

void updateAutoPumpState(bool isFull, uint8_t relayIndex, AutoPumpState& state, unsigned long& stopMs) {
	if (state == AUTO_PUMP_RUNNING && millis() >= stopMs) {
		setRelayState(relayIndex, false);
		state = AUTO_PUMP_WAITING_FOR_CLEAR;
	}

	if (state == AUTO_PUMP_WAITING_FOR_CLEAR && !isFull) {
		state = AUTO_PUMP_IDLE;
	}

	if (isFull && state == AUTO_PUMP_IDLE && !relayStates[relayIndex]) {
		setRelayState(relayIndex, true);
		state = AUTO_PUMP_RUNNING;
		stopMs = millis() + AUTO_PUMP_RUN_MS;
	}
}

void updatePumpSafetyLogic() {
	const bool small1Full = levelFull[1];
	const bool small2Full = levelFull[2];
	mainPumpBlocked = small1Full || small2Full;

	if (mainPumpBlocked) {
		setRelayState(RELAY_MAIN_PUMP_INDEX, false);
	}

	updateAutoPumpState(small1Full, RELAY_SMALL_PUMP_1_INDEX, autoPump1State, autoPump1StopMs);
	updateAutoPumpState(small2Full, RELAY_SMALL_PUMP_2_INDEX, autoPump2State, autoPump2StopMs);

	if (mainPumpBlocked) {
		levelActionText[0] = "Main pump blocked";
	} else {
		levelActionText[0] = "Main tank OK";
	}

	if (small1Full) {
		levelActionText[1] = (autoPump1State == AUTO_PUMP_RUNNING) ? "Returning to main" : "Full";
	} else if (autoPump1State == AUTO_PUMP_WAITING_FOR_CLEAR) {
		levelActionText[1] = "Waiting to reset";
	} else {
		levelActionText[1] = "Ready";
	}

	if (small2Full) {
		levelActionText[2] = (autoPump2State == AUTO_PUMP_RUNNING) ? "Returning to main" : "Full";
	} else if (autoPump2State == AUTO_PUMP_WAITING_FOR_CLEAR) {
		levelActionText[2] = "Waiting to reset";
	} else {
		levelActionText[2] = "Ready";
	}
}

void updatePumpTelemetryFromRelays() {
	mainPump = relayStates[RELAY_MAIN_PUMP_INDEX] ? 1800.0f : 0.0f;
	airPump = relayStates[RELAY_AIR_PUMP_INDEX] ? 100.0f : 0.0f;
	smallPump1 = relayStates[RELAY_SMALL_PUMP_1_INDEX] ? 1800.0f : 0.0f;
	smallPump2 = relayStates[RELAY_SMALL_PUMP_2_INDEX] ? 1800.0f : 0.0f;

	// Backward compatibility for existing dashboard charts.
	waterPump1 = smallPump1;
	waterPump2 = smallPump2;
}

String deviceAddressToString(const DeviceAddress addr) {
	char hexAddress[17];
	for (uint8_t i = 0; i < 8; i++) {
		sprintf(&hexAddress[i * 2], "%02X", addr[i]);
	}
	return String(hexAddress);
}

String jsonNumberOrNull(float value, uint8_t precision) {
	if (isnan(value) || isinf(value)) {
		return "null";
	}
	return String(value, static_cast<unsigned int>(precision));
}

void discoverThermometers() {
	thermometerCount = 0;
	tempBus.begin();
	lastThermometerDiscovery = millis();

	DeviceAddress addr;
	oneWire.reset_search();

	while (oneWire.search(addr) && thermometerCount < MAX_THERMOMETERS) {
		if (tempBus.validAddress(addr)) {
			memcpy(thermometerAddresses[thermometerCount], addr, sizeof(DeviceAddress));
			thermometerCount++;
		}
	}

	Serial.print("Thermometers found on one-wire bus: ");
	Serial.println(thermometerCount);
	for (uint8_t i = 0; i < thermometerCount; i++) {
		Serial.print("  Sensor ");
		Serial.print(i + 1);
		Serial.print(" ID: ");
		Serial.println(deviceAddressToString(thermometerAddresses[i]));
	}
}

void connectToWiFi() {
	WiFi.mode(WIFI_STA);
	WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

	Serial.print("Connecting to Wi-Fi");
	int attempts = 0;
	while (WiFi.status() != WL_CONNECTED && attempts < 40) {
		delay(500);
		Serial.print('.');
		attempts++;
	}
	Serial.println();

	if (WiFi.status() == WL_CONNECTED) {
		Serial.println("Wi-Fi connected");
		Serial.print("IP address: ");
		Serial.println(WiFi.localIP());
	} else {
		Serial.println("Wi-Fi connection failed. Rebooting in 5 seconds...");
		delay(5000);
		ESP.restart();
	}
}

void updateSensorValues() {
	// Update values every 2 seconds to match dashboard polling cadence.
	const unsigned long now = millis();
	if (now - lastSensorUpdate < 2000) {
		return;
	}

	const unsigned long elapsedMs = now - lastSensorUpdate;
	lastSensorUpdate = now;

	int fullCount = 0;
	for (uint8_t i = 0; i < LEVEL_SENSOR_COUNT; i++) {
		const int rawValue = analogRead(LEVEL_SENSOR_PINS[i]);
		levelFull[i] = (rawValue >= LEVEL_SENSOR_THRESHOLD);
		if (levelFull[i]) {
			fullCount++;
		}
	}
	level = (LEVEL_SENSOR_COUNT > 0) ? ((fullCount / static_cast<float>(LEVEL_SENSOR_COUNT)) * 100.0f) : 0.0f;

	updatePumpSafetyLogic();

	uint32_t pulses = 0;
	noInterrupts();
	pulses = flowPulseCount;
	flowPulseCount = 0;
	interrupts();

	const float elapsedSeconds = elapsedMs > 0 ? (elapsedMs / 1000.0f) : 1.0f;
	const float pulseFrequency = pulses / elapsedSeconds;
	flowSensor = pulseFrequency / FLOW_PULSES_PER_SECOND_PER_L_MIN;

	// If no thermometer was found at boot, keep retrying discovery.
	if (thermometerCount == 0 && (millis() - lastThermometerDiscovery) >= THERMOMETER_REDISCOVERY_INTERVAL_MS) {
		discoverThermometers();
	}

	tempBus.requestTemperatures();
	float* temperatures[MAX_THERMOMETERS] = {&temperature1, &temperature2, &temperature3};
	for (uint8_t i = 0; i < MAX_THERMOMETERS; i++) {
		*temperatures[i] = (thermometerCount > i) ? tempBus.getTempC(thermometerAddresses[i]) : NAN;
	}

	float sum = 0.0f;
	int validReadings = 0;
	for (uint8_t i = 0; i < MAX_THERMOMETERS; i++) {
		const float value = *temperatures[i];
		if (!isnan(value) && value > -100.0f) {
			sum += value;
			validReadings++;
		}
	}
	temperature = validReadings > 0 ? (sum / validReadings) : NAN;

	updatePumpTelemetryFromRelays();

	Serial.printf(
		"{\"level\":%.1f,\"level1\":%s,\"level2\":%s,\"level3\":%s,\"levelActions\":{\"tank1\":\"%s\",\"tank2\":\"%s\",\"tank3\":\"%s\"},\"mainPumpBlocked\":%s,\"autoPump1Active\":%s,\"autoPump2Active\":%s,\"temperature\":%.1f,\"temperature1\":%.2f,\"temperature2\":%.2f,\"temperature3\":%.2f,\"mainPump\":%.0f,\"airPump\":%.0f,\"flowSensor\":%.2f,\"smallPump1\":%.0f,\"smallPump2\":%.0f,\"waterPump1\":%.0f,\"waterPump2\":%.0f}\n",
		level,
		jsonBool(levelFull[0]),
		jsonBool(levelFull[1]),
		jsonBool(levelFull[2]),
		levelActionText[0],
		levelActionText[1],
		levelActionText[2],
		jsonBool(mainPumpBlocked),
		jsonBool(autoPump1State == AUTO_PUMP_RUNNING),
		jsonBool(autoPump2State == AUTO_PUMP_RUNNING),
		temperature,
		temperature1,
		temperature2,
		temperature3,
		mainPump,
		airPump,
		flowSensor,
		smallPump1,
		smallPump2,
		waterPump1,
		waterPump2
	);
}

void handleRoot() {
	server.send(200, "text/plain", "HydroShield ESP32 API running");
}

void handleSensors() {
	String payload = "{";
	payload += "\"level\":" + jsonNumberOrNull(level, 1) + ",";
	payload += "\"level1\":" + String(jsonBool(levelFull[0])) + ",";
	payload += "\"level2\":" + String(jsonBool(levelFull[1])) + ",";
	payload += "\"level3\":" + String(jsonBool(levelFull[2])) + ",";
	payload += "\"levelSensors\":{";
	payload += "\"tank1\":" + String(jsonBool(levelFull[0])) + ",";
	payload += "\"tank2\":" + String(jsonBool(levelFull[1])) + ",";
	payload += "\"tank3\":" + String(jsonBool(levelFull[2]));
	payload += "},";
	payload += "\"temperature\":" + jsonNumberOrNull(temperature, 1) + ",";
	payload += "\"temperature1\":" + jsonNumberOrNull(temperature1, 2) + ",";
	payload += "\"temperature2\":" + jsonNumberOrNull(temperature2, 2) + ",";
	payload += "\"temperature3\":" + jsonNumberOrNull(temperature3, 2) + ",";
	payload += "\"temperatures\":{";
	for (uint8_t i = 0; i < thermometerCount; i++) {
		if (i > 0) payload += ",";
		payload += "\"" + deviceAddressToString(thermometerAddresses[i]) + "\":" + jsonNumberOrNull(temperatureByIndex(i), 2);
	}
	payload += "},";
	payload += "\"temperatureSensorCount\":" + String(thermometerCount) + ",";
	payload += "\"mainPump\":" + jsonNumberOrNull(mainPump, 0) + ",";
	payload += "\"airPump\":" + jsonNumberOrNull(airPump, 0) + ",";
	payload += "\"flowSensor\":" + jsonNumberOrNull(flowSensor, 2) + ",";
	payload += "\"smallPump1\":" + jsonNumberOrNull(smallPump1, 0) + ",";
	payload += "\"smallPump2\":" + jsonNumberOrNull(smallPump2, 0) + ",";
	payload += "\"waterPump1\":" + jsonNumberOrNull(waterPump1, 0) + ",";
	payload += "\"waterPump2\":" + jsonNumberOrNull(waterPump2, 0) + ",";
	payload += "\"levelActions\":{\"tank1\":\"" + String(levelActionText[0]) + "\",\"tank2\":\"" + String(levelActionText[1]) + "\",\"tank3\":\"" + String(levelActionText[2]) + "\"},";
	payload += "\"mainPumpBlocked\":" + String(jsonBool(mainPumpBlocked)) + ",";
	payload += "\"autoPump1Active\":" + String(jsonBool(autoPump1State == AUTO_PUMP_RUNNING)) + ",";
	payload += "\"autoPump2Active\":" + String(jsonBool(autoPump2State == AUTO_PUMP_RUNNING));
	payload += "}";

	server.send(200, "application/json", payload);
}

void handleStatus() {
	String payload = "{";
	payload += "\"wifiConnected\":" + String(WiFi.status() == WL_CONNECTED ? "true" : "false") + ",";
	payload += "\"ip\":\"" + WiFi.localIP().toString() + "\",";
	payload += "\"rssi\":" + String(WiFi.RSSI()) + ",";
	payload += "\"uptimeMs\":" + String(millis()) + ",";
	payload += "\"relayActiveLow\":" + String(RELAY_ACTIVE_LOW ? "true" : "false");
	payload += "}";

	server.send(200, "application/json", payload);
}

void handleDevices() {
	String payload = "{";

	// Thermometers on the OneWire bus.
	payload += "\"thermometers\":[";
	for (uint8_t i = 0; i < thermometerCount; i++) {
		if (i > 0) payload += ",";
		float tempVal = temperatureByIndex(i);
		payload += "{";
		payload += "\"index\":" + String(i) + ",";
		payload += "\"id\":\"" + deviceAddressToString(thermometerAddresses[i]) + "\",";
		payload += "\"pin\":" + String(TEMP_ONEWIRE_PIN) + ",";
		payload += "\"lastTempC\":" + (isnan(tempVal) ? String("null") : String(tempVal, 2));
		payload += "}";
	}
	payload += "],";
	payload += "\"thermometerCount\":" + String(thermometerCount) + ",";

	// Relay channels and their current states.
	payload += "\"relays\":[";
	for (uint8_t i = 0; i < RELAY_COUNT; i++) {
		if (i > 0) payload += ",";
		payload += "{\"channel\":" + String(i + 1) + ",\"name\":\"" + RELAY_NAMES[i] + "\",\"pin\":" + String(relayPins[i]) + ",\"state\":" + String(jsonBool(relayStates[i])) + "}";
	}
	payload += "],";

	// Analog / digital sensors.
	payload += "\"sensors\":{";
	payload += "\"levelSensors\":{\"tank1\":" + String(jsonBool(levelFull[0])) + ",\"tank2\":" + String(jsonBool(levelFull[1])) + ",\"tank3\":" + String(jsonBool(levelFull[2])) + "},";
	payload += "\"flowSensor\":{\"pin\":" + String(FLOW_SENSOR_PIN) + ",\"type\":\"pulse\",\"lastValue\":" + String(flowSensor, 2) + ",\"factorHzPerLMin\":" + String(FLOW_PULSES_PER_SECOND_PER_L_MIN, 1) + "},";
	payload += "\"oneWireBus\":{\"pin\":" + String(TEMP_ONEWIRE_PIN) + ",\"type\":\"oneWire\",\"deviceCount\":" + String(thermometerCount) + "}";
	payload += "}";

	payload += "}";
	server.send(200, "application/json", payload);
}

void handleGetRelays() {
	String payload = "{";
	payload += "\"relayActiveLow\":" + String(jsonBool(RELAY_ACTIVE_LOW)) + ",";
	payload += "\"channels\":{";
	payload += "\"mainPump\":" + String(jsonBool(relayStates[RELAY_MAIN_PUMP_INDEX])) + ",";
	payload += "\"airPump\":" + String(jsonBool(relayStates[RELAY_AIR_PUMP_INDEX])) + ",";
	payload += "\"smallPump1\":" + String(jsonBool(relayStates[RELAY_SMALL_PUMP_1_INDEX])) + ",";
	payload += "\"smallPump2\":" + String(jsonBool(relayStates[RELAY_SMALL_PUMP_2_INDEX])) + ",";
	// Legacy names for compatibility.
	payload += "\"waterPump1\":" + String(jsonBool(relayStates[RELAY_SMALL_PUMP_1_INDEX])) + ",";
	payload += "\"waterPump2\":" + String(jsonBool(relayStates[RELAY_SMALL_PUMP_2_INDEX]));
	payload += "}";
	payload += "}";

	server.send(200, "application/json", payload);
}

void handleSetRelay() {
	if (!server.hasArg("channel") || !server.hasArg("state")) {
		server.send(400, "application/json", "{\"error\":\"Use channel (1-4) and state (on/off or 1/0)\"}");
		return;
	}

	const int channel = server.arg("channel").toInt();
	if (channel < 1 || channel > RELAY_COUNT) {
		server.send(400, "application/json", "{\"error\":\"Invalid channel. Use 1-4\"}");
		return;
	}

	bool desiredState = false;
	if (!parseOnOffArg(server.arg("state"), desiredState)) {
		server.send(400, "application/json", "{\"error\":\"Invalid state. Use on/off or 1/0\"}");
		return;
	}

	setRelayState(channel - 1, desiredState);
	updatePumpTelemetryFromRelays();

	String payload = "{";
	payload += "\"ok\":true,";
	payload += "\"channel\":" + String(channel) + ",";
	payload += "\"state\":" + String(desiredState ? "true" : "false");
	payload += "}";
	server.send(200, "application/json", payload);
}

void handleSetPumps() {
	bool changed = false;
	struct PumpArgMap {
		const char* arg;
		uint8_t relayIndex;
		const char* errorLabel;
	};
	const PumpArgMap args[] = {
		{"airPump", RELAY_AIR_PUMP_INDEX, "airPump"},
		{"mainPump", RELAY_MAIN_PUMP_INDEX, "mainPump"},
		{"smallPump1", RELAY_SMALL_PUMP_1_INDEX, "smallPump1"},
		{"smallPump2", RELAY_SMALL_PUMP_2_INDEX, "smallPump2"},
		// Legacy arg names map to small pumps.
		{"waterPump1", RELAY_SMALL_PUMP_1_INDEX, "waterPump1"},
		{"waterPump2", RELAY_SMALL_PUMP_2_INDEX, "waterPump2"}
	};

	for (const auto& arg : args) {
		if (!applyRelayArgIfPresent(arg.arg, arg.relayIndex, arg.errorLabel, changed)) {
			return;
		}
	}

	if (!changed) {
		server.send(400, "application/json", "{\"error\":\"No valid pump/relay args provided\"}");
		return;
	}

	updatePumpTelemetryFromRelays();
	handleGetRelays();
}

void readButtons() {
	for (uint8_t i = 0; i < BUTTON_COUNT; i++) {
		const bool reading = digitalRead(buttonPins[i]);
		if (reading != buttonLastState[i]) {
			buttonLastDebounce[i] = millis();
			buttonLastState[i] = reading;
		}

		if ((millis() - buttonLastDebounce[i]) >= BUTTON_DEBOUNCE_MS) {
			if (reading != buttonStableState[i]) {
				buttonStableState[i] = reading;
				// Trigger once per press (falling edge).
				if (buttonStableState[i] == LOW) {
					setRelayState(i, !relayStates[i]);
					updatePumpTelemetryFromRelays();
				}
			}
		}
	}
}

void readEStopButton() {
	const bool reading = digitalRead(ESTOP_BUTTON_PIN);
	if (reading != eStopLastButtonState) {
		eStopLastDebounce = millis();
		eStopLastButtonState = reading;
	}

	if ((millis() - eStopLastDebounce) >= BUTTON_DEBOUNCE_MS) {
		if (reading != eStopStableButtonState) {
			eStopStableButtonState = reading;
			// Toggle only once per press.
			if (eStopStableButtonState == LOW) {
				if (eStopActive) {
					clearEStop();
				} else {
					activateEStop();
				}
			}
		}
	}
}

void handleEStop() {
	// GET → return current state.
	// POST/GET with ?active=true|false → set state.
	if (server.hasArg("active")) {
		bool desired = false;
		if (!parseOnOffArg(server.arg("active"), desired)) {
			server.send(400, "application/json", "{\"error\":\"Use active=true|false|on|off|1|0\"}");
			return;
		}
		if (desired) {
			activateEStop();
		} else {
			clearEStop();
		}
	}
	String payload = "{";
	payload += "\"eStopActive\":" + String(eStopActive ? "true" : "false");
	payload += "}";
	server.send(200, "application/json", payload);
}

void setup() {
	Serial.begin(115200);
	lastSensorUpdate = millis();

	for (uint8_t i = 0; i < RELAY_COUNT; i++) {
		pinMode(relayPins[i], OUTPUT);
		setRelayState(i, false);
	}
	updatePumpTelemetryFromRelays();
	pinMode(ESTOP_LED_PIN, OUTPUT);
	updateEStopLed();

	pinMode(FLOW_SENSOR_PIN, INPUT_PULLUP);
	attachInterrupt(digitalPinToInterrupt(FLOW_SENSOR_PIN), onFlowPulse, FALLING);

	for (uint8_t i = 0; i < LEVEL_SENSOR_COUNT; i++) {
		pinMode(LEVEL_SENSOR_PINS[i], INPUT);
		levelFull[i] = (analogRead(LEVEL_SENSOR_PINS[i]) >= LEVEL_SENSOR_THRESHOLD);
	}
	for (uint8_t i = 0; i < BUTTON_COUNT; i++) {
		pinMode(buttonPins[i], INPUT_PULLUP);
		const bool current = digitalRead(buttonPins[i]);
		buttonLastState[i] = current;
		buttonStableState[i] = current;
		buttonLastDebounce[i] = millis();
	}
	pinMode(ESTOP_BUTTON_PIN, INPUT_PULLUP);
	eStopLastButtonState = digitalRead(ESTOP_BUTTON_PIN);
	eStopStableButtonState = eStopLastButtonState;
	eStopLastDebounce = millis();

	discoverThermometers();

	connectToWiFi();

	// Allow dashboard requests from other origins (e.g., localhost or file://).
	server.enableCORS(true);

	server.on("/", HTTP_GET, handleRoot);
	server.on("/api/sensors", HTTP_GET, handleSensors);
	server.on("/api/status", HTTP_GET, handleStatus);
	server.on("/api/devices", HTTP_GET, handleDevices);
	server.on("/api/relays", HTTP_GET, handleGetRelays);
	server.on("/api/relay", HTTP_POST, handleSetRelay);
	server.on("/api/relay", HTTP_GET, handleSetRelay);
	server.on("/api/pumps", HTTP_POST, handleSetPumps);
	server.on("/api/pumps", HTTP_GET, handleSetPumps);
	server.on("/api/estop", HTTP_GET, handleEStop);
	server.on("/api/estop", HTTP_POST, handleEStop);
	server.begin();

	Serial.println("HTTP server started on port 80");
}

void loop() {
	readEStopButton();
	readButtons();
	updateSensorValues();
	server.handleClient();
}
