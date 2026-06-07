// K10 Bot Script
// Mode: Keyboard Tank (2 wheels + 2 arms)
// Controls: Arrow keys for drive, Q/W/E and A/S/D for arms

// -----------------------------------------------------------------------------
// Connection Setup (same pattern for all scripts)
// 1) Set BOT_IP, BOT_PORT and BOT_TOKEN
// 2) Run the script and wait for a "✓ Connected..." log line
// -----------------------------------------------------------------------------
const BOT_IP = '192.168.4.1';
const BOT_PORT = '81';
const BOT_TOKEN = 'YOUR BOT TOKEN HERE';

// -----------------------------------------------------------------------------
// Servo Mapping
// -----------------------------------------------------------------------------
// STEP 1 — channel names
const MyServos = {
  LEFT_WHEEL: 0,
  RIGHT_WHEEL: 1,
  ARM_1: 2,
  ARM_2: 3
};
const keyStates = {};
let servosAttached = false;

function ensureServosAttached() {
  if (servosAttached) return;
  if (typeof isMasterRegistered !== 'undefined' && !isMasterRegistered) return;

  attachServo(MyServos.LEFT_WHEEL, SERVO_TYPES.ROTATIONAL);
  attachServo(MyServos.RIGHT_WHEEL, SERVO_TYPES.ROTATIONAL);
  attachServo(MyServos.ARM_1, SERVO_TYPES.ANGULAR_270);
  attachServo(MyServos.ARM_2, SERVO_TYPES.ANGULAR_270);
  servosAttached = true;
  _scriptLog('✓ Servos attached');
}

async function initializeKeyboardTankControl() {
  const connected = await getBotControl(BOT_IP, BOT_PORT, BOT_TOKEN);
  if (!connected) {
    _scriptLog('❌ Connection failed. Check BOT_IP, BOT_PORT and BOT_TOKEN.');
    return;
  }

  ensureServosAttached();
  _scriptLog('✓ Connected. Keyboard tank control ready.');
}

initializeKeyboardTankControl();

// -----------------------------------------------------------------------------
// Actions
// -----------------------------------------------------------------------------

function moveForward(speed = 100) {
  setServoSpeeds([[MyServos.LEFT_WHEEL, speed], [MyServos.RIGHT_WHEEL, speed]]);
}

function moveBackward(speed = 100) {
  setServoSpeeds([[MyServos.LEFT_WHEEL, -speed], [MyServos.RIGHT_WHEEL, -speed]]);
}

function rotateCounterClockwise(speed = 100) {
  setServoSpeeds([[MyServos.LEFT_WHEEL, -speed], [MyServos.RIGHT_WHEEL, speed]]);
}

function rotateClockwise(speed = 100) {
  setServoSpeeds([[MyServos.LEFT_WHEEL, speed], [MyServos.RIGHT_WHEEL, -speed]]);
}

function stopWheels() {
  setServoSpeeds([[MyServos.LEFT_WHEEL, 0], [MyServos.RIGHT_WHEEL, 0]]);
}

function ARM1_Up() { setServoAngle(MyServos.ARM_1, 90); }
function ARM1_Mid() { setServoAngle(MyServos.ARM_1, 0); }
function ARM1_Down() { setServoAngle(MyServos.ARM_1, -90); }
function ARM2_Up() { setServoAngle(MyServos.ARM_2, 90); }
function ARM2_Mid() { setServoAngle(MyServos.ARM_2, 0); }
function ARM2_Down() { setServoAngle(MyServos.ARM_2, -90); }



CUSTOMCONTROL.onKeyDown = function (event) {
  ensureServosAttached();

  const key = event.key.toLowerCase();

  // Prevent default for arrow keys
  if (['arrowup', 'arrowdown', 'arrowleft', 'arrowright'].includes(key)) {
    event.preventDefault();
  }

  // Skip if key already pressed
  if (keyStates[key]) return;
  keyStates[key] = true;

  // Map keys to actions
  switch (key) {
    case 'arrowup':
      moveForward();
      break;
    case 'arrowdown':
      moveBackward();
      break;
    case 'arrowleft':
      rotateCounterClockwise();
      break;
    case 'arrowright':
      rotateClockwise();
      break;
    case 'q':
      ARM1_Up();
      break;
    case 'w':
      ARM1_Mid();
      break;
    case 'e':
      ARM1_Down();
      break;
    case 'a':
      ARM2_Up();
      break;
    case 's':
      ARM2_Mid();
      break;
    case 'd':
      ARM2_Down();
      break;


  }
};

/**
 * Handle keyboard key up
 */
CUSTOMCONTROL.onKeyUp = function (event) {
  const key = event.key.toLowerCase();
  keyStates[key] = false;

  switch (key) {
    case 'arrowup':
    case 'arrowdown':
    case 'arrowleft':
    case 'arrowright':
      stopWheels();
      break;
    case 'q':
      ARM1_Mid();
      break;
  }
};
// -----------------------------------------------------------------------------

