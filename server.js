// server.js - Deploy this to Render with WebSocket support
const express = require('express');
const http = require('http');
const WebSocket = require('ws');
const mongoose = require('mongoose');
const cors = require('cors');
require('dotenv').config();

const app = express();
const server = http.createServer(app);
const wss = new WebSocket.Server({ server });

const PORT = process.env.PORT || 3000;

// Middleware
app.use(cors());
app.use(express.json());

// MongoDB Connection
mongoose.connect(process.env.MONGODB_URI || 'mongodb://localhost:27017/esp32motors', {
  useNewUrlParser: true,
  useUnifiedTopology: true
})
.then(() => console.log('✓ MongoDB Connected'))
.catch(err => console.error('✗ MongoDB Connection Error:', err));

// Motor State Schema
const motorStateSchema = new mongoose.Schema({
  motorA: { type: Boolean, default: false },
  motorB: { type: Boolean, default: false },
  voltage: { type: Number, default: 0 },
  current: { type: Number, default: 0 },
  power: { type: Number, default: 0 },
  lastUpdated: { type: Date, default: Date.now }
});

const MotorState = mongoose.model('MotorState', motorStateSchema);

// Initialize motor state
async function initializeState() {
  const count = await MotorState.countDocuments();
  if (count === 0) {
    await MotorState.create({
      motorA: false,
      motorB: false,
      voltage: 0,
      current: 0,
      power: 0
    });
    console.log('✓ Initial motor state created');
  }
}
initializeState();

// Store connected clients
let esp32Client = null;
const webClients = new Set();

// WebSocket Connection Handler
wss.on('connection', (ws, req) => {
  console.log('📡 New WebSocket connection');
  
  ws.on('message', async (message) => {
    try {
      const data = JSON.parse(message);
      
      // ESP32 Registration
      if (data.type === 'esp32_register') {
        esp32Client = ws;
        ws.clientType = 'esp32';
        console.log('🤖 ESP32 connected via WebSocket');
        
        // Send current state to ESP32
        const state = await MotorState.findOne().sort({ lastUpdated: -1 });
        if (state) {
          ws.send(JSON.stringify({
            type: 'initial_state',
            motorA: state.motorA,
            motorB: state.motorB
          }));
        }
        
        // Notify all web clients that ESP32 is online
        broadcastToWeb({
          type: 'esp32_status',
          connected: true
        });
      }
      
      // Web Client Registration
      else if (data.type === 'web_register') {
        webClients.add(ws);
        ws.clientType = 'web';
        console.log('🌐 Web client connected');
        
        // Send current state to web client
        const state = await MotorState.findOne().sort({ lastUpdated: -1 });
        if (state) {
          ws.send(JSON.stringify({
            type: 'state_update',
            motorA: state.motorA,
            motorB: state.motorB,
            voltage: state.voltage,
            current: state.current,
            power: state.power,
            lastUpdated: state.lastUpdated
          }));
        }
        
        // Send ESP32 connection status
        ws.send(JSON.stringify({
          type: 'esp32_status',
          connected: esp32Client !== null && esp32Client.readyState === WebSocket.OPEN
        }));
      }
      
      // Motor Control Command from Web
      else if (data.type === 'motor_control') {
        console.log(`🎮 Control: Motor ${data.motor} -> ${data.state ? 'ON' : 'OFF'}`);
        
        // Send command directly to ESP32
        if (esp32Client && esp32Client.readyState === WebSocket.OPEN) {
          esp32Client.send(JSON.stringify({
            type: 'motor_command',
            motor: data.motor,
            state: data.state
          }));
          
          // Update database
          const updateField = `motor${data.motor}`;
          await MotorState.findOneAndUpdate(
            {},
            { [updateField]: data.state, lastUpdated: new Date() },
            { upsert: true }
          );
        } else {
          // ESP32 not connected, send error to web client
          ws.send(JSON.stringify({
            type: 'error',
            message: 'ESP32 not connected'
          }));
        }
      }
      
      // State Update from ESP32
      else if (data.type === 'state_update') {
        // Update database
        await MotorState.findOneAndUpdate(
          {},
          {
            motorA: data.motorA,
            motorB: data.motorB,
            voltage: data.voltage || 0,
            current: data.current || 0,
            power: data.power || 0,
            lastUpdated: new Date()
          },
          { upsert: true }
        );
        
        // Broadcast to all web clients
        broadcastToWeb({
          type: 'state_update',
          motorA: data.motorA,
          motorB: data.motorB,
          voltage: data.voltage || 0,
          current: data.current || 0,
          power: data.power || 0,
          lastUpdated: new Date()
        });
      }
      
    } catch (error) {
      console.error('WebSocket message error:', error);
    }
  });
  
  ws.on('close', () => {
    if (ws.clientType === 'esp32') {
      console.log('🤖 ESP32 disconnected');
      esp32Client = null;
      
      // Notify all web clients
      broadcastToWeb({
        type: 'esp32_status',
        connected: false
      });
    } else if (ws.clientType === 'web') {
      console.log('🌐 Web client disconnected');
      webClients.delete(ws);
    }
  });
  
  ws.on('error', (error) => {
    console.error('WebSocket error:', error);
  });
});

// Broadcast message to all web clients
function broadcastToWeb(data) {
  const message = JSON.stringify(data);
  webClients.forEach(client => {
    if (client.readyState === WebSocket.OPEN) {
      client.send(message);
    }
  });
}

// REST API endpoints (fallback)
app.get('/api/status', async (req, res) => {
  try {
    const state = await MotorState.findOne().sort({ lastUpdated: -1 });
    if (!state) {
      return res.json({
        motorA: false,
        motorB: false,
        voltage: 0,
        current: 0,
        power: 0,
        lastUpdated: new Date()
      });
    }
    res.json({
      motorA: state.motorA,
      motorB: state.motorB,
      voltage: state.voltage,
      current: state.current,
      power: state.power,
      lastUpdated: state.lastUpdated,
      esp32Connected: esp32Client !== null && esp32Client.readyState === WebSocket.OPEN
    });
  } catch (error) {
    res.status(500).json({ error: error.message });
  }
});

// Health check
app.get('/health', (req, res) => {
  res.json({ 
    status: 'OK', 
    timestamp: new Date(),
    uptime: process.uptime(),
    mongoStatus: mongoose.connection.readyState === 1 ? 'connected' : 'disconnected',
    esp32Connected: esp32Client !== null && esp32Client.readyState === WebSocket.OPEN,
    webClients: webClients.size
  });
});

// Root endpoint
app.get('/', (req, res) => {
  res.json({ 
    message: 'ESP32 Motor Control Backend API with WebSocket',
    version: '3.0',
    websocket: 'ws://' + req.get('host'),
    endpoints: {
      websocket: {
        'ESP32': 'Send {type: "esp32_register"}',
        'Web': 'Send {type: "web_register"}',
        'Control': 'Send {type: "motor_control", motor: "A"/"B", state: true/false}'
      },
      rest: {
        'GET /api/status': 'Get current motor state',
        'GET /health': 'Health check'
      }
    }
  });
});

server.listen(PORT, () => {
  console.log('=================================');
  console.log('ESP32 Motor Control Backend');
  console.log('WebSocket Enabled - Real-time');
  console.log('=================================');
  console.log(`HTTP: http://localhost:${PORT}`);
  console.log(`WebSocket: ws://localhost:${PORT}`);
  console.log(`Environment: ${process.env.NODE_ENV || 'development'}`);
  console.log('=================================\n');
});
