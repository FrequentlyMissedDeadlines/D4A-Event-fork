// K10 Bot Script
// Mode: Keyboard Control (WASD)
// Controls: W/S = forward/backward, A/D = turn left/right

// -----------------------------------------------------------------------------
// Connection Setup (same pattern for all scripts)
// 1) Set BOT_IP, BOT_PORT and BOT_TOKEN
// 2) Run the script and wait for a "✓ Connected..." log line
// -----------------------------------------------------------------------------
const BOT_IP = '192.168.4.1';
const BOT_PORT = '81';
const BOT_TOKEN = 'YOUR BOT TOKEN HERE';

const LEFT_WHEEL = 0;
const RIGHT_WHEEL = 1;
const keys = {};
let servosAttached = false;

function ensureServosAttached() {
  if (servosAttached) return;
  if (typeof isMasterRegistered !== 'undefined' && !isMasterRegistered) return;

  attachServo(LEFT_WHEEL, SERVO_TYPES.ROTATIONAL);
  attachServo(RIGHT_WHEEL, SERVO_TYPES.ROTATIONAL);
  servosAttached = true;
  _scriptLog('✓ Servos attached');
}

function updateMovement() {
  ensureServosAttached();

  const w = keys['w'];
  const a = keys['a'];
  const s = keys['s'];
  const d = keys['d'];

  let leftSpeed = 0;
  let rightSpeed = 0;

  if (w) { leftSpeed += 100; rightSpeed += 100; }
  if (s) { leftSpeed -= 100; rightSpeed -= 100; }
  if (a) { leftSpeed -= 50;  rightSpeed += 50; }
  if (d) { leftSpeed += 50;  rightSpeed -= 50; }

  setServoSpeeds([[LEFT_WHEEL, leftSpeed], [RIGHT_WHEEL, rightSpeed]]);
}

async function initializeKeyboardControl() {
  const connected = await getBotControl(BOT_IP, BOT_PORT, BOT_TOKEN);
  if (!connected) {
    _scriptLog('❌ Connection failed. Check BOT_IP, BOT_PORT and BOT_TOKEN.');
    return;
  }

  ensureServosAttached();
  _scriptLog('✓ Connected. Keyboard control ready (W=forward, S=back, A=left, D=right).');
}

initializeKeyboardControl();

// Step 2 — override keyboard hooks
CUSTOMCONTROL.onKeyDown = function(event) {
  const key = event.key.toLowerCase();
  if (!['w', 'a', 's', 'd'].includes(key)) return;

  event.preventDefault();
  keys[key] = true;
  updateMovement();
};

CUSTOMCONTROL.onKeyUp = function(event) {
  const key = event.key.toLowerCase();
  if (!['w', 'a', 's', 'd'].includes(key)) return;

  event.preventDefault();
  delete keys[key];
  updateMovement();
};