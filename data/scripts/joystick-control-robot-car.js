// ─────────────────────────────────────────────────────────────────────────────
// sample-robot.js  —  Basic 2-motorized wheels robot with 2 arms
// ─────────────────────────────────────────────────────────────────────────────
// Servo wiring assumed:
//   CH 0  Motor      (continuous rotation, green servo)
//   CH 1  Direction  (angular 270°, grey servo)
//   CH 2  Arm        (angular 270°, grey servo)
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
  MOTOR: 0,
  DIRECTION: 1,
  ARM: 2
};
current_direction = 0;
// STEP 2 — attach servos once
attachServo(MyServos.MOTOR, SERVO_TYPES.ROTATIONAL);
attachServo(MyServos.DIRECTION, SERVO_TYPES.ANGULAR_270);
attachServo(MyServos.ARM, SERVO_TYPES.ANGULAR_270);
_scriptLog('✓ Servos attached');


// ─────────────────────────────────────────────────────────────────────────────
// Name some actions
// ─────────────────────────────────────────────────────────────────────────────

function moveForward(speed = 100) {
  setServoSpeeds([[MyServos.MOTOR, speed]]);
}

function moveBackward(speed = 100) {
  setServoSpeeds([[MyServos.MOTOR, -speed]]);
}
function adjustDirection(increment = 1) {
  current_direction += increment;
  setServoSpeeds([[MyServos.DIRECTION, current_direction]]);
}
function direction(position = 0) {
  setServoSpeeds([[MyServos.DIRECTION, position]]);
}


function stop() {
  setServoSpeeds([[MyServos.MOTOR, 0]]);
}

function ARM_Up() { setServoAngle(MyServos.ARM, 90); }
function ARM_Mid() { setServoAngle(MyServos.ARM, 0); }
function ARM_Down() { setServoAngle(MyServos.ARM, -90); }




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
      adjustDirection(-1);
      break;
    case 'arrowright':
      adjustDirection(1);
      break;
    case 'space':
      direction(0);
      break;
    case 'q':
      ARM_Up();
      break;
    case 'w':
      ARM_Mid();
      break;
    case 'e':
      ARM_Down();
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
      stop();       // release arrow key → stop motor
      break;
    case 'space':
      direction(0); // release space → re-center direction servo
      break;
    case 'q':
      ARM_Mid();    // release Q → return arm to neutral position
      break;
  }
}

const STICK_DEADZONE = 0.15;  // Prevent stick drift


CUSTOMCONTROL.processGamepadInput = function (gamepad) {

  // Handle D-PAD for input for movement
  if (gamepad.buttons[XBOX_BUTTONS.DPAD_UP].pressed) {
    moveForward();
  } else if (gamepad.buttons[XBOX_BUTTONS.DPAD_DOWN].pressed) {
    moveBackward();
  } else if (gamepad.buttons[XBOX_BUTTONS.DPAD_LEFT].pressed) {
    adjustDirection(-1);
  } else if (gamepad.buttons[XBOX_BUTTONS.DPAD_RIGHT].pressed) {
    adjustDirection(1);
  } else {
    let stickX = gamepad.axes[STICK_AXES.LEFT_X];
    let stickY = gamepad.axes[STICK_AXES.LEFT_Y];

    // Apply deadzone
    if (Math.abs(stickX) < STICK_DEADZONE) stickX = 0;
    if (Math.abs(stickY) < STICK_DEADZONE) stickY = 0;

    if (stickY !== 0 || stickX !== 0) {
      // Convert to motor speeds


      // Clamp to valid range
      const speedClamped = Math.max(-100, Math.min(100, stickX));
      const directionClamped = Math.max(-100, Math.min(100, stickY));

      setServoSpeeds([[MyServos.MOTOR, speedClamped]]);
      setServoSpeeds([[MyServos.DIRECTION, directionClamped]]);
    }
    else { stop(); }
  }

};
// ─────────────────────────────────────────────────────────────────────────────
// ─────────────────────────────────────────────────────────────────────────────

