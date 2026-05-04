// ============================================
// Node.js Server for Serial Communication
// Run: npm install express serialport cors
// Then: node serialServer.js
// Access: http://localhost:3000
// ============================================

const express = require('express');
const SerialPort = require('serialport').SerialPort;
const { ReadlineParser } = require('@serialport/parser-readline');
const cors = require('cors');
const path = require('path');

const app = express();
const PORT = 3000;

// Enable CORS and JSON parsing
app.use(cors());
app.use(express.json());
app.use(express.static(path.join(__dirname, '../src')));

// Current sensor data storage
let currentSensorData = {
  level: 0,
  temperature: 0,
  airPump: 0,
  flowSensor: 0,
  waterPump1: 0,
  waterPump2: 0
};

// Serial port configuration
const BAUD_RATE = 115200;
let serialPort;
let isConnected = false;

// Initialize serial connection
function initializeSerial(portName) {
  try {
    serialPort = new SerialPort({
      path: portName,
      baudRate: BAUD_RATE
    });

    const parser = serialPort.pipe(new ReadlineParser({ delimiter: '\n' }));

    serialPort.on('open', () => {
      console.log(`✅ Serial port ${portName} opened at ${BAUD_RATE} baud`);
      isConnected = true;
    });

    parser.on('data', (line) => {
      try {
        const data = JSON.parse(line);
        currentSensorData = { ...currentSensorData, ...data };
        console.log(`📊 Sensor Data:`, currentSensorData);
      } catch (error) {
        console.warn('⚠️  Could not parse sensor data:', line);
      }
    });

    serialPort.on('error', (error) => {
      console.error('❌ Serial port error:', error);
      isConnected = false;
    });

    serialPort.on('close', () => {
      console.log('Serial port closed');
      isConnected = false;
    });
  } catch (error) {
    console.error('❌ Failed to open serial port:', error);
    isConnected = false;
  }
}

// API endpoint to get all sensor data
app.get('/api/sensors', (req, res) => {
  res.json(currentSensorData);
});

// API endpoint to get individual sensor
app.get('/api/sensors/:sensor', (req, res) => {
  const sensorName = req.params.sensor.toLowerCase();
  if (currentSensorData.hasOwnProperty(sensorName)) {
    res.json({ [sensorName]: currentSensorData[sensorName] });
  } else {
    res.status(404).json({ error: 'Sensor not found' });
  }
});

// API endpoint for connection status
app.get('/api/status', (req, res) => {
  res.json({
    connected: isConnected,
    portName: serialPort?.path || 'None',
    lastUpdate: new Date().toISOString(),
    sensorData: currentSensorData
  });
});

// List available COM ports
app.get('/api/ports', async (req, res) => {
  try {
    const { SerialPort: SP } = require('serialport');
    const ports = await SP.list();
    res.json(ports);
  } catch (error) {
    res.status(500).json({ error: 'Could not list ports' });
  }
});

// Connect to a specific port
app.post('/api/connect', (req, res) => {
  const { port } = req.body;
  if (!port) {
    return res.status(400).json({ error: 'Port not specified' });
  }
  
  if (serialPort) {
    serialPort.close();
  }
  
  initializeSerial(port);
  res.json({ message: `Attempting to connect to ${port}` });
});

// Disconnect from serial port
app.post('/api/disconnect', (req, res) => {
  if (serialPort) {
    serialPort.close();
    res.json({ message: 'Disconnected from serial port' });
  } else {
    res.status(400).json({ error: 'No serial port connected' });
  }
});

// Start server
app.listen(PORT, () => {
  console.log(`🚀 Server running at http://localhost:${PORT}`);
  console.log('Available endpoints:');
  console.log('  GET  /api/sensors         - Get all sensor data');
  console.log('  GET  /api/sensors/:sensor - Get specific sensor');
  console.log('  GET  /api/status          - Get connection status');
  console.log('  GET  /api/ports           - List available COM ports');
  console.log('  POST /api/connect         - Connect to port ({"port": "COM3"})');
  console.log('  POST /api/disconnect      - Disconnect from port');
});

// Auto-detect and connect to first available ESP32
async function autoConnect() {
  try {
    const { SerialPort: SP } = require('serialport');
    const ports = await SP.list();
    
    if (ports.length > 0) {
      const portName = ports[0].path;
      console.log(`🔍 Found port: ${portName}, attempting connection...`);
      initializeSerial(portName);
    } else {
      console.log('⚠️  No serial ports found. Waiting for connection...');
    }
  } catch (error) {
    console.error('Error detecting ports:', error);
  }
}

// Try to auto-connect on startup
setTimeout(autoConnect, 1000);
