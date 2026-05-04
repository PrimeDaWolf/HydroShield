let port;
let reader;
let serialConnected = false;
let serialBuffer = '';

const STATUS = {
  connected: { text: 'Connected', color: '#27ae60' },
  disconnected: { text: 'Disconnected', color: '#e74c3c' }
};

const SERIAL_UI = {
  panel: 'position: fixed; top: 10px; right: 10px; z-index: 1000;',
  baseBtn: 'padding: 10px 15px; color: white; border: none; border-radius: 5px; cursor: pointer; font-size: 14px; font-weight: bold;',
  connectBtn: 'background-color: #4a90e2;',
  disconnectBtn: 'background-color: #e74c3c; margin-left: 5px; display: none;',
  status: 'margin-left: 10px; font-size: 12px; color: #e74c3c;'
};

function createElement(tag, text, style) {
  const el = document.createElement(tag);
  if (text !== undefined) el.textContent = text;
  if (style) el.style.cssText = style;
  return el;
}

function setConnectionStatus(connectBtn, disconnectBtn, statusSpan, connected) {
  connectBtn.style.display = connected ? 'none' : 'inline-block';
  disconnectBtn.style.display = connected ? 'inline-block' : 'none';
  statusSpan.textContent = connected ? STATUS.connected.text : STATUS.disconnected.text;
  statusSpan.style.color = connected ? STATUS.connected.color : STATUS.disconnected.color;
}

// Connect to ESP32 via USB
async function connectSerial() {
  try {
    port = await navigator.serial.requestPort();
    await port.open({ baudRate: 115200 });
    serialConnected = true;
    console.log('Connected to ESP32');
    startReadingSerial();
  } catch (error) {
    console.error('Failed to connect to serial port:', error);
    serialConnected = false;
  }
}

// Read data from serial port
async function startReadingSerial() {
  const textDecoder = new TextDecoderStream();
  port.readable.pipeTo(textDecoder.writable).catch(() => {});
  reader = textDecoder.readable.getReader();

  while (serialConnected) {
    try {
      const { value, done } = await reader.read();
      if (done) break;

      processSerialChunk(value || '');
    } catch (error) {
      console.error('Error reading serial:', error);
      serialConnected = false;
    }
  }
}

function processSerialChunk(chunk) {
  serialBuffer += chunk;
  const lines = serialBuffer.split('\n');
  serialBuffer = lines.pop() || '';

  lines.map((line) => line.trim()).filter(Boolean).forEach(parseESP32Data);
}

// Parse JSON data from ESP32
function parseESP32Data(data) {
  try {
    const sensorData = JSON.parse(data);
    updateChartsFromSerial(sensorData);
  } catch (error) {
    console.warn('Could not parse sensor data:', data);
  }
}

// Update charts with serial data
function updateChartsFromSerial(data) {
  const timestamp = new Date().toLocaleTimeString();
  const addIfDefined = (key, value) => value !== undefined && addDataPoint(key, value, timestamp);

  if (typeof resolveLevelStatuses === 'function') {
    resolveLevelStatuses(data);
  }
  addIfDefined('level', data.level);

  if (typeof resolveTemperatures === 'function' && Array.isArray(thermometerKeys)) {
    const temperatures = resolveTemperatures(data);
    thermometerKeys.forEach((key) => {
      const value = temperatures[key];
      if (value !== undefined && !Number.isNaN(Number(value))) {
        addDataPoint(key, Number(value), timestamp);
      }
    });
  }

  if (data.airPump !== undefined) updateDoughnutChart('airPump', data.airPump, 100 - data.airPump);
  addIfDefined('flowSensor', data.flowSensor);

  // New field names with compatibility fallback.
  const pump1Value = data.smallPump1 !== undefined ? data.smallPump1 : data.waterPump1;
  const pump2Value = data.smallPump2 !== undefined ? data.smallPump2 : data.waterPump2;

  addIfDefined('waterPump1', pump1Value);
  addIfDefined('waterPump2', pump2Value);
}

// Disconnect from serial port
async function disconnectSerial() {
  try {
    if (reader) await reader.cancel();
  } catch (_) {
    // Ignore cancellation errors during disconnect.
  }
  try {
    if (port) await port.close();
  } catch (_) {
    // Ignore close errors when device disconnects unexpectedly.
  }
  reader = null;
  port = null;
  serialBuffer = '';
  serialConnected = false;
  console.log('Disconnected from ESP32');
}

/* Add button to page for Web Serial connection
document.addEventListener('DOMContentLoaded', () => {
  const btnContainer = createElement('div', undefined, SERIAL_UI.panel);
  const connectBtn = createElement('button', '🔌 Connect ESP32', `${SERIAL_UI.baseBtn} ${SERIAL_UI.connectBtn}`);
  const disconnectBtn = createElement('button', '❌ Disconnect', `${SERIAL_UI.baseBtn} ${SERIAL_UI.disconnectBtn}`);
  const statusSpan = createElement('span', STATUS.disconnected.text, SERIAL_UI.status);

  connectBtn.onclick = async () => {
    if (serialConnected) return;
    await connectSerial();
    setConnectionStatus(connectBtn, disconnectBtn, statusSpan, serialConnected);
  };

  disconnectBtn.onclick = async () => {
    await disconnectSerial();
    setConnectionStatus(connectBtn, disconnectBtn, statusSpan, false);
  };

  btnContainer.appendChild(connectBtn);
  btnContainer.appendChild(disconnectBtn);
  btnContainer.appendChild(statusSpan);
  document.body.appendChild(btnContainer);
});
*/
