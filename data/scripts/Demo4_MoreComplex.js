// K10 Bot Script
// Mode: State Machine (Complex Behavior)
// Controls: A = explore, B = capture

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
const ARM = 2;
let servosAttached = false;

let currentState = 'idle';

function ensureServosAttached() {
  if (servosAttached) return;
  if (typeof isMasterRegistered !== 'undefined' && !isMasterRegistered) return;

  attachServo(LEFT_WHEEL,  SERVO_TYPES.ROTATIONAL);
  attachServo(RIGHT_WHEEL, SERVO_TYPES.ROTATIONAL);
  attachServo(ARM,         SERVO_TYPES.ANGULAR_270);
  servosAttached = true;
  _scriptLog('✓ Servos attached');
}

async function initializeComplexControl() {
  const connected = await getBotControl(BOT_IP, BOT_PORT, BOT_TOKEN);
  if (!connected) {
    _scriptLog('❌ Connection failed. Check BOT_IP, BOT_PORT and BOT_TOKEN.');
    return;
  }

  ensureServosAttached();
  _scriptLog('✓ Connected. Complex control ready (A=explore, B=capture).');
}

initializeComplexControl();

function setState(newState) {
  currentState = newState;
  _scriptLog('→ State: ' + newState);
}

async function explore() {
  if (currentState !== 'explore') return;
  _scriptLog('Exploring...');
  
  setServoSpeeds([[LEFT_WHEEL, 100], [RIGHT_WHEEL, 100]]);
  await delay(2000);
  
  if (currentState === 'explore') {
    setServoSpeeds([[LEFT_WHEEL, 0], [RIGHT_WHEEL, 0]]);
    setState('idle');
  }
}

async function capture() {
  if (currentState !== 'capture') return;
  setServoAngle(ARM, 225);  // arm up   (¾ of 270° range)
  await delay(500);
  setServoAngle(ARM, 135);  // arm mid  (center of 270° range)

  if (currentState === 'capture') {
    setState('idle');
  }
}

// Gamepad control
CUSTOMCONTROL.processGamepadInput = function(gamepad) {
  ensureServosAttached();

  if (gamepad.buttons[XBOX_BUTTONS.A].pressed && currentState !== 'explore') {
    setState('explore');
    explore();
  } else if (gamepad.buttons[XBOX_BUTTONS.B].pressed && currentState !== 'capture') {
    setState('capture');
    capture();
  }
};