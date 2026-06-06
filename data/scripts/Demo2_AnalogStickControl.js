// Template 2: Analog Stick Control (with Deadzone)
// Use this for smooth, analog left-stick control on a diff-drive (tank) robot.
// Left stick: Y-axis = forward/backward, X-axis = steer left/right.
// NOTE: Gamepad Y-axis is inverted (push forward → Y = -1.0), corrected below.


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
const DEADZONE = 0.15;  // Prevent stick drift

function setSpeed(leftSpeed, rightSpeed) {
  setServoSpeeds([[LEFT_WHEEL, leftSpeed], [RIGHT_WHEEL, rightSpeed]]);
}

// Step 1 — attach servos
attachServo(LEFT_WHEEL,  SERVO_TYPES.ROTATIONAL);
attachServo(RIGHT_WHEEL, SERVO_TYPES.ROTATIONAL);
_scriptLog('✓ Analog stick control ready (left stick to drive)');

// Step 2 — override gamepad hook
CUSTOMCONTROL.processGamepadInput = function(gamepad) {
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