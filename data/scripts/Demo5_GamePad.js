// K10 Bot Script
// Mode: GamePad Turret + Tracks
// Controls:
// - Left stick: drive tracks (speed + direction)
// - XBOX/Guide: reset all servos to neutral and stop tracks
// - Left trigger: servo 2 from min (released) to max (pushed)
// - Right trigger: servo 3 from min (released) to max (pushed)
// - Left bumper: servo 4 min/released, max/pressed
// - Right bumper: servo 5 min/released, max/pressed
// - A button: forces both servo 4 and servo 5 to max

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
const LEFT_TRACK = 0;   // continuous
const RIGHT_TRACK = 1;  // continuous
const TURRET_1 = 2;      // angular
const TURRET_2 = 3;      // angular
const FIRE_1 = 4;      // angular
const FIRE_2 = 5;      // angular

const ANGLE_MIN = 0;
const ANGLE_MID = 135;
const ANGLE_MAX = 270;
const STICK_DEADZONE = 0.05;

let servosAttached = false;
let lastGamepadLog = '';

function clamp(value, minValue, maxValue) {
  return Math.max(minValue, Math.min(maxValue, value));
}

function ensureServosAttached() {
  if (servosAttached) return;
  if (typeof isMasterRegistered !== 'undefined' && !isMasterRegistered) return;

  attachServo(LEFT_TRACK, SERVO_TYPES.ROTATIONAL);
  attachServo(RIGHT_TRACK, SERVO_TYPES.ROTATIONAL);
  attachServo(TURRET_1, SERVO_TYPES.ANGULAR_270);
  attachServo(TURRET_2, SERVO_TYPES.ANGULAR_270);
  attachServo(FIRE_1, SERVO_TYPES.ANGULAR_270);
  attachServo(FIRE_2, SERVO_TYPES.ANGULAR_270);

  servosAttached = true;
  _scriptLog('✓ Servos attached');
}

async function initializeDemo5GamePad() {
  const connected = await getBotControl(BOT_IP, BOT_PORT, BOT_TOKEN);
  if (!connected) {
    _scriptLog('❌ Connection failed. Check BOT_IP, BOT_PORT and BOT_TOKEN.');
    return;
  }

  ensureServosAttached();
  await delay(150);
  setNeutralAll();
  _scriptLog('✓ Connected. Demo5 ready: stick=drive, LT/RT=S2/S3, LB/RB/A=S4/S5, Guide=neutral.');
}

initializeDemo5GamePad();

function setNeutralAll() {
  setServoSpeeds([
    [LEFT_TRACK, 0],
    [RIGHT_TRACK, 0]
  ]);

  sendSupportedAngularAngle(TURRET_1, ANGLE_MID);
  sendSupportedAngularAngle(TURRET_2, ANGLE_MID);
  sendSupportedAngularAngle(FIRE_1, ANGLE_MID);
  sendSupportedAngularAngle(FIRE_2, ANGLE_MID);
}

function getButtonIndex(candidates, fallback) {
  if (typeof XBOX_BUTTONS === 'undefined') return fallback;
  for (const name of candidates) {
    if (typeof XBOX_BUTTONS[name] !== 'undefined') return XBOX_BUTTONS[name];
  }
  return fallback;
}

function getButtonIndices(candidates, fallbacks) {
  const indices = [];

  if (typeof XBOX_BUTTONS !== 'undefined') {
    for (const name of candidates) {
      if (typeof XBOX_BUTTONS[name] !== 'undefined') {
        indices.push(XBOX_BUTTONS[name]);
      }
    }
  }

  for (const idx of fallbacks) {
    indices.push(idx);
  }

  const unique = [];
  for (const idx of indices) {
    if (!unique.includes(idx)) unique.push(idx);
  }
  return unique;
}

function getButton(gamepad, index) {
  if (!gamepad || !gamepad.buttons || index < 0 || index >= gamepad.buttons.length) return null;
  return gamepad.buttons[index];
}

function getPressed(gamepad, index) {
  const btn = getButton(gamepad, index);
  return !!(btn && btn.pressed);
}

function getPressedAny(gamepad, indices) {
  for (const idx of indices) {
    if (getPressed(gamepad, idx)) return true;
  }
  return false;
}

function getValue01(gamepad, index) {
  const btn = getButton(gamepad, index);
  if (!btn) return 0;
  if (typeof btn.value === 'number') return clamp(btn.value, 0, 1);
  return btn.pressed ? 1 : 0;
}

function axis(gamepad, axisIndex, fallback) {
  if (!gamepad || !gamepad.axes || axisIndex < 0 || axisIndex >= gamepad.axes.length) return fallback;
  const v = gamepad.axes[axisIndex];
  return (typeof v === 'number') ? v : fallback;
}

function axisFromCandidates(gamepad, candidates, fallback) {
  for (const idx of candidates) {
    const value = axis(gamepad, idx, NaN);
    if (!Number.isNaN(value)) return value;
  }
  return fallback;
}

function unitToAngle(value01) {
  return Math.round(ANGLE_MIN + clamp(value01, 0, 1) * (ANGLE_MAX - ANGLE_MIN));
}

function sendSupportedAngularAngle(pin, angle) {
  const clampedAngle = Math.max(ANGLE_MIN, Math.min(ANGLE_MAX, Math.round(angle)));
  const mask = (1 << pin) & 0xFF;
  const angleHi = (clampedAngle >> 8) & 0xFF;
  const angleLo = clampedAngle & 0xFF;

  if (typeof sendWSFireAndForget === 'function'
      && typeof MOTOR_SERVO_ACTION !== 'undefined'
      && typeof MOTOR_SERVO_ACTION.SET_SERVOS_ANGLE !== 'undefined') {
    const packet = new Uint8Array([
      MOTOR_SERVO_ACTION.SET_SERVOS_ANGLE,
      mask,
      angleHi,
      angleLo
    ]);
    sendWSFireAndForget(packet);
  } else {
    setServoAngle(pin, clampedAngle);
  }
}

function formatTriggerValue(label, value01) {
  return label + '=' + Math.round(clamp(value01, 0, 1) * 100) + '%';
}

function formatButtonState(label, pressed) {
  return label + '=' + (pressed ? 'down' : 'up');
}

function logGamepadState(lt, rt, lbPressed, rbPressed, aPressed, guidePressed, leftSpeed, rightSpeed) {
  const logLine = [
    '🎮 Gamepad:',
    formatTriggerValue('LT', lt),
    formatTriggerValue('RT', rt),
    formatButtonState('LB', lbPressed),
    formatButtonState('RB', rbPressed),
    formatButtonState('A', aPressed),
    formatButtonState('Guide', guidePressed),
    'Tracks=' + leftSpeed + '/' + rightSpeed
  ].join(' ');

  if (logLine === lastGamepadLog) return;
  lastGamepadLog = logLine;
  _scriptLog(logLine);
}

CUSTOMCONTROL.processGamepadInput = function(gamepad) {
  ensureServosAttached();
  if (!servosAttached) return;

  const idxA = getButtonIndex(['A'], 0);
  const idxLBList = getButtonIndices(['LB', 'LEFT_BUMPER', 'LEFT_SHOULDER'], [4]);
  const idxRBList = getButtonIndices(['RB', 'RIGHT_BUMPER', 'RIGHT_SHOULDER'], [5]);
  const idxLT = getButtonIndex(['LT', 'LEFT_TRIGGER'], 6);
  const idxRT = getButtonIndex(['RT', 'RIGHT_TRIGGER'], 7);
  const idxGuideList = getButtonIndices(['XBOX', 'GUIDE', 'HOME', 'META'], [16, 8]);
  const guidePressed = getPressedAny(gamepad, idxGuideList);

  if (guidePressed) {
    setNeutralAll();
    logGamepadState(0, 0, false, false, false, true, 0, 0);
    return;
  }

  const axisLeftXList = [];
  const axisLeftYList = [];
  if (typeof STICK_AXES !== 'undefined') {
    if (typeof STICK_AXES.LEFT_X !== 'undefined') axisLeftXList.push(STICK_AXES.LEFT_X);
    if (typeof STICK_AXES.LEFT_Y !== 'undefined') axisLeftYList.push(STICK_AXES.LEFT_Y);
  }
  axisLeftXList.push(0, 2);
  axisLeftYList.push(1, 3);

  let stickX = axisFromCandidates(gamepad, axisLeftXList, 0);
  let stickY = axisFromCandidates(gamepad, axisLeftYList, 0);

  if (Math.abs(stickX) < STICK_DEADZONE) stickX = 0;
  if (Math.abs(stickY) < STICK_DEADZONE) stickY = 0;

  const forward = -stickY;
  const turn = stickX;

  const leftSpeed = clamp(Math.round((forward - turn) * 100), -100, 100);
  const rightSpeed = clamp(Math.round((forward + turn) * 100), -100, 100);

  setServoSpeeds([
    [LEFT_TRACK, leftSpeed],
    [RIGHT_TRACK, rightSpeed]
  ]);

  const lt = getValue01(gamepad, idxLT);
  const rt = getValue01(gamepad, idxRT);
  const lbPressed = getPressedAny(gamepad, idxLBList);
  const rbPressed = getPressedAny(gamepad, idxRBList);
  const aPressed = getPressed(gamepad, idxA);

  logGamepadState(lt, rt, lbPressed, rbPressed, aPressed, false, leftSpeed, rightSpeed);

  sendSupportedAngularAngle(TURRET_1, unitToAngle(lt));
  sendSupportedAngularAngle(TURRET_2, unitToAngle(rt));
  sendSupportedAngularAngle(FIRE_1, (lbPressed || aPressed) ? ANGLE_MAX : ANGLE_MIN);
  sendSupportedAngularAngle(FIRE_2, (rbPressed || aPressed) ? ANGLE_MAX : ANGLE_MIN);
};