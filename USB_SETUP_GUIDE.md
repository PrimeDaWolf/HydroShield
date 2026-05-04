# ESP32 USB Serial Integration - Setup Guide

## Option 1: Web Serial API (Easiest - Browser Direct)

### Requirements:
- Chrome, Edge, or Opera browser (NOT Firefox/Safari yet)
- ESP32 connected via USB
- Browser must support Web Serial API

### How to use:
1. Include the `webserial.js` file in your HTML:
```html
<script src="../js/index.js"></script>
<script src="../js/webserial.js"></script>
```

2. Click "🔌 Connect ESP32" button in top-right corner of the page
3. Select your ESP32 COM port from the popup
4. Data will automatically update the charts

### ESP32 Arduino Code Example:
Your ESP32 must send JSON data over Serial (115200 baud):
```cpp
#include <ArduinoJson.h>

void sendSensorData() {
  StaticJsonDocument<200> doc;
  doc["level"] = analogRead(LEVEL_PIN) / 40.95; // 0-100%
  doc["temperature"] = readTemperature(); // °C
  doc["airPump"] = isPumpRunning() ? 100 : 0; // 0-100%
  doc["flowSensor"] = getFlowRate(); // L/min
  doc["waterPump1"] = getPump1Speed(); // RPM
  doc["waterPump2"] = getPump2Speed(); // RPM
  
  serializeJson(doc, Serial);
  Serial.println();
  delay(2000); // Send every 2 seconds
}

void setup() {
  Serial.begin(115200);
}

void loop() {
  sendSensorData();
}
```

### Odseven Water Sensor Module for Arduino
- Wire the module to the ESP32 as follows:
  - `+` → `3.3V` (do not use 5V when connecting directly to ESP32 ADC pins)
  - `-` → `GND`
  - `S` → `ADC pin 34`, `35`, or `36`
- The code uses analog readings from the Odseven module and applies a threshold to detect water level.
- If you need to tune sensitivity, adjust `LEVEL_SENSOR_THRESHOLD` in `src/HydroShield.cpp`.

---

## Option 2: Node.js Server (More Robust)

### Requirements:
- Node.js installed
- npm packages: `express`, `serialport`, `cors`

### Installation:
```bash
# Navigate to project directory
cd "c:\Users\choch\Documents\PlatformIO\Projects\260409-005028-arduino-blink"

# Install dependencies
npm install express serialport cors
```

### Run the server:
```bash
node serialServer.js
```

You should see:
```
✅ Serial port COM3 opened at 115200 baud
🚀 Server running at http://localhost:3000
```

### ESP32 Arduino Code (same as Option 1)

### Update index.js for Node.js server:
Replace the fetch URL in `index.js`:
```javascript
// Line ~118: Change from
const response = await fetch('http://192.168.1.100/api/sensors', {

// To:
const response = await fetch('http://localhost:3000/api/sensors', {
```

### Server Endpoints:
- `GET /api/sensors` - Get all sensor data
- `GET /api/sensors/temperature` - Get specific sensor
- `GET /api/status` - Connection status
- `GET /api/ports` - List available COM ports
- `POST /api/connect` - Connect to port
- `POST /api/disconnect` - Disconnect

---

## Which Option to Choose?

| Feature | Web Serial | Node.js |
|---------|-----------|---------|
| **Setup Complexity** | ⭐ Easiest | ⭐⭐⭐ More setup |
| **Browser Support** | Chrome/Edge only | Works everywhere |
| **Real-time** | ✅ Yes | ✅ Yes |
| **Testing** | ✅ Great | ✅ Better for production |
| **Debugging** | Console shows data | Server logs available |
| **Reliability** | Works well for local | Most stable |

**For initial testing:** Use **Web Serial API (Option 1)**
**For production:** Use **Node.js Server (Option 2)**

---

## Troubleshooting

### No COM ports appear in Web Serial?
- Make sure ESP32 is connected and recognized by Windows
- Check Device Manager (devmgmt.msc) for COM ports
- Update CH340 drivers if needed

### "Cannot find module 'serialport'"
- Run: `npm install serialport` again
- Make sure you're in the correct project directory

### Charts not updating?
- Open browser console (F12) and check for errors
- Verify ESP32 is sending valid JSON
- Check baud rate is 115200

### JSON parse errors?
- Ensure ESP32 sends: `{"level":75}\n` (with newline at end)
- Serial data format must be valid JSON
