// K10 Bot Script
// Mode: Analog Stick Control (with deadzone)
// Controls: Left stick Y = forward/backward, X = steer

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
const DEADZONE = 0.15;  // Prevent stick drift
let servosAttached = false;

function ensureServosAttached() {
  if (servosAttached) return;
  if (typeof isMasterRegistered !== 'undefined' && !isMasterRegistered) return;

  attachServo(LEFT_WHEEL,  SERVO_TYPES.ROTATIONAL);
  attachServo(RIGHT_WHEEL, SERVO_TYPES.ROTATIONAL);
  servosAttached = true;
  _scriptLog('✓ Servos attached');
}

async function initializeAnalogStickControl() {
  const connected = await getBotControl(BOT_IP, BOT_PORT, BOT_TOKEN);
  if (!connected) {
    _scriptLog('❌ Connection failed. Check BOT_IP, BOT_PORT and BOT_TOKEN.');
    return;
  }

  ensureServosAttached();
  _scriptLog('✓ Connected. Analog stick control ready (left stick to drive).');
}

initializeAnalogStickControl();

function setSpeed(leftSpeed, rightSpeed) {
  setServoSpeeds([[LEFT_WHEEL, leftSpeed], [RIGHT_WHEEL, rightSpeed]]);
}

// Step 2 — override gamepad hook
CUSTOMCONTROL.processGamepadInput = function(gamepad) {
  ensureServosAttached();

  let stickX = gamepad.axes[STICK_AXES.LEFT_X];
  let stickY = gamepad.axes[STICK_AXES.LEFT_Y];

  // Apply deadzone
  if (Math.abs(stickX) < DEADZONE) stickX = 0;
  if (Math.abs(stickY) < DEADZONE) stickY = 0;

  // Negate stickY: pushing forward gives Y = -1.0 on most browsers.
  // Tank drive mixing: left = forward - turn, right = forward + turn.
  const forward = -stickY;
  const leftSpeed  = Math.round((forward - stickX) * 100);
  const rightSpeed = Math.round((forward + stickX) * 100);

  // Clamp to valid range [-100, +100]
  setSpeed(
    Math.max(-100, Math.min(100, leftSpeed)),
    Math.max(-100, Math.min(100, rightSpeed))
  );
};