// K10 Bot Script
// Mode: D-Pad Movement
// Controls: D-Pad up/down/left/right

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
let servosAttached = false;

function ensureServosAttached() {
  if (servosAttached) return;
  if (typeof isMasterRegistered !== 'undefined' && !isMasterRegistered) return;

  attachServo(LEFT_WHEEL, SERVO_TYPES.ROTATIONAL);
  attachServo(RIGHT_WHEEL, SERVO_TYPES.ROTATIONAL);
  servosAttached = true;
  _scriptLog('✓ Servos attached');
}

async function initializeDPadControl() {
  const connected = await getBotControl(BOT_IP, BOT_PORT, BOT_TOKEN);
  if (!connected) {
    _scriptLog('❌ Connection failed. Check BOT_IP, BOT_PORT and BOT_TOKEN.');
    return;
  }

  ensureServosAttached();
  _scriptLog('✓ Connected. D-Pad control ready.');
}

initializeDPadControl();

function moveForward() {
  setServoSpeeds([[LEFT_WHEEL, 100], [RIGHT_WHEEL, 100]]);
  _scriptLog('→ Moving forward');
}

function moveBackward() {
  setServoSpeeds([[LEFT_WHEEL, -100], [RIGHT_WHEEL, -100]]);
  _scriptLog('← Moving backward');
}

function turnLeft() {
  setServoSpeeds([[LEFT_WHEEL, -100], [RIGHT_WHEEL, 100]]);
  _scriptLog('↙ Turning left');
}

function turnRight() {
  setServoSpeeds([[LEFT_WHEEL, 100], [RIGHT_WHEEL, -100]]);
  _scriptLog('↘ Turning right');
}

function stop() {
  setServoSpeeds([[LEFT_WHEEL, 0], [RIGHT_WHEEL, 0]]);
  _scriptLog('⏸ Stopped');
}

// Handle D-Pad input
CUSTOMCONTROL.processGamepadInput = function(gamepad) {
  ensureServosAttached();

  if (gamepad.buttons[XBOX_BUTTONS.DPAD_UP].pressed) {
    moveForward();
  } else if (gamepad.buttons[XBOX_BUTTONS.DPAD_DOWN].pressed) {
    moveBackward();
  } else if (gamepad.buttons[XBOX_BUTTONS.DPAD_LEFT].pressed) {
    turnLeft();
  } else if (gamepad.buttons[XBOX_BUTTONS.DPAD_RIGHT].pressed) {
    turnRight();
  } else {
    stop();
  }
};