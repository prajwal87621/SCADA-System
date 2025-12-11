// server.js - Deploy this to Render
const express = require('express');
const mongoose = require('mongoose');
const cors = require('cors');
require('dotenv').config();

const app = express();
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

// Command Queue Schema (for ESP32 to poll)
const commandSchema = new mongoose.Schema({
  motor: { type: String, required: true }, // 'A' or 'B'
  action: { type: String, required: true }, // 'toggle'
  processed: { type: Boolean, default: false },
  createdAt: { type: Date, default: Date.now, expires: 60 } // Auto-delete after 60 seconds
});

const Command = mongoose.model('Command', commandSchema);

// Initialize motor state if not exists
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

// ==================== WEB API ENDPOINTS ====================

// Get current motor state (for web interface)
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
      lastUpdated: state.lastUpdated
    });
  } catch (error) {
    res.status(500).json({ error: error.message });
  }
});

// Toggle motor (from web interface) - creates command for ESP32
app.post('/api/motor/:motor', async (req, res) => {
  try {
    const motor = req.params.motor.toUpperCase();
    
    if (motor !== 'A' && motor !== 'B') {
      return res.status(400).json({ error: 'Invalid motor. Use A or B' });
    }

    // Create command for ESP32 to process
    await Command.create({
      motor: motor,
      action: 'toggle'
    });

    console.log(`→ Command created: Toggle Motor ${motor}`);

    res.json({ 
      success: true, 
      message: `Command sent to toggle Motor ${motor}` 
    });
  } catch (error) {
    res.status(500).json({ error: error.message });
  }
});

// ==================== ESP32 ENDPOINTS ====================

// ESP32 polls for new commands
app.get('/esp32/commands', async (req, res) => {
  try {
    const commands = await Command.find({ processed: false });
    res.json({ commands });
  } catch (error) {
    res.status(500).json({ error: error.message });
  }
});

// ESP32 marks command as processed
app.post('/esp32/command/:id/processed', async (req, res) => {
  try {
    await Command.findByIdAndUpdate(req.params.id, { processed: true });
    res.json({ success: true });
  } catch (error) {
    res.status(500).json({ error: error.message });
  }
});

// ESP32 updates current state
app.post('/esp32/update', async (req, res) => {
  try {
    const { motorA, motorB, voltage, current, power } = req.body;
    
    await MotorState.findOneAndUpdate(
      {},
      {
        motorA,
        motorB,
        voltage,
        current,
        power,
        lastUpdated: new Date()
      },
      { upsert: true, new: true }
    );

    console.log(`✓ ESP32 Update: A:${motorA ? 'ON' : 'OFF'} B:${motorB ? 'ON' : 'OFF'} ${voltage.toFixed(2)}V`);

    res.json({ success: true });
  } catch (error) {
    res.status(500).json({ error: error.message });
  }
});

// Health check
app.get('/health', (req, res) => {
  res.json({ 
    status: 'OK', 
    timestamp: new Date(),
    uptime: process.uptime()
  });
});

// Root endpoint
app.get('/', (req, res) => {
  res.json({ 
    message: 'ESP32 Motor Control Backend API',
    version: '2.0',
    endpoints: {
      web: {
        'GET /api/status': 'Get current motor state',
        'POST /api/motor/:motor': 'Toggle motor (A or B)'
      },
      esp32: {
        'GET /esp32/commands': 'Poll for new commands',
        'POST /esp32/command/:id/processed': 'Mark command as processed',
        'POST /esp32/update': 'Update motor state and sensor data'
      },
      utility: {
        'GET /health': 'Health check'
      }
    }
  });
});

app.listen(PORT, () => {
  console.log('=================================');
  console.log('ESP32 Motor Control Backend');
  console.log('=================================');
  console.log(`Server: http://localhost:${PORT}`);
  console.log(`Environment: ${process.env.NODE_ENV || 'development'}`);
  console.log('=================================\n');
});