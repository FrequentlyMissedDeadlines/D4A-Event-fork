// Keyboard Servo Group Control
// Keys:
//   Space -> angular servos to 0 deg, rotational servos to speed 0
//   1     -> angular servos to minimum angle, rotational servos to max negative speed
//   9     -> angular servos to maximum angle, rotational servos to max positive speed


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


// Configure your servo connection pins here.
const ANGULAR_SERVO_PINS = [2, 3];
const ROTATIONAL_SERVO_PINS = [0, 1];

// Tuning values (change if your setup requires different limits).
const ANGULAR_SERVO_TYPE = SERVO_TYPES.ANGULAR_270;
const ANGULAR_MIN_ANGLE = -135;
const ANGULAR_ZERO_ANGLE = 0;
const ANGULAR_MAX_ANGLE = 135;// ─────────────────────────────────────────────────────────────────────────────
// get control of bot (replace with your bot's IP, port, and token) 
// ─────────────────────────────────────────────────────────────────────────────
getBotControl('192.168.4.1', '81', '00000');

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

function setAllAngularServos(angle) {
  for (const pin of ANGULAR_SERVO_PINS) {
    setServoAngle(pin, angle);
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
  _scriptLog('Applied neutral: angular=0, rotational=0');
}

function moveAllToMinimum() {
  setAllAngularServos(ANGULAR_MIN_ANGLE);
  setAllRotationalServos(ROTATIONAL_MAX_NEGATIVE_SPEED);
  _scriptLog('Applied minimum: angular=min, rotational=max negative');
}

function moveAllToMaximum() {
  setAllAngularServos(ANGULAR_MAX_ANGLE);
  setAllRotationalServos(ROTATIONAL_MAX_POSITIVE_SPEED);
  _scriptLog('Applied maximum: angular=max, rotational=max positive');
}

attachConfiguredServos();
_scriptLog('Servo groups ready. Keys: Space, 1, 9');

CUSTOMCONTROL.onKeyDown = function(event) {
  const key = event.key;

  if (key === ' ' || event.code === 'Space') {
    event.preventDefault();
    moveAllToNeutral();
    return;
  }

  if (key === '1') {
    moveAllToMinimum();
    return;
  }

  if (key === '9') {
    moveAllToMaximum();
  }
};