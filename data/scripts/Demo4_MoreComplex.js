// Template 4: State Machine (Complex Behavior)
// Use this when your bot needs different modes (explore, capture, return).
// ARM servo uses ANGULAR_270 type: valid angle range is 0° – 270°.


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
const ARM = 2;

let currentState = 'idle';

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

// Step 1 — attach servos
attachServo(LEFT_WHEEL,  SERVO_TYPES.ROTATIONAL);
attachServo(RIGHT_WHEEL, SERVO_TYPES.ROTATIONAL);
attachServo(ARM,         SERVO_TYPES.ANGULAR_270);
_scriptLog('✓ Servos attached — ARM range: 0°–270°');

// Gamepad control
CUSTOMCONTROL.processGamepadInput = function(gamepad) {
  if (gamepad.buttons[XBOX_BUTTONS.A].pressed && currentState !== 'explore') {
    setState('explore');
    explore();
  } else if (gamepad.buttons[XBOX_BUTTONS.B].pressed && currentState !== 'capture') {
    setState('capture');
    capture();
  }
};