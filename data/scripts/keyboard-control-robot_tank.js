// ─────────────────────────────────────────────────────────────────────────────
// sample-robot.js  —  Basic 2-motorized tankrobot with 2 arms
// ─────────────────────────────────────────────────────────────────────────────
// Servo wiring assumed:
//   CH 0  Left  wheel  (continuous rotation, green servo)
//   CH 1  Right wheel  (continuous rotation, green servo)
//   CH 2  ARM_1          (angular 270°, grey servo)
//   CH 3  ARM_2          (angular 270°, grey servo)
// ─────────────────────────────────────────────────────────────────────────────



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


// ─────────────────────────────────────────────────────────────────────────────

// ─────────────────────────────────────────────────────────────────────────────
// Servo mapping
// ─────────────────────────────────────────────────────────────────────────────
// STEP 1 — channel names
const MyServos = {
  LEFT_WHEEL: 0,
  RIGHT_WHEEL: 1,
  ARM_1: 2,
  ARM_2: 3
};

// STEP 2 — attach servos once
attachServo(MyServos.LEFT_WHEEL, SERVO_TYPES.ROTATIONAL);
attachServo(MyServos.RIGHT_WHEEL, SERVO_TYPES.ROTATIONAL);
attachServo(MyServos.ARM_1, SERVO_TYPES.ANGULAR_270);
attachServo(MyServos.ARM_2, SERVO_TYPES.ANGULAR_270);
_scriptLog('✓ Servos attached');


// ─────────────────────────────────────────────────────────────────────────────
// Name some actions
// ─────────────────────────────────────────────────────────────────────────────

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
}

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
      relax();
      break;
  }
}
// ─────────────────────────────────────────────────────────────────────────────
// ─────────────────────────────────────────────────────────────────────────────

