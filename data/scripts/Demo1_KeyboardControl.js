// Template 1: Keyboard Control (WASD)
// Use this for keyboard-based movement on a diff-drive (tank) robot.
// W/S = forward/backward, A/D = turn left/right.

// *********************************************************************************
// ** IMPORTANT: UPDATE THE BOT CONTROL LINE BELOW WITH YOUR BOT'S IP AND TOKEN ! **
// *********************************************************************************
const BOT_IP = '192.168.4.1';
const BOT_PORT = '81';
const BOT_TOKEN = 'YOUR BOT TOKEN HERE';
  const connected = await getBotControl(BOT_IP, BOT_PORT, BOT_TOKEN);
  if (!connected) {
    alert('❌ Could not connect to bot. Check IP and token.');
    return;
  }

const LEFT_WHEEL = 0;
const RIGHT_WHEEL = 1;
const keys = {};
let servos_attached = false;

function ensureServosAttached() {
  if (servos_attached) return;
  if (typeof isMasterRegistered !== 'undefined' && !isMasterRegistered) return;

  attachServo(LEFT_WHEEL, SERVO_TYPES.ROTATIONAL);
  attachServo(RIGHT_WHEEL, SERVO_TYPES.ROTATIONAL);
  servos_attached = true;
  _scriptLog('✓ Wheel servos attached');
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
    _scriptLog('❌ Could not connect to bot. Check IP/port/token.');
    return;
  }

  ensureServosAttached();
  _scriptLog('✓ Keyboard control ready (W=forward, S=back, A=left, D=right)');
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