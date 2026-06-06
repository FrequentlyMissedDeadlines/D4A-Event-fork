// This script is intended to test your servos 
// before you mount them:
// Keys:
//   1  -> angular servos to minimum angle, rotational servos to max negative speed
//   0  -> angular servos to center angle, rotational servos to speed 0
//   9  -> angular servos to maximum angle, rotational servos to max positive speed

// *********************************************************************************
// ** IMPORTANT: UPDATE THE BOT CONTROL LINE BELOW WITH YOUR BOT'S IP AND TOKEN ! **
// *********************************************************************************
const BOT_TOKEN = 'YOUR BOT TOKEN HERE';
const BOT_IP = '192.168.4.1';

// *********************************************************************************
// ** IMPORTANT: DEFINE THE SERVO CONNECTION PINS
// *********************************************************************************
const ROTATIONAL_SERVO_PINS = [0, 1];
const ANGULAR_SERVO_PINS = [4, 5];

// now let it go :)
const BOT_PORT = '81';

const connected = await getBotControl(BOT_IP, BOT_PORT, BOT_TOKEN);
  if (!connected) {
    alert('❌ Could not connect to bot. Check IP and token.');
    return;
  }
// Tuning values (change if your setup requires different limits).
const ANGULAR_SERVO_TYPE = SERVO_TYPES.ANGULAR_270;
const ANGULAR_MIN_ANGLE = 0;
const ANGULAR_ZERO_ANGLE = 135;
const ANGULAR_MAX_ANGLE = 270;

const ROTATIONAL_MAX_NEGATIVE_SPEED = -100;
const ROTATIONAL_STOP_SPEED = 0;
const ROTATIONAL_MAX_POSITIVE_SPEED = 100;

function attachConfiguredServos() {
  for (const pin of ANGULAR_SERVO_PINS) {
    attachServo(pin, ANGULAR_SERVO_TYPE);
  }

  for (const pin of ROTATIONAL_SERVO_PINS) {
    attachServo(pin, SERVO_TYPES.ROTATIONAL);
  }
}

function sendSupportedAngularAngle(pin, angle) {
  const clampedAngle = Math.max(0, Math.min(270, Math.round(angle)));
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

  _scriptLog('Angular pin ' + pin + ' -> ' + clampedAngle + ' deg via cmd 0x24');
}

function setAllAngularServos(angle) {
  for (const pin of ANGULAR_SERVO_PINS) {
    sendSupportedAngularAngle(pin, angle);
  }
}

function setAllRotationalServos(speed) {
  const speedPairs = ROTATIONAL_SERVO_PINS.map(function(pin) {
    return [pin, speed];
  });

  if (speedPairs.length > 0) {
    setServoSpeeds(speedPairs);
  }
}

function moveAllToNeutral() {
  setAllAngularServos(ANGULAR_ZERO_ANGLE);
  setAllRotationalServos(ROTATIONAL_STOP_SPEED);
  _scriptLog('Applied neutral: angular=135, rotational=0');
}

function moveAllToMinimum() {
  setAllAngularServos(ANGULAR_MIN_ANGLE);
  setAllRotationalServos(ROTATIONAL_MAX_NEGATIVE_SPEED);
  _scriptLog('Applied minimum: angular=0, rotational=max negative');
}

function moveAllToMaximum() {
  setAllAngularServos(ANGULAR_MAX_ANGLE);
  setAllRotationalServos(ROTATIONAL_MAX_POSITIVE_SPEED);
  _scriptLog('Applied maximum: angular=270, rotational=max positive');
}

attachConfiguredServos();
_scriptLog('Servo groups ready. Keys: 1 (min), 0 (center), 9 (max)');
_scriptLog('Waiting briefly for servo type attach before first angular move...');
await delay(150);
moveAllToNeutral();

CUSTOMCONTROL.onKeyDown = function(event) {
  const key = event.key;

  if (key === '1') {
        event.preventDefault();
    moveAllToMinimum();
    return;
  } 
  if (key === ' ' || key === '0' || key === '5') {
    event.preventDefault();
    moveAllToNeutral();
    return;
  } 
  if (key === '9') {
        event.preventDefault();
    moveAllToMaximum();
    return;
  }
};