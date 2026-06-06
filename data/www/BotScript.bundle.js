'use strict';

// ── API helpers ───────────────────────────────────────────────────────────────

function getDebugLogState() {
  if (!window.__k10DebugLogState) {
    window.__k10DebugLogState = { nextId: 1 };
  }
  return window.__k10DebugLogState;
}

function nextDebugLogId() {
  const state = getDebugLogState();
  const id = state.nextId;
  state.nextId += 1;
  return id;
}

function getDebugTimestamp() {
  return new Date().toISOString();
}

function logCommonApi(message, details) {
  const logId = nextDebugLogId();
  const timestamp = getDebugTimestamp();
  if (details !== undefined) {
    console.log(`[common.js][${timestamp}][id:${logId}] ${message}`, details);
    return;
  }
  console.log(`[common.js][${timestamp}][id:${logId}] ${message}`);
}

function logCommonApiError(message, details) {
  const logId = nextDebugLogId();
  const timestamp = getDebugTimestamp();
  if (details !== undefined) {
    console.error(`[common.js][${timestamp}][id:${logId}] ${message}`, details);
    return;
  }
  console.error(`[common.js][${timestamp}][id:${logId}] ${message}`);
}

/**
 * General API call with optional JSON body.
 * @param {string} endpoint
 * @param {string} [method='GET']
 * @param {object|null} [body=null]  Sent as JSON body when provided.
 * @returns {Promise<{ok, status, data}|{ok, error}>}
 */
async function apiCall(endpoint, method = 'GET', body = null) {
  const requestId = nextDebugLogId();
  logCommonApi('apiCall() start', { request_id: requestId, endpoint, method, body });
  const options = { method };
  if (body !== null) {
    options.headers = { 'Content-Type': 'application/json' };
    options.body = JSON.stringify(body);
  }
  try {
    logCommonApi('apiCall() fetch request', { request_id: requestId, endpoint, options });
    const response = await fetch(endpoint, options);
    logCommonApi('apiCall() fetch response received', {
      request_id: requestId,
      endpoint,
      method,
      ok: response.ok,
      status: response.status
    });
    const data = await response.json();
    logCommonApi('apiCall() response json parsed', { request_id: requestId, endpoint, method, data });
    return { ok: response.ok, status: response.status, data };
  } catch (error) {
    logCommonApiError('apiCall() failed', { request_id: requestId, endpoint, method, body, error });
    return { ok: false, error: error.message };
  }
}

/**
 * GET request — convenience wrapper around apiCall.
 * @param {string} url
 */
async function apiGet(url) {
  logCommonApi('apiGet() start', { url });
  return apiCall(url, 'GET');
}

/**
 * POST request with no body — convenience wrapper around apiCall.
 * @param {string} url
 */
async function apiPost(url) {
  logCommonApi('apiPost() start', { url });
  return apiCall(url, 'POST');
}

/**
 * API call where parameters are appended as URL query string.
 * Used when the firmware endpoint reads from query params rather than JSON body.
 * @param {string} endpoint
 * @param {string} [method='GET']
 * @param {object|null} [params=null]
 */
async function apiCallParams(endpoint, method = 'GET', params = null) {
  logCommonApi('apiCallParams() start', { endpoint, method, params });
  let url = endpoint;
  if (params) url += '?' + new URLSearchParams(params).toString();
  logCommonApi('apiCallParams() resolved url', { url, method });
  return apiCall(url, method);
}

// ── Page title status badge ───────────────────────────────────────────────────

/**
 * Update the #titleStatus span shown next to the page <h1>.
 * @param {string} text  - displayed text, e.g. '[started]'
 * @param {string} color - CSS color string
 */
function setTitleStatus(text, color) {
  logCommonApi('setTitleStatus() start', { text, color });
  const el = document.getElementById('titleStatus');
  if (el) { el.textContent = text; el.style.color = color; }
}

// ── Status message (bottom statusbar #page-status) ─────────────────────────

/**
 * Briefly show a message in the bottom statusbar's left slot,
 * then clear it after 3 s.
 * @param {string}  message
 * @param {boolean} [isError=false]
 */
function showStatus(message, isError = false) {
  logCommonApi('showStatus() start', { message, isError });
  console.log(`[STATUS] ${message} ${isError ? '(error)' : ''}`);
  const el = document.getElementById('page-status');
  if (!el) return;
  el.textContent = message;
  el.style.color = isError ? '#ff6b6b' : '#4CAF50';
  clearTimeout(showStatus._t);
  showStatus._t = setTimeout(() => { el.textContent = ''; el.style.color = ''; }, 3000);
}

// ── Collapsible sections ──────────────────────────────────────────────────────

/**
 * Toggle a collapsible section between expanded and collapsed.
 * @param {string} sectionId - id of the .collapsible-body element
 * @param {string} btnId     - id of the .collapse-btn element
 */
function toggleSection(sectionId, btnId) {
  logCommonApi('toggleSection() start', { sectionId, btnId });
  const body = document.getElementById(sectionId);
  const btn  = document.getElementById(btnId);
  const collapsed = body.classList.toggle('collapsed');
  btn.classList.toggle('collapsed', collapsed);
}

// ── HTML escaping ─────────────────────────────────────────────────────────────

/**
 * Escape HTML special characters to prevent XSS.
 * @param {string} text
 * @returns {string}
 */
function escapeHtml(text) {
  logCommonApi('escapeHtml() start', { text });
  const div = document.createElement('div');
  div.textContent = text;
  return div.innerHTML;
}
'use strict';

// ═════════════════════════════════════════════════════════════════════════════
// ⚠️  ARCHITECTURE: TWO SERVO CONTROL MODES
// ═════════════════════════════════════════════════════════════════════════════
//
// BotScript.js is the infrastructure layer that handles WebSocket communication,
// heartbeat, master registration, and connection management.
//
// 📌 IMPORTANT: Two different servo control modes exist:
//
// MODE 1: REQUEST-RESPONSE (BotScript.js - Confirmed Control Functions)
//   Functions: requestSetServoAngles(pairs[]), requestSetServoSpeeds(pairs[]), attachServos(), detachServos()
//   Behavior: Waits for response, shows status feedback, returns response code
//   Use case: Sequential operations, scripted setup, any flow needing confirmation
//
// MODE 2: FIRE-AND-FORGET (BotScriptActions.js - Direct Control)
//   Functions: attachServo(channel, type), setServoAngle(channel, angle),
//             setServoSpeeds(pairs[])
//   Behavior: Returns immediately, no response wait, optimized for real-time
//   Use case: Gamepad/keyboard control, script automation
//
// ✅ USER-FACING FUNCTIONS (available after registerMaster):
//   - registerMaster(token) — Register as master controller [CALL THIS FIRST]
//   - unregisterMaster() — Unregister from master
//   - rebootBot() — Emergency stop + reboot the board
//   - setBotName(name) — Set bot display name
//   - getBattery() — Query battery level
//   - setScreen(screenIndex), nextScreen(), previousScreen() — Screen navigation
//   - startHeartbeat() / stopHeartbeat() — Connection keepalive (auto-started)
//   - startPing() / stopPing() — Connection latency monitoring
//
// ⚠️  PRIVATE FUNCTIONS (don't call directly):
//   - sendWSPacket(), processWSResponse(), sendWSFireAndForget()
//   - Other internal helpers
//
// EXECUTION FLOW:
// 1. Page loads, BotScript.js + BotScriptActions.js initialize
// 2. User enters token, clicks "Register as Master"
// 3. Heartbeat auto-starts (40ms keepalive)
// 4. User can now call servo functions:
//    - Use BotScriptActions.js fire-and-forget for real-time control
//    - Use BotScript.js request-response for UI feedback
//
// ═════════════════════════════════════════════════════════════════════════════

// ── WebSocket Configuration ───────────────────────────────────────────────────

// Global connection parameters (set by getBotControl or HTML form)
let gBotIp = '';      // e.g., "192.168.4.1"
let gBotPort = '';    // e.g., "80"

let ws = null;  // WebSocket connection to bridge
let wsConnected = false;
let wsQueue = [];  // Queue for messages sent while connecting
let wsConnectPromise = null;
let wsReconnectTimer = null;
let wsManualClose = false;
let pendingResponse = null;

let heartbeatInterval = null;
let heartbeatSendTime = 0;           // performance.now() of last heartbeat send
let heartbeatRTTHistory = [];        // rolling window of round-trip times (ms)
const MAX_HEARTBEAT_RTT_HISTORY = 10;
let isMasterRegistered = false;
let pingInterval = null;
let pingHistory = [];
let pingSequence = 0;
const MAX_PING_HISTORY = 100;

const WS_PORT = 81;
const WS_TIMEOUT = 500;
const HEARTBEAT_INTERVAL_MS = 40; // Send every 40ms (well under 50ms deadline)

// BotProto response codes — must match BotProto constants in BotMessageHandler.h
const BOT_RESP = {
  OK:               0x00, // BotProto::resp_ok
  INVALID_PARAMS:   0x01, // BotProto::resp_invalid_params
  INVALID_VALUES:   0x02, // BotProto::resp_invalid_values
  OPERATION_FAILED: 0x03, // BotProto::resp_operation_failed
  NOT_STARTED:      0x04, // BotProto::resp_not_started
  UNKNOWN_SERVICE:  0x05, // BotProto::resp_unknown_service
  UNKNOWN_CMD:      0x06, // BotProto::resp_unknown_cmd
  NOT_MASTER:       0x07  // BotProto::resp_not_master
};

// Human-readable names for BOT_RESP codes (used in debug displays)
const BOT_RESP_NAMES = {
  [BOT_RESP.OK]:               'OK',
  [BOT_RESP.INVALID_PARAMS]:   'INVALID_PARAMS',
  [BOT_RESP.INVALID_VALUES]:   'INVALID_VALUES',
  [BOT_RESP.OPERATION_FAILED]: 'OPERATION_FAILED',
  [BOT_RESP.NOT_STARTED]:      'NOT_STARTED',
  [BOT_RESP.UNKNOWN_SERVICE]:  'UNKNOWN_SERVICE',
  [BOT_RESP.UNKNOWN_CMD]:      'UNKNOWN_CMD',
  [BOT_RESP.NOT_MASTER]:       'NOT_MASTER'
};

// WS Action codes — AmakerBotService (service_id 0x04)
const WS_ACTION = {
  MASTER_REGISTER:   0x41, // AmakerBotService CMD_REGISTER
  MASTER_UNREGISTER: 0x42, // AmakerBotService CMD_UNREGISTER
  HEARTBEAT:         0x43, // AmakerBotService CMD_HEARTBEAT
  PING:              0x44, // AmakerBotService CMD_PING
  REBOOT:            0x4A  // AmakerBotService CMD_REBOOT
};

// MotorServo action bytes — MotorServoService (service_id 0x02)
const MOTOR_SERVO_ACTION = {
  SET_MOTORS_SPEED:       0x21, // MotorServoConsts::CMD_SET_MOTORS_SPEED
  SET_SERVO_TYPE:         0x22, // MotorServoConsts::CMD_SET_SERVO_TYPE
  SET_SERVOS_SPEED:       0x23, // MotorServoConsts::CMD_SET_SERVOS_SPEED
  SET_SERVOS_ANGLE:       0x24, // MotorServoConsts::CMD_SET_SERVOS_ANGLE
  INCREMENT_SERVOS_ANGLE: 0x25, // MotorServoConsts::CMD_INCREMENT_SERVOS_ANGLE
  GET_MOTORS_SPEED:       0x26, // MotorServoConsts::CMD_GET_MOTORS_SPEED
  GET_SERVOS_ANGLE:       0x27, // MotorServoConsts::CMD_GET_SERVOS_ANGLE
  STOP_ALL_MOTORS:        0x28, // MotorServoConsts::CMD_STOP_ALL_MOTORS
  GET_BATTERY:            0x29, // MotorServoConsts::CMD_GET_BATTERY
  SET_SERVO270_ANGLE:     0x2A  // MotorServoConsts::CMD_SET_SERVO270_ANGLE
};

// DFR1216 action bytes — DFR1216Board (service_id 0x03)
const DFR1216_ACTION = {
  SET_LED_COLOR:    0x31, // DFR1216Consts::udp_action_set_led_color
  TURN_OFF_LED:     0x32, // DFR1216Consts::udp_action_turn_off_led
  TURN_OFF_ALL:     0x33, // DFR1216Consts::udp_action_turn_off_all_leds
  GET_LED_STATUS:   0x34, // DFR1216Consts::udp_action_get_led_status
  GET_BATTERY:      0x35  // DFR1216Consts::udp_action_get_battery
};


// ── Page Initialization ───────────────────────────────────────────────────────

document.addEventListener('DOMContentLoaded', () => {
  // Auto-populate bot IP from current location and set global
  const currentHost = window.location.hostname;
  gBotIp = currentHost;
  const botIpField = document.getElementById('botIp');
  if (botIpField) {
    botIpField.value = currentHost;
  }
  
  // Set default port in global and UI
  gBotPort = '81';
  const botPortField = document.getElementById('botPort');
  if (botPortField && !botPortField.value) {
    botPortField.value = '81';
  }
  
  // Setup angle sliders
  for (let i = 0; i < 4; i++) {
    const slider = document.getElementById(`angle${i}`);
    const valueSpan = document.getElementById(`angle${i}-value`);
    if (slider && valueSpan) {
      slider.addEventListener('input', (e) => {
        valueSpan.textContent = e.target.value + '°';
      });
    }
  }
  
  // Setup speed sliders
  for (let i = 4; i < 8; i++) {
    const slider = document.getElementById(`speed${i}`);
    const valueSpan = document.getElementById(`speed${i}-value`);
    if (slider && valueSpan) {
      slider.addEventListener('input', (e) => {
        valueSpan.textContent = e.target.value;
      });
    }
  }
  
  updateUIState();
  showStatus('WS demo loaded. Enter master token to register.', false);
  
  // Initialize ping graph
  initPingGraph();
});

window.addEventListener('beforeunload', cleanupRealtimeConnections);
window.addEventListener('pagehide', cleanupRealtimeConnections);

// ── WebSocket Bridge Management ───────────────────────────────────────────────

/**
 * Cleanly stop timers and WebSocket activity when leaving the page.
 */
function cleanupRealtimeConnections() {
  stopHeartbeat();
  stopPing();
  closeWebSocket();
}

/**
 * Reject the currently pending WS response, if any.
 *
 * @param {Error} error - Reason for rejection.
 */
function rejectPendingResponse(error) {
  if (!pendingResponse) {
    return;
  }

  clearTimeout(pendingResponse.timeoutId);
  pendingResponse.reject(error);
  pendingResponse = null;
}

/**
 * Schedule a reconnect only when the page still needs a live master session.
 */
function scheduleReconnect() {
  if (wsReconnectTimer || wsManualClose || !isMasterRegistered) {
    return;
  }

  wsReconnectTimer = setTimeout(() => {
    wsReconnectTimer = null;
    initializeWebSocket().catch((error) => {
      console.error('[WS] Reconnect failed:', error);
    });
  }, 3000);
}

/**
 * Initialize WebSocket connection to the bridge at /ws
 * Uses global gBotIp and gBotPort variables
 */
function initializeWebSocket() {
  if (wsConnected && ws && ws.readyState === WebSocket.OPEN) {
    return Promise.resolve(ws);
  }

  if (wsConnectPromise) {
    return wsConnectPromise;
  }

  if (wsReconnectTimer) {
    clearTimeout(wsReconnectTimer);
    wsReconnectTimer = null;
  }

  if (!gBotIp) {
    return Promise.reject(new Error('Bot IP not configured'));
  }

  const portStr = gBotPort || '81';
  const wsUrl = `ws://${gBotIp}:${portStr}/ws`;
  wsManualClose = false;
  console.log('[WS] initializeWebSocket ' + wsUrl);
  console.log(`[WS] Connecting to ${wsUrl}`);

  wsConnectPromise = new Promise((resolve, reject) => {
    try {
      const socket = new WebSocket(wsUrl);
      ws = socket;
      ws.binaryType = 'arraybuffer';

      const connectionTimeout = setTimeout(() => {
        if (socket.readyState === WebSocket.CONNECTING) {
          console.error('[WS] Connection timeout - closing socket');
          socket.close();
        }
      }, 5000);

      socket.onopen = () => {
        clearTimeout(connectionTimeout);

        if (ws !== socket) {
          socket.close();
          return;
        }

        wsConnected = true;
        wsConnectPromise = null;
        console.log(`[WS] Connected to ${gBotIp}:${gBotPort}`);
        showStatus(`WebSocket connected (${gBotIp}:${gBotPort})`, false);
        updateUIState();
        resolve(socket);
      };

      socket.onmessage = (event) => {
        if (ws !== socket) {
          return;
        }

        const response = new Uint8Array(event.data);
        console.log(`[WS RX] ${Array.from(response).map(b => b.toString(16).padStart(2, '0')).join('')}`);
        processWSResponse(response);
      };

      socket.onerror = (error) => {
        clearTimeout(connectionTimeout);

        if (ws !== socket) {
          return;
        }

        wsConnected = false;
        console.error('[WS] Error:', error);
        console.error('[WS] Connection failed. URL:', wsUrl);
        console.error('[WS] Bot IP:', gBotIp);
        console.error('[WS] Port:', gBotPort);
        console.error('[WS] ReadyState:', socket.readyState);
        showStatus('WebSocket bridge error: Check bot IP and firewall', true);
        updateUIState();
      };

      socket.onclose = () => {
        clearTimeout(connectionTimeout);

        if (ws !== socket) {
          return;
        }

        ws = null;
        wsConnected = false;
        wsConnectPromise = null;
        rejectPendingResponse(new Error('WebSocket bridge disconnected'));
        console.log('[WS] Disconnected');
        showStatus('WebSocket bridge disconnected', true);
        updateUIState();
        scheduleReconnect();
      };
    } catch (error) {
      wsConnectPromise = null;
      console.error('[WS] Failed to create WebSocket:', error);
      showStatus('Failed to connect to WebSocket bridge', true);
      reject(error);
    }
  });

  return wsConnectPromise;
}

/**
 * Verify WebSocket bridge is properly configured
 * Uses global gBotIp variable
 */
function ensureWebSocketBridge() {
  if (!gBotIp) {
    console.error('Bot IP address not configured');
    showStatus('Please configure bot IP address', true);
    return false;
  }
  
  // WebSocket is the bridge - check if connected
  if (!wsConnected) {
    console.error('WebSocket bridge not connected');
    showStatus('WebSocket bridge not connected', true);
    return false;
  }
  
  return true;
}

/**
 * Close WebSocket connection
 */
function closeWebSocket() {
  wsManualClose = true;

  if (wsReconnectTimer) {
    clearTimeout(wsReconnectTimer);
    wsReconnectTimer = null;
  }

  rejectPendingResponse(new Error('WebSocket bridge closed'));

  if (ws) {
    ws.close();
    ws = null;
    wsConnected = false;
    wsQueue = [];
    console.log('[WS] Closed');
  }

  wsConnectPromise = null;
  updateUIState();
}

// ── Bot Control Interface ─────────────────────────────────────────────────────

/**
 * Unified function to connect to WebSocket and register as master
 * 
 * @param {string} ip - Bot IP address (e.g., "192.168.4.1")
 * @param {string|number} port - WebSocket port (e.g., "80" or 80)
 * @param {string} masterToken - 5-character master token (e.g., "abc12")
 * @returns {Promise<boolean>} - true if successfully connected and registered, false otherwise
 * 
 * @example
 * // Simple usage - await connection and registration
 * const success = await getBotControl("192.168.4.1", "80", "abc12");
 * if (success) {
 *   _scriptLog('✓ Connected and registered as master');
 * } else {
 *   _scriptLog('❌ Failed to connect or register');
 * }
 * 
 * @example
 * // With error handling
 * try {
 *   const connected = await getBotControl("192.168.4.1", 80, "abc12");
 *   if (connected) {
 *     // You can now use servo functions
 *     attachServo(0, SERVO_TYPES.ROTATIONAL);
 *     setServoSpeeds([[0, 100]]);
 *   }
 * } catch (error) {
 *   console.error('Connection failed:', error);
 * }
 */
async function getBotControl(ip, port, masterToken) {
  try {
    // Validate inputs
    if (!ip || typeof ip !== 'string') {
      throw new Error('Bot IP must be a non-empty string');
    }
    if (!masterToken || typeof masterToken !== 'string' || masterToken.length !== 5) {
      throw new Error('Master token must be a 5-character string');
    }

    // Normalize port to string if it's a number
    const portStr = String(port);
    if (!portStr || portStr === '0' || portStr === '') {
      throw new Error('Port must be a valid port number');
    }

    console.log(`[getBotControl] Connecting to bot at ${ip}:${portStr} with token: ${masterToken}`);
    
    // Step 1: Set global variables (single source of truth)
    gBotIp = ip;
    gBotPort = portStr;
    console.log(`[getBotControl] Set global: gBotIp=${gBotIp}, gBotPort=${gBotPort}`);
    
    // Step 1b: Also update HTML form fields for UI consistency
    const botIpField = document.getElementById('botIp');
    const botPortField = document.getElementById('botPort');
    
    if (botIpField) {
      botIpField.value = ip;
    }
    
    if (botPortField) {
      botPortField.value = portStr;
    }
    
    // Step 2: Connect to WebSocket
    console.log('[getBotControl] Step 2: Initializing WebSocket...');
    showStatus('Connecting to bot...', false);
    
    await initializeWebSocket();
    
    if (!wsConnected) {
      throw new Error('WebSocket connection failed - bot may be offline or unreachable');
    }
    
    console.log('[getBotControl] WebSocket connected successfully');
    
    // Step 3: Register as master
    console.log('[getBotControl] Step 3: Registering as master...');
    showStatus('Registering as master...', false);
    
    await registerMaster(masterToken);
    
    if (!isMasterRegistered) {
      throw new Error('Master registration failed - invalid token or bot rejected registration');
    }
    
    console.log('[getBotControl] Successfully registered as master');
    showStatus(`✓ Connected and registered as master (${gBotIp}:${gBotPort})`, false);
    
    return true;

  } catch (error) {
    console.error('[getBotControl] Error:', error);
    showStatus(`❌ Connection failed: ${error.message}`, true);
    updateLastResponse(`ERROR: ${error.message}`);
    return false;
  }
}

// ── Master Registration ───────────────────────────────────────────────────────

/**
 * Register this client as master controller
 */
async function registerMaster(token) {
    
  if (!token || token.length !== 5) {
    showStatus('Please enter a valid 5-character token', true);
    console.error('Invalid token:', token);
    return;
  }
  
  // Initialize WebSocket if not already connected
  if (!wsConnected) {
    showStatus('Initializing WebSocket bridge...', false);
    console.info('Initializing WebSocket bridge...');
    await initializeWebSocket();
  }
  

  
  try {
    showStatus('Registering as master...', false);

    // Build WS packet: 0x41 + token bytes
    const packet = new Uint8Array(1 + token.length);
    packet[0] = WS_ACTION.MASTER_REGISTER;
    for (let i = 0; i < token.length; i++) {
      packet[i + 1] = token.charCodeAt(i);
    }
    
    // Send WS packet (simulated via HTTP proxy)
    const response = await sendWSPacket(packet);
    
    // Parse response: [action][status] — always 2 bytes
    if (response && response.length >= 2) {
      const statusByte = response[1];
      handleMasterRegistrationResponse(statusByte, token);
    } else {
      showStatus('Invalid response from bot', true);
      updateLastResponse('Invalid response');
    }
    
  } catch (error) {
    console.error('Master registration failed:', error);
    showStatus('Registration failed: ' + error.message, true);
    updateLastResponse('Error: ' + error.message);
  }
}

/**
 * Handle master registration response
 */
function handleMasterRegistrationResponse(statusByte, token) {
  const statusName = BOT_RESP_NAMES[statusByte] || `UNKNOWN(0x${statusByte.toString(16)})`;
  updateLastResponse(`MASTER_REGISTER: ${statusName}`);
  
  if (statusByte === BOT_RESP.OK) {
    isMasterRegistered = true;
    showStatus(`✓ Registered as master with token ${token}`, false);
    startHeartbeat();
    updateUIState();
  } else if (statusByte === BOT_RESP.OPERATION_FAILED) {
    showStatus('Registration ignored (master already registered)', true);
  } else if (statusByte === BOT_RESP.NOT_MASTER) {
    showStatus('Registration denied (invalid token)', true);
  } else {
    showStatus(`Registration failed: ${statusName}`, true);
  }
}

/**
 * Unregister as master controller
 */
async function unregisterMaster() {
  
  try {
    showStatus('Unregistering master...', false);
    
    // Build WS packet: 0x42
    const packet = new Uint8Array([WS_ACTION.MASTER_UNREGISTER]);
    
    // Send WS packet
    const response = await sendWSPacket(packet);
    
    // Parse response: 0x42 + status byte
    if (response && response.length >= 2) {
      const statusByte = response[1];
      handleMasterUnregistrationResponse(statusByte);
    } else {
      showStatus('Invalid response from bot', true);
      updateLastResponse('Invalid response');
    }
    
  } catch (error) {
    console.error('Master unregistration failed:', error);
    showStatus('Unregistration failed: ' + error.message, true);
    updateLastResponse('Error: ' + error.message);
  }
}

/**
 * Handle master unregistration response
 */
function handleMasterUnregistrationResponse(statusByte) {
  const statusName = BOT_RESP_NAMES[statusByte] || `UNKNOWN(0x${statusByte.toString(16)})`;
  updateLastResponse(`MASTER_UNREGISTER: ${statusName}`);
  
  if (statusByte === BOT_RESP.OK) {
    isMasterRegistered = false;
    showStatus('✓ Successfully unregistered as master', false);
    stopHeartbeat();
    closeWebSocket();
    updateUIState();
  } else if (statusByte === BOT_RESP.NOT_MASTER) {
    showStatus('Unregistration denied (not the registered master)', true);
  } else {
    showStatus(`Unregistration failed: ${statusName}`, true);
  }
}

/**
 * Emergency stop + reboot the bot.
 * Uses fire-and-forget because the board restarts immediately.
 */
async function rebootBot() {
  if (!isMasterRegistered) {
    showStatus('Must be registered as master to reboot the bot', true);
    return;
  }

  if (!confirm('Emergency reboot the K10 now? Motors will be stopped and the connection will drop.')) {
    return;
  }

  try {
    if (!wsConnected) {
      await initializeWebSocket();
    }

    if (!wsConnected || !ws || ws.readyState !== WebSocket.OPEN) {
      throw new Error('WebSocket bridge not connected');
    }

    stopHeartbeat();
    sendWSFireAndForget(new Uint8Array([WS_ACTION.REBOOT]));
    updateLastResponse('REBOOT: SENT');
    showStatus('🛑 Emergency reboot command sent', false);
    _scriptLog('🛑 Emergency reboot command sent to board', false);

    setTimeout(() => {
      isMasterRegistered = false;
      closeWebSocket();
      updateUIState();
    }, 250);
  } catch (error) {
    console.error('Reboot failed:', error);
    showStatus('Failed to send reboot: ' + error.message, true);
    updateLastResponse('REBOOT: ERROR');
  }
}

// ── Bot Name Management ───────────────────────────────────────────────────────

/**
 * Set the bot name via WS
 */
async function setBotName() {
  const botName = document.getElementById('botName').value.trim();
  
  if (!botName) {
    showStatus('Please enter a bot name', true);
    return;
  }
  
  if (!isMasterRegistered) {
    showStatus('Must be registered as master to set bot name', true);
    return;
  }
  
  try {
    showStatus('Setting bot name...', false);
    
    // Build WS packet: "AMAKERBOT:setname:<name>"
    const message = `AMAKERBOT:setname:${botName}`;
    const packet = new TextEncoder().encode(message);
    
    // Send WS packet
    const response = await sendWSPacket(packet);
    
    // Parse text response
    if (response && response.length > 0) {
      const responseText = new TextDecoder().decode(response);
      handleBotNameResponse(responseText, botName);
    } else {
      showStatus('No response from bot', true);
      updateLastResponse('No response');
    }
    
  } catch (error) {
    console.error('Set bot name failed:', error);
    showStatus('Failed to set bot name: ' + error.message, true);
    updateLastResponse('Error: ' + error.message);
  }
}

/**
 * Handle bot name response
 */
function handleBotNameResponse(responseText, botName) {
  updateLastResponse(`SET_NAME: ${responseText}`);
  
  try {
    const response = JSON.parse(responseText);
    if (response.result === 'ok') {
      showStatus(`✓ Bot name set to "${botName}"`, false);
    } else {
      showStatus(`Failed: ${response.message || 'unknown error'}`, true);
    }
  } catch (e) {
    showStatus('Invalid response format', true);
  }
}

// ── Board Info ───────────────────────────────────────────────────────────────

/**
 * Request the battery level from the board.
 * Response: [DFR1216_ACTION.GET_BATTERY][resp_ok][level:u8  0-100]
 */
async function getBattery() {
  if (!wsConnected) {
    showStatus('WebSocket not connected', true);
    return;
  }

  try {
    showStatus('Reading battery level...', false);
    const packet   = new Uint8Array([DFR1216_ACTION.GET_BATTERY]);
    const response = await sendWSPacket(packet);

    if (response && response.length >= 3 && response[1] === BOT_RESP.OK) {
      const level = response[2];
      showStatus(`🔋 Battery: ${level}%`, false);
      updateLastResponse(`GET_BATTERY: ${level}%`);
      const elem = document.getElementById('batteryLevel');
      if (elem) elem.textContent = `${level}%`;
    } else {
      const respName = response && response.length >= 2
        ? (BOT_RESP_NAMES[response[1]] || `0x${response[1].toString(16)}`)
        : 'no response';
      showStatus(`Battery read failed: ${respName}`, true);
      updateLastResponse(`GET_BATTERY: ${respName}`);
    }
  } catch (error) {
    console.error('getBattery failed:', error);
    showStatus('Battery read error: ' + error.message, true);
    updateLastResponse('Error: ' + error.message);
  }
}

// ── Servo Attachment Management ───────────────────────────────────────────────

/**
 * Select all servo channels
 */
function selectAllServos() {
  for (let i = 0; i < 8; i++) {
    document.getElementById(`servo${i}`).checked = true;
  }
}

/**
 * Deselect all servo channels
 */
function deselectAllServos() {
  for (let i = 0; i < 8; i++) {
    document.getElementById(`servo${i}`).checked = false;
  }
}

/**
 * Get selected servo channels as bitmask
 * @returns {number} Bitmask where bit N = servo channel N
 */
function getServoMask() {
  let mask = 0;
  for (let i = 0; i < 8; i++) {
    if (document.getElementById(`servo${i}`).checked) {
      mask |= (1 << i);
    }
  }
  return mask;
}
/**
 * Attach servos with selected type
 */
async function attachServos() {
  if (!isMasterRegistered) {
    showStatus('Must be registered as master to attach servos', true);
    return;
  }
  
  const mask = getServoMask();
  if (mask === 0) {
    showStatus('Please select at least one servo channel', true);
    return;
  }
  
  const type = parseInt(document.getElementById('servoType').value);
  
  try {
    showStatus('Attaching servos...', false);
    
    // Build WS packet: SET_SERVO_TYPE [mask] [type]
    const packet = new Uint8Array([MOTOR_SERVO_ACTION.SET_SERVO_TYPE, mask, type]);
    
    // Send WS packet
    const response = await sendWSPacket(packet);
    
    // Parse response: 0x22 + resp_code
    if (response && response.length >= 2) {
      const respCode = response[1];
      handleServoAttachResponse(respCode, mask, type);
    } else {
      showStatus('Invalid response from bot', true);
      updateLastResponse('Invalid response');
    }
    
  } catch (error) {
    console.error('Attach servos failed:', error);
    showStatus('Failed to attach servos: ' + error.message, true);
    updateLastResponse('Error: ' + error.message);
  }
}

async function attachServo(channel, type) {
  if (!isMasterRegistered) {
    showStatus('Must be registered as master to attach servos', true);
    return;
  }
  
  const mask = (1 << channel);
  if (mask === 0) {
    showStatus('Please select at least one servo channel', true);
    return;
  }
  
 
  try {
    showStatus('Attaching servos...', false);
    
    // Build WS packet: SET_SERVO_TYPE [mask] [type]
    const packet = new Uint8Array([MOTOR_SERVO_ACTION.SET_SERVO_TYPE, mask, type]);
    
    // Send WS packet
    const response = await sendWSPacket(packet);
    
    // Parse response: 0x22 + resp_code
    if (response && response.length >= 2) {
      const respCode = response[1];
      handleServoAttachResponse(respCode, mask, type);
    } else {
      showStatus('Invalid response from bot', true);
      updateLastResponse('Invalid response');
    }
    
  } catch (error) {
    console.error('Attach servos failed:', error);
    showStatus('Failed to attach servos: ' + error.message, true);
    updateLastResponse('Error: ' + error.message);
  }
}

/**
 * Detach selected servos (attach with type=0)
 */
async function detachServos() {
  if (!isMasterRegistered) {
    showStatus('Must be registered as master to detach servos', true);
    return;
  }
  
  const mask = getServoMask();
  if (mask === 0) {
    showStatus('Please select at least one servo channel', true);
    return;
  }
  
  try {
    showStatus('Detaching servos...', false);
    
    // Build WS packet: SET_SERVO_TYPE [mask] [type=0]
    const packet = new Uint8Array([MOTOR_SERVO_ACTION.SET_SERVO_TYPE, mask, 0]);
    
    // Send WS packet
    const response = await sendWSPacket(packet);
    
    // Parse response: 0x22 + resp_code
    if (response && response.length >= 2) {
      const respCode = response[1];
      handleServoAttachResponse(respCode, mask, 0);
    } else {
      showStatus('Invalid response from bot', true);
      updateLastResponse('Invalid response');
    }
    
  } catch (error) {
    console.error('Detach servos failed:', error);
    showStatus('Failed to detach servos: ' + error.message, true);
    updateLastResponse('Error: ' + error.message);
  }
}

/**
 * Handle servo attach/detach response
 */
function handleServoAttachResponse(respCode, mask, type) {
  const typeNames = ['Detached', '270° Angular', 'Continuous'];
  const typeName = typeNames[type] || `Type ${type}`;
  
  const respName = BOT_RESP_NAMES[respCode] || `0x${respCode.toString(16)}`;
  updateLastResponse(`ATTACH_SERVO (mask=0x${mask.toString(16)}, type=${type}): ${respName}`);
  
  if (respCode === BOT_RESP.OK) {
    const channels = [];
    for (let i = 0; i < 8; i++) {
      if (mask & (1 << i)) channels.push(i);
    }
    const action = type === 0 ? 'detached' : `attached as ${typeName}`;
    showStatus(`✓ Servo channels ${channels.join(', ')} ${action}`, false);
  } else if (respCode === BOT_RESP.NOT_MASTER) {
    showStatus('Not authorized - not registered as master', true);
  } else {
    showStatus(`Failed: ${respName}`, true);
  }
}
// ── Servo Control ─────────────────────────────────────────────────────────────

/**
 * Set servo angles for angular servos (0x24) with request-response.
 * Sends the command and waits for bot confirmation.
 * Use setServoAngle() from BotScriptActions.js for fire-and-forget direct control.
 *
 * @param {Array<[number, number]>} pairs - Array of [channel, angle] pairs
 *   channel: servo channel number (0-7)
 *   angle: angle in degrees (0 to 270 depending on type)
 * @returns {Promise<number|null>} BOT_RESP code on success, null on error
 * @example
 *   const resp = await requestSetServoAngles([[2, 90], [3, 135]]);
 *   if (resp === BOT_RESP.OK) _scriptLog('✓ Angles set');
 */
async function requestSetServoAngles(pairs) {
  if (!isMasterRegistered) {
    showStatus('Must be registered as master to control servos', true);
    return null;
  }

  if (!Array.isArray(pairs) || pairs.length === 0) {
    showStatus('No servo pairs provided', true);
    return null;
  }

  try {
    showStatus('Setting servo angles...', false);

    // Protocol: SET_SERVOS_ANGLE [servo_mask:u8] [angle_hi:u8] [angle_lo:u8]
    // Group channels that share the same angle into one packet.
    const angleMap = new Map();
    for (const [channel, angle] of pairs) {
      const rounded = Math.round(angle);
      angleMap.set(rounded, (angleMap.get(rounded) || 0) | (1 << channel));
    }

    let lastRespCode = null;

    for (const [angle, mask] of angleMap) {
      const angleHi = (angle >> 8) & 0xFF;
      const angleLo = angle & 0xFF;
      const packet = new Uint8Array([MOTOR_SERVO_ACTION.SET_SERVOS_ANGLE, mask & 0xFF, angleHi, angleLo]);
      const response = await sendWSPacket(packet);

      if (response && response.length >= 2) {
        lastRespCode = response[1];
        handleServoAngleResponse(lastRespCode);
        if (lastRespCode !== BOT_RESP.OK) {
          return lastRespCode;
        }
      } else {
        showStatus('Invalid response from bot', true);
        updateLastResponse('Invalid response');
        return null;
      }
    }

    return lastRespCode;

  } catch (error) {
    console.error('Set servo angles failed:', error);
    showStatus('Failed to set angles: ' + error.message, true);
    updateLastResponse('Error: ' + error.message);
    return null;
  }
}

/**
 * Handle servo angle response
 */
function handleServoAngleResponse(respCode) {
  const respName = BOT_RESP_NAMES[respCode] || `0x${respCode.toString(16)}`;
  updateLastResponse(`SET_SERVO_ANGLE: ${respName}`);
  
  if (respCode === BOT_RESP.OK) {
    showStatus('✓ Servo angles set', false);
  } else if (respCode === BOT_RESP.OPERATION_FAILED) {
    showStatus('Operation failed (check servo types and angle ranges)', true);
  } else if (respCode === BOT_RESP.NOT_MASTER) {
    showStatus('Not authorized - not registered as master', true);
  } else {
    showStatus(`Failed: ${respName}`, true);
  }
}

/**
 * Center all angle servos to 0°
 */
function centerAllAngles() {
  for (let i = 0; i < 4; i++) {
    const slider = document.getElementById(`angle${i}`);
    const valueSpan = document.getElementById(`angle${i}-value`);
    if (slider && valueSpan) {
      slider.value = 0;
      valueSpan.textContent = '0°';
    }
    const checkbox = document.getElementById(`angle${i}-enable`);
    if (checkbox) checkbox.checked = true;
  }
  showStatus('All angles centered to 0°', false);
}

/**
 * Set servo speeds for rotational servos (0x23) with request-response.
 * Sends the command and waits for bot confirmation.
 * Use setServoSpeeds(pairs[]) from BotScriptActions.js for fire-and-forget direct control.
 *
 * @param {Array<[number, number]>} pairs - Array of [channel, speed] pairs
 *   channel: servo channel number (0-7)
 *   speed: speed value (-100 to +100)
 * @returns {Promise<number|null>} BOT_RESP code on success, null on error
 * @example
 *   const resp = await requestSetServoSpeeds([[0, 100], [1, -50]]);
 *   if (resp === BOT_RESP.OK) _scriptLog('✓ Speeds set');
 */
async function requestSetServoSpeeds(pairs) {
  if (!isMasterRegistered) {
    showStatus('Must be registered as master to control servos', true);
    return null;
  }

  if (!Array.isArray(pairs) || pairs.length === 0) {
    showStatus('No servo pairs provided', true);
    return null;
  }

  try {
    showStatus('Setting servo speeds...', false);

    // Protocol: SET_SERVOS_SPEED [servo_mask:u8] [speed:i8]
    // Group channels that share the same speed into one packet.
    const speedMap = new Map();
    for (const [channel, speed] of pairs) {
      speedMap.set(speed, (speedMap.get(speed) || 0) | (1 << channel));
    }

    let lastRespCode = null;

    for (const [speed, mask] of speedMap) {
      const packet = new Uint8Array([MOTOR_SERVO_ACTION.SET_SERVOS_SPEED, mask & 0xFF, speed & 0xFF]);
      const response = await sendWSPacket(packet);

      if (response && response.length >= 2) {
        lastRespCode = response[1];
        handleServoSpeedResponse(lastRespCode, mask);
        if (lastRespCode !== BOT_RESP.OK) {
          return lastRespCode;
        }
      } else {
        showStatus('Invalid response from bot', true);
        updateLastResponse('Invalid response');
        return null;
      }
    }

    return lastRespCode;

  } catch (error) {
    console.error('Set servo speeds failed:', error);
    showStatus('Failed to set speeds: ' + error.message, true);
    updateLastResponse('Error: ' + error.message);
    return null;
  }
}

/**
 * Handle servo speed response
 */
function handleServoSpeedResponse(respCode, mask) {
  const respName = BOT_RESP_NAMES[respCode] || `0x${respCode.toString(16)}`;
  updateLastResponse(`SET_SERVO_SPEED (mask=0x${mask.toString(16)}): ${respName}`);
  
  if (respCode === BOT_RESP.OK) {
    showStatus('✓ Servo speeds set', false);
  } else if (respCode === BOT_RESP.OPERATION_FAILED) {
    showStatus('Operation failed (check servo types are rotational)', true);
  } else if (respCode === BOT_RESP.NOT_MASTER) {
    showStatus('Not authorized - not registered as master', true);
  } else {
    showStatus(`Failed: ${respName}`, true);
  }
}

/**
 * Stop all servos (0x23)
 */
async function stopAllServos() {
  if (!isMasterRegistered) {
    showStatus('Must be registered as master to stop servos', true);
    return;
  }
  
  try {
    showStatus('Stopping all servos...', false);
    
    // Build packet: STOP_ALL_MOTORS (no parameters)
    const packet = new Uint8Array([MOTOR_SERVO_ACTION.STOP_ALL_MOTORS]);
    
    // Send WS packet
    const response = await sendWSPacket(packet);
    
    // Parse response: STOP_ALL_MOTORS + resp_code
    if (response && response.length >= 2) {
      const respCode = response[1];
      if (respCode === BOT_RESP.OK) {
        showStatus('✓ All servos stopped', false);
        updateLastResponse('STOP_SERVOS: OK');
        
        // Reset speed sliders to 0
        for (let i = 4; i < 8; i++) {
          const slider = document.getElementById(`speed${i}`);
          const valueSpan = document.getElementById(`speed${i}-value`);
          if (slider && valueSpan) {
            slider.value = 0;
            valueSpan.textContent = '0';
          }
        }
      } else {
        showStatus(`Stop failed: response code 0x${respCode.toString(16)}`, true);
        updateLastResponse(`STOP_SERVOS: 0x${respCode.toString(16)}`);
      }
    } else {
      showStatus('Invalid response from bot', true);
      updateLastResponse('Invalid response');
    }
    
  } catch (error) {
    console.error('Stop servos failed:', error);
    showStatus('Failed to stop servos: ' + error.message, true);
    updateLastResponse('Error: ' + error.message);
  }
}

// ── UI Control ────────────────────────────────────────────────────────────────

// UI Service action bytes (service_id 0x06)
const UI_ACTION = {
  NEXT_SCREEN: 0x61,  // Advance to next screen
  PREV_SCREEN: 0x62,  // Go back to previous screen
  SET_SCREEN:  0x63   // Set screen to specific index
};

/**
 * Advance to the next screen (wraps from last screen back to first)
 */
async function nextScreen() {
  try {
    showStatus('Going to next screen...', false);
    
    // Build packet: NEXT_SCREEN (no payload)
    const packet = new Uint8Array([UI_ACTION.NEXT_SCREEN]);
    
    // Send WS packet
    const response = await sendWSPacket(packet);
    
    // Parse response: 0x51 + resp_code
    if (response && response.length >= 2) {
      const respCode = response[1];
      handleUIScreenResponse(respCode, 'NEXT_SCREEN');
    } else {
      showStatus('Invalid response from bot', true);
      updateLastResponse('Invalid response');
    }
    
  } catch (error) {
    console.error('Next screen failed:', error);
    showStatus('Failed to navigate: ' + error.message, true);
    updateLastResponse('Error: ' + error.message);
  }
}

/**
 * Go back to the previous screen (wraps from first screen to last)
 */
async function previousScreen() {
  try {
    showStatus('Going to previous screen...', false);
    
    // Build packet: PREV_SCREEN (no payload)
    const packet = new Uint8Array([UI_ACTION.PREV_SCREEN]);
    
    // Send WS packet
    const response = await sendWSPacket(packet);
    
    // Parse response: 0x52 + resp_code
    if (response && response.length >= 2) {
      const respCode = response[1];
      handleUIScreenResponse(respCode, 'PREV_SCREEN');
    } else {
      showStatus('Invalid response from bot', true);
      updateLastResponse('Invalid response');
    }
    
  } catch (error) {
    console.error('Previous screen failed:', error);
    showStatus('Failed to navigate: ' + error.message, true);
    updateLastResponse('Error: ' + error.message);
  }
}

/**
 * Jump directly to a specific screen by index
 * @param {number} screenIndex Screen index (0-5):
 *   0 = Splash Screen
 *   1 = App Info / Dashboard
 *   2 = App Log
 *   3 = Service Log
 *   4 = Debug Log
 *   5 = ESP-IDF Log
 */
async function setScreen(screenIndex) {
  // Validate screen index
  if (typeof screenIndex !== 'number' || screenIndex < 0 || screenIndex > 5) {
    showStatus('Invalid screen index (0-5)', true);
    return;
  }
  
  try {
    const screenNames = ['Splash', 'App Info', 'App Log', 'Service Log', 'Debug Log', 'ESP Log'];
    showStatus(`Going to screen ${screenIndex} (${screenNames[screenIndex]})...`, false);
    
    // Build packet: SET_SCREEN [screen_index]
    const packet = new Uint8Array([UI_ACTION.SET_SCREEN, screenIndex]);
    
    // Send WS packet
    const response = await sendWSPacket(packet);
    
    // Parse response: 0x53 + resp_code
    if (response && response.length >= 2) {
      const respCode = response[1];
      handleUIScreenResponse(respCode, `SET_SCREEN (${screenNames[screenIndex]})`);
    } else {
      showStatus('Invalid response from bot', true);
      updateLastResponse('Invalid response');
    }
    
  } catch (error) {
    console.error('Set screen failed:', error);
    showStatus('Failed to navigate: ' + error.message, true);
    updateLastResponse('Error: ' + error.message);
  }
}

/**
 * Handle UI screen navigation response
 */
function handleUIScreenResponse(respCode, command) {
  const respName = BOT_RESP_NAMES[respCode] || `0x${respCode.toString(16)}`;
  updateLastResponse(`${command}: ${respName}`);
  
  if (respCode === BOT_RESP.OK) {
    showStatus('✓ Screen changed', false);
  } else if (respCode === BOT_RESP.INVALID_VALUES) {
    showStatus('Invalid screen index', true);
  } else {
    showStatus(`Failed: ${respName}`, true);
  }
}

// ── Heartbeat Management ──────────────────────────────────────────────────────

/**
 * Start sending heartbeat packets every 40ms.
 * Uses fire-and-forget (WS-like): no response is awaited.
 * DENIED responses are detected asynchronously in processWSResponse().
 */
function startHeartbeat() {
  if (heartbeatInterval) {
    return; // Already running
  }

  heartbeatInterval = setInterval(() => {
    if (!wsConnected) {
      return;
    }
    const packet = new Uint8Array([WS_ACTION.HEARTBEAT]);
    heartbeatSendTime = performance.now();
    sendWSFireAndForget(packet);
  }, HEARTBEAT_INTERVAL_MS);

  updateUIState();
}

/**
 * Stop sending heartbeat packets
 */
function stopHeartbeat() {
  if (heartbeatInterval) {
    clearInterval(heartbeatInterval);
    heartbeatInterval = null;
  }
  updateUIState();
}

// ── Ping and Latency Monitoring ───────────────────────────────────────────────

let pingCanvas = null;
let pingCtx = null;

/**
 * Initialize ping graph canvas
 */
function initPingGraph() {
  pingCanvas = document.getElementById('pingGraph');
  if (pingCanvas) {
    pingCtx = pingCanvas.getContext('2d');
    drawPingGraph();
  }
}

/**
 * Start ping monitoring
 */
function startPing() {
  if (pingInterval) {
    showStatus('Ping already running', true);
    return;
  }
  
  if (!gBotIp) {
    showStatus('Please configure bot IP address', true);
    return;
  }
  
  showStatus('Starting ping monitor...', false);
  pingInterval = setInterval(sendPingPacket, 1000); // Ping every second
  sendPingPacket(); // Send first ping immediately
}

/**
 * Stop ping monitoring
 */
function stopPing() {
  if (pingInterval) {
    clearInterval(pingInterval);
    pingInterval = null;
    showStatus('Ping monitoring stopped', false);
  }
}

/**
 * Send a single ping packet
 */
async function sendPingPacket() {
  try {
    const startTime = performance.now();
    const id = pingSequence++;
    
    // Build PING packet: 0x44 + 4-byte ID (uint32 LE)
    const packet = new Uint8Array(5);
    packet[0] = WS_ACTION.PING;
    packet[1] = id & 0xFF;
    packet[2] = (id >> 8) & 0xFF;
    packet[3] = (id >> 16) & 0xFF;
    packet[4] = (id >> 24) & 0xFF;
    
    // Send WS packet
    const response = await sendWSPacket(packet);
    const endTime = performance.now();
    const latency = Math.round(endTime - startTime);
    
    // Verify response (should be 5-byte echo)
    if (response && response.length === 5 && response[0] === WS_ACTION.PING) {
      recordPingResult(latency);
    } else {
      console.warn('Invalid ping response');
    }
    
  } catch (error) {
    console.error('Ping failed:', error);
  }
}

/**
 * Record ping result and update graph
 */
function recordPingResult(latency) {
  // Add to history
  if (pingHistory.length > MAX_PING_HISTORY) {
    pingHistory.shift();
  }
  
  // Calculate statistics
  pingHistory.push(latency);
  const current = latency;
  const avg = Math.round(pingHistory.reduce((a, b) => a + b, 0) / pingHistory.length);
  const min = Math.min(...pingHistory);
  const max = Math.max(...pingHistory);
  
  // Update stats display
  document.getElementById('pingCurrent').textContent = current;
  document.getElementById('pingAvg').textContent = avg;
  document.getElementById('pingMin').textContent = min;
  document.getElementById('pingMax').textContent = max;
  
  // Update graph
  drawPingGraph();
}

/**
 * Draw the ping graph
 */
function drawPingGraph() {
  if (!pingCtx || !pingCanvas) return;
  
  const width = pingCanvas.width;
  const height = pingCanvas.height;
  const padding = 10;
  const graphWidth = width - padding * 2;
  const graphHeight = height - padding * 2;
  
  // Clear canvas
  pingCtx.fillStyle = '#0a0a0a';
  pingCtx.fillRect(0, 0, width, height);
  
  if (pingHistory.length === 0) {
    // Draw "No Data" message
    pingCtx.fillStyle = '#666';
    pingCtx.font = '14px Arial';
    pingCtx.textAlign = 'center';
    pingCtx.fillText('No ping data yet', width / 2, height / 2);
    return;
  }
  
  // Calculate scale
  const maxLatency = Math.max(...pingHistory, 50); // Minimum scale of 50ms
  const scale = graphHeight / maxLatency;
  
  // Draw grid lines
  pingCtx.strokeStyle = '#222';
  pingCtx.lineWidth = 1;
  for (let i = 0; i <= 4; i++) {
    const y = padding + (graphHeight / 4) * i;
    pingCtx.beginPath();
    pingCtx.moveTo(padding, y);
    pingCtx.lineTo(width - padding, y);
    pingCtx.stroke();
    
    // Draw scale labels
    const value = Math.round(maxLatency * (1 - i / 4));
    pingCtx.fillStyle = '#666';
    pingCtx.font = '10px Arial';
    pingCtx.textAlign = 'right';
    pingCtx.fillText(value + 'ms', padding - 5, y + 3);
  }
  
  // Draw ping line
  pingCtx.strokeStyle = '#4CAF50';
  pingCtx.lineWidth = 2;
  pingCtx.beginPath();
  
  const pointSpacing = graphWidth / (MAX_PING_HISTORY - 1);
  const startIndex = Math.max(0, pingHistory.length - MAX_PING_HISTORY);
  
  for (let i = 0; i < pingHistory.length; i++) {
    const x = padding + pointSpacing * i;
    const y = padding + graphHeight - (pingHistory[i] * scale);
    
    if (i === 0) {
      pingCtx.moveTo(x, y);
    } else {
      pingCtx.lineTo(x, y);
    }
  }
  
  pingCtx.stroke();
  
  // Draw points
  pingCtx.fillStyle = '#4CAF50';
  for (let i = 0; i < pingHistory.length; i++) {
    const x = padding + pointSpacing * i;
    const y = padding + graphHeight - (pingHistory[i] * scale);
    
    pingCtx.beginPath();
    pingCtx.arc(x, y, 2, 0, 2 * Math.PI);
    pingCtx.fill();
  }
}

/**
 * Clear ping graph and history
 */
function clearPingGraph() {
  pingHistory = [];
  pingSequence = 0;
  document.getElementById('pingCurrent').textContent = '—';
  document.getElementById('pingAvg').textContent = '—';
  document.getElementById('pingMin').textContent = '—';
  document.getElementById('pingMax').textContent = '—';
  drawPingGraph();
  showStatus('Ping history cleared', false);
}

// ── WS over WebSocket (fire-and-forget + request-response) ──────────────────

/**
 * Send WS packet to bot via WebSocket bridge
 * 
 * @param {Uint8Array} packet - WS packet data
 * @returns {Promise<Uint8Array>} Response packet (received via WebSocket)
 */
async function sendWSPacket(packet) {
  const packetCopy = new Uint8Array(packet);

  udpSendChain = udpSendChain
    .catch(() => undefined)
    .then(() => sendWSPacketInternal(packetCopy));

  return udpSendChain;
}

let udpSendChain = Promise.resolve();

/**
 * Process a WS response received over the WebSocket bridge.
 *
 * Fire-and-forget senders (heartbeat, servo commands from the joystick)
 * never set a pendingResponse, so their replies are handled here as
 * asynchronous notifications.  Request-response senders (register,
 * unregister, ping) set pendingResponse with an expectedAction byte;
 * only a matching reply resolves that promise.
 */
function processWSResponse(response) {
  // ── Async notification handling (fire-and-forget responses) ──────────
  if (response.length >= 2) {
    const action = response[0];
    const statusByte = response[1];

    // Heartbeat response → measure round-trip time
    if (action === WS_ACTION.HEARTBEAT) {
      if (statusByte === BOT_RESP.NOT_MASTER) {
        console.warn('[WS] Heartbeat denied - master registration lost');
        isMasterRegistered = false;
        stopHeartbeat();
        updateUIState();
        showStatus('Master registration lost - heartbeat denied', true);
      } else if (heartbeatSendTime > 0) {
        const rtt = Math.round(performance.now() - heartbeatSendTime);
        heartbeatRTTHistory.push(rtt);
        if (heartbeatRTTHistory.length > MAX_HEARTBEAT_RTT_HISTORY) {
          heartbeatRTTHistory.shift();
        }
        const avg = Math.round(
          heartbeatRTTHistory.reduce((a, b) => a + b, 0) / heartbeatRTTHistory.length
        );
        const el = document.getElementById('heartbeatResponseTime');
        if (el) {
          el.textContent = `${avg} ms`;
        }
      }
    }
  }

  // ── Request-response matching ───────────────────────────────────────
  if (!pendingResponse) {
    return; // Fire-and-forget reply, already handled above
  }

  // Only resolve when the action byte matches the expected one so that a
  // stray fire-and-forget reply cannot steal a pending registration or ping.
  if (response.length > 0 && response[0] === pendingResponse.expectedAction) {
    clearTimeout(pendingResponse.timeoutId);
    pendingResponse.resolve(response);
    pendingResponse = null;
  }
}

/**
 * Send one WS packet while guaranteeing a single in-flight request.
 *
 * @param {Uint8Array} packet - WS packet data.
 * @returns {Promise<Uint8Array>} Response packet.
 */
async function sendWSPacketInternal(packet) {
  const hexData = Array.from(packet)
    .map(b => b.toString(16).padStart(2, '0'))
    .join('');

  console.log(`[WS TX] ${hexData} via WebSocket to ${gBotIp}:${gBotPort}`);

  if (!wsConnected || !ws || ws.readyState !== WebSocket.OPEN) {
    await initializeWebSocket();
  }

  if (!wsConnected || !ws || ws.readyState !== WebSocket.OPEN) {
    console.error('[WS] WebSocket not connected');
    throw new Error('WebSocket bridge not connected');
  }

  return new Promise((resolve, reject) => {
    const timeoutId = setTimeout(() => {
      pendingResponse = null;
      reject(new Error('WS response timeout (no reply within ' + WS_TIMEOUT + 'ms)'));
    }, WS_TIMEOUT);

    pendingResponse = { resolve, reject, timeoutId, expectedAction: packet[0] };
    ws.send(packet.buffer.slice(packet.byteOffset, packet.byteOffset + packet.byteLength));
  });
}

/**
 * Send a WS-like fire-and-forget message over the WebSocket bridge.
 * Mimics real WS semantics: no response is awaited, the message is
 * silently dropped when the socket is not open.
 *
 * Use this for latency-sensitive controls (joystick servo commands,
 * heartbeat) where waiting for a reply would stall the next send.
 *
 * @param {Uint8Array} packet - Binary payload to send.
 */
function sendWSFireAndForget(packet) {
  if (!wsConnected || !ws || ws.readyState !== WebSocket.OPEN) {
    return; // Silently discard, just like a real WS send on a down link
  }
  ws.send(packet.buffer.slice(packet.byteOffset, packet.byteOffset + packet.byteLength));
}

// ── UI Updates ────────────────────────────────────────────────────────────────

/**
 * Update UI elements based on current state
 */
function updateUIState() {
  // Connection status
  const connStatus = document.getElementById('connectionStatus');
  if (wsConnected) {
    connStatus.textContent = 'Connected';
    connStatus.className = 'status-badge status-connected';
  } else if (wsConnectPromise) {
    connStatus.textContent = 'Connecting';
    connStatus.className = 'status-badge status-warning';
  } else if (gBotIp) {
    connStatus.textContent = 'Configured';
    connStatus.className = 'status-badge status-warning';
  } else {
    connStatus.textContent = 'Not Connected';
    connStatus.className = 'status-badge status-disconnected';
  }
  
  // Master registration status
  const masterStatus = document.getElementById('masterRegistered');
  if (isMasterRegistered) {
    masterStatus.textContent = 'Yes';
    masterStatus.className = 'status-badge status-connected';
    setTitleStatus('[MASTER]', '#4CAF50');
  } else {
    masterStatus.textContent = 'No';
    masterStatus.className = 'status-badge status-disconnected';
    setTitleStatus('', '#999');
  }
  
  // Heartbeat status
  const heartbeatStatus = document.getElementById('heartbeatStatus');
  if (heartbeatInterval) {
    heartbeatStatus.textContent = 'Running';
    heartbeatStatus.className = 'status-badge status-connected';
  } else {
    heartbeatStatus.textContent = 'Stopped';
    heartbeatStatus.className = 'status-badge status-disconnected';
  }
}

/**
 * Update last response display
 */
function updateLastResponse(text) {
  const elem = document.getElementById('lastResponse');
  if (elem) {
    elem.textContent = text;
    elem.style.fontFamily = "'Courier New', monospace";
    elem.style.fontSize = '12px';
  }
}

/**
 * Toggle a panel section while collapsing another (mutually exclusive)
 * @param {string} thisSectionId - id of the section to toggle
 * @param {string} thisBtnId - id of the button for this section
 * @param {string} otherSectionId - id of the other section to collapse
 * @param {string} otherBtnId - id of the button for the other section
 */
function togglePanelExclusive(thisSectionId, thisBtnId, otherSectionId, otherBtnId) {
  const thisBody = document.getElementById(thisSectionId);
  const thisBtn = document.getElementById(thisBtnId);
  const otherBody = document.getElementById(otherSectionId);
  const otherBtn = document.getElementById(otherBtnId);
  
  // If this panel is currently collapsed, expand it and collapse the other
  const isThisCollapsed = thisBody.classList.contains('collapsed');
  
  if (isThisCollapsed) {
    // Expand this panel
    thisBody.classList.remove('collapsed');
    thisBtn.classList.remove('collapsed');
    
    // Collapse the other panel
    if (otherBody && otherBtn) {
      otherBody.classList.add('collapsed');
      otherBtn.classList.add('collapsed');
    }
  } else {
    // Just collapse this panel (don't force expand the other)
    thisBody.classList.add('collapsed');
    thisBtn.classList.add('collapsed');
  }
}

// ── Script Engine ─────────────────────────────────────────────────────────────

const SCRIPT_STORAGE_KEY = 'botScripts';
let _scriptAbortController = null;

/**
 * Promise-based delay helper, respects abort signal.
 * @param {number} ms - Milliseconds to wait
 */
function delay(ms) {
  return new Promise((resolve, reject) => {
    if (_scriptAbortController && _scriptAbortController.signal.aborted) {
      return reject(new Error('Script stopped'));
    }
    const t = setTimeout(resolve, ms);
    if (_scriptAbortController) {
      _scriptAbortController.signal.addEventListener('abort', () => {
        clearTimeout(t);
        reject(new Error('Script stopped'));
      });
    }
  });
}

/** Append a line to the script output console */
function _scriptLog(msg, isError) {
  const out = document.getElementById('scriptOutput');
  if (!out) return;
  const line = document.createElement('div');
  line.style.color = isError ? '#f66' : '#aaa';
  line.textContent = `[${new Date().toLocaleTimeString()}] ${msg}`;
  out.appendChild(line);
  out.scrollTop = out.scrollHeight;
}

/** Run the script currently in the textarea */
async function runScript() {
  stopScript(); // cancel any previously running script
  _scriptAbortController = new AbortController();

  const code = document.getElementById('scriptEditor').value.trim();
  if (!code) { _scriptLog('Script is empty.', true); return; }

  const out = document.getElementById('scriptOutput');
  if (out) out.innerHTML = '';
  _scriptLog('▶ Running…', false);

  try {
    // Wrap in an async IIFE so top-level await works
    const AsyncFunction = Object.getPrototypeOf(async function(){}).constructor;
    const fn = new AsyncFunction('delay', code);
    await fn(delay);
    _scriptLog('✔ Done.', false);
  } catch (e) {
    if (e.message !== 'Script stopped') {
      console.error(e);
      _scriptLog('✖ ' + e.message, true);
    } else {
      _scriptLog('⏹ Stopped.', false);
    }
  } finally {
    _scriptAbortController = null;
  }
}

/** Abort a running script */
function stopScript() {
  if (_scriptAbortController) {
    _scriptAbortController.abort();
    _scriptAbortController = null;
  }
}

// ── localStorage persistence ──────────────────────────────────────────────────

/** Load the scripts map from localStorage */
function _loadScriptsMap() {
  try { return JSON.parse(localStorage.getItem(SCRIPT_STORAGE_KEY)) || {}; }
  catch { return {}; }
}

/** Persist the scripts map to localStorage */
function _saveScriptsMap(map) {
  localStorage.setItem(SCRIPT_STORAGE_KEY, JSON.stringify(map));
}

/** Refresh the slot <select> from localStorage */
function _refreshSlotList() {
  const select = document.getElementById('scriptSlot');
  if (!select) return;
  const map = _loadScriptsMap();
  const current = select.value;
  select.innerHTML = '<option value="">— saved scripts —</option>';
  Object.keys(map).sort().forEach(name => {
    const opt = document.createElement('option');
    opt.value = name;
    opt.textContent = name;
    select.appendChild(opt);
  });
  if (current && map[current]) select.value = current;
}

/** Save current editor content under the given name */
function saveScript() {
  const name = document.getElementById('scriptName').value.trim() || 'script-' + Date.now();
  const code = document.getElementById('scriptEditor').value;
  const map  = _loadScriptsMap();
  map[name]  = code;
  _saveScriptsMap(map);
  _refreshSlotList();
  document.getElementById('scriptSlot').value = name;
  _scriptLog(`💾 Saved as "${name}"`, false);
}

/** Load selected slot into editor */
function loadScriptFromSlot() {
  const name = document.getElementById('scriptSlot').value;
  if (!name) return;
  const map = _loadScriptsMap();
  if (map[name] !== undefined) {
    document.getElementById('scriptEditor').value = map[name];
    document.getElementById('scriptName').value = name;
    _scriptLog(`📂 Loaded "${name}"`, false);
  }
}

/** Delete the currently selected slot */
function deleteScript() {
  const name = document.getElementById('scriptSlot').value;
  if (!name) { _scriptLog('No script selected.', true); return; }
  const map = _loadScriptsMap();
  delete map[name];
  _saveScriptsMap(map);
  _refreshSlotList();
  _scriptLog(`🗑️ Deleted "${name}"`, false);
}

/** Download editor content as a .js file */
function exportScript() {
  const name = (document.getElementById('scriptName').value.trim() || 'botscript') + '.js';
  const blob = new Blob([document.getElementById('scriptEditor').value], { type: 'text/javascript' });
  const a = document.createElement('a');
  a.href = URL.createObjectURL(blob);
  a.download = name;
  a.click();
  URL.revokeObjectURL(a.href);
}

/** Import a .js / .txt file into the editor */
function importScript(event) {
  const file = event.target.files[0];
  if (!file) return;
  const reader = new FileReader();
  reader.onload = e => {
    document.getElementById('scriptEditor').value = e.target.result;
    document.getElementById('scriptName').value = file.name.replace(/\.[^.]+$/, '');
    _scriptLog(`⬆️ Imported "${file.name}"`, false);
  };
  reader.readAsText(file);
  event.target.value = '';
}

// ── Board filesystem (ESP32 LittleFS /scripts/) ──────────────────────────────

const SCRIPT_BOARD_BASE = '/scripts';

/**
 * Refresh the board slot <select> by listing scripts from the board.
 */
async function boardListScripts() {
  try {
    const res = await fetch(SCRIPT_BOARD_BASE);
    if (!res.ok) throw new Error(`HTTP ${res.status}`);
    const names = await res.json(); // string[]
    _refreshBoardSlotList(names);
    _scriptLog(`🔄 Board: ${names.length} script(s)`, false);
  } catch (e) {
    _scriptLog(`❌ Board list failed: ${e.message}`, true);
    showStatus('Board list failed: ' + e.message, true);
  }
}

/** Populate the board slot <select> */
function _refreshBoardSlotList(names) {
  const select = document.getElementById('boardScriptSlot');
  if (!select) return;
  const current = select.value;
  select.innerHTML = '<option value="">— board scripts —</option>';
  (names || []).sort().forEach(name => {
    const opt = document.createElement('option');
    opt.value = name;
    opt.textContent = name;
    select.appendChild(opt);
  });
  if (current && (names || []).includes(current)) select.value = current;
}

/** onchange handler for the board slot <select> */
function loadScriptFromBoardSlot() {
  const name = document.getElementById('boardScriptSlot').value;
  if (!name) return;
  boardLoadScript(name);
}

/**
 * Load a script from the board into the editor.
 * @param {string} [nameArg] Override; defaults to the board slot selection.
 */
async function boardLoadScript(nameArg) {
  const name = nameArg || document.getElementById('boardScriptSlot').value;
  if (!name) { _scriptLog('No board script selected.', true); return; }
  try {
    const res = await fetch(`${SCRIPT_BOARD_BASE}/${encodeURIComponent(name)}`);
    if (!res.ok) throw new Error(`HTTP ${res.status}`);
    const code = await res.text();
    document.getElementById('scriptEditor').value = code;
    document.getElementById('scriptName').value = name;
    document.getElementById('boardScriptSlot').value = name;
    _scriptLog(`📥 Loaded "${name}" from board`, false);
    showStatus(`📥 Loaded "${name}" from board`, false);
  } catch (e) {
    _scriptLog(`❌ Board load failed: ${e.message}`, true);
    showStatus('Board load failed: ' + e.message, true);
  }
}

/**
 * Save the current editor content to the board under the script name.
 */
async function boardSaveScript() {
  const name = document.getElementById('scriptName').value.trim() || 'script-' + Date.now();
  const code = document.getElementById('scriptEditor').value;
  try {
    const res = await fetch(`${SCRIPT_BOARD_BASE}/${encodeURIComponent(name)}`, {
      method: 'POST',
      headers: { 'Content-Type': 'text/plain; charset=utf-8' },
      body: code
    });
    if (!res.ok) throw new Error(`HTTP ${res.status}`);
    _scriptLog(`📤 Saved "${name}" to board`, false);
    showStatus(`📤 Saved "${name}" to board`, false);
    await boardListScripts();
    const sel = document.getElementById('boardScriptSlot');
    if (sel) sel.value = name;
  } catch (e) {
    _scriptLog(`❌ Board save failed: ${e.message}`, true);
    showStatus('Board save failed: ' + e.message, true);
  }
}

/**
 * Delete the currently selected board script.
 */
async function boardDeleteScript() {
  const name = document.getElementById('boardScriptSlot').value;
  if (!name) { _scriptLog('No board script selected.', true); return; }
  if (!confirm(`Delete "${name}" from the board?`)) return;
  try {
    const res = await fetch(`${SCRIPT_BOARD_BASE}/${encodeURIComponent(name)}`, {
      method: 'DELETE'
    });
    if (!res.ok) throw new Error(`HTTP ${res.status}`);
    _scriptLog(`🗑️ Deleted "${name}" from board`, false);
    showStatus(`🗑️ Deleted "${name}" from board`, false);
    await boardListScripts();
  } catch (e) {
    _scriptLog(`❌ Board delete failed: ${e.message}`, true);
    showStatus('Board delete failed: ' + e.message, true);
  }
}

// Populate slot list on load
document.addEventListener('DOMContentLoaded', _refreshSlotList);
document.addEventListener('DOMContentLoaded', boardListScripts);
'use strict';

// ═════════════════════════════════════════════════════════════════════════════
// ⚠️  ARCHITECTURE: FIRE-AND-FORGET SERVO CONTROL MODE
// ═════════════════════════════════════════════════════════════════════════════
//
// BotScriptActions.js provides GAMEPAD/KEYBOARD input handling and
// FIRE-AND-FORGET servo control functions optimized for real-time response.
// This file is auto-loaded and always available.
//
// 📌 IMPORTANT: Two different servo control modes exist (choose carefully):
//
// MODE 1: REQUEST-RESPONSE (BotScript.js functions ending in "UI")
//   Functions: requestSetServoAngles(pairs[]), requestSetServoSpeeds(pairs[]), attachServos()
//   Behavior: Waits for response, shows status, gets values from HTML inputs
//   Use case: UI buttons in WebSocketJoystick.html, manual control
//
// MODE 2: FIRE-AND-FORGET (BotScriptActions.js - THIS FILE)
//   Functions: attachServo(channel, type), setServoAngle(channel, angle),
//             setServoSpeeds(pairs[])
//   Behavior: Returns immediately, no wait, optimized for 50ms gamepad polling
//   Use case: Real-time control from gamepad/keyboard, script automation
//
// ✅ AVAILABLE IN YOUR CODE (from this file):
//   - Constants: SERVO_TYPES, XBOX_BUTTONS, PLAYSTATION_BUTTONS, etc.
//   - Gamepad: detectGamepad(), pollGamepad(), handleGamepaButton()
//   - Gamepad SVG overlay: showGamepad(true/false) — live visual feedback
//   - Servo control (fire-and-forget): attachServo(), setServoAngle(), setServoSpeeds()
//   - UI: updateButtonIndicator(), updateGamepadStatus()
//   - Hooks: CUSTOMCONTROL.onKeyDown, CUSTOMCONTROL.onKeyUp, CUSTOMCONTROL.processGamepadInput
//
// ❌ NOT AVAILABLE (use from BotScript.js instead):
//   - registerMaster(), unregisterMaster() — Master registration (CALL FIRST)
//   - startHeartbeat(), stopHeartbeat() — Connection keepalive (auto-started)
//   - setBotName(), getBattery() — Bot info queries
//   - setScreen(), nextScreen(), previousScreen() — UI screen navigation
//   - Request-response servo functions: requestSetServoAngles(), requestSetServoSpeeds()
//
// HOW TO USE:
// 1. Override CUSTOMCONTROL hooks in your HTML page:
//    - CUSTOMCONTROL.onKeyDown(event) — keyboard key down
//    - CUSTOMCONTROL.onKeyUp(event) — keyboard key up
//    - CUSTOMCONTROL.processGamepadInput(gamepad) — gamepad input
// 2. Call fire-and-forget servo functions from within those hooks
// 3. Functions return immediately (no waiting for responses)
// 4. Use requestSetServoSpeeds(pairs[]) from BotScript.js if you need confirmed response
//
// ═════════════════════════════════════════════════════════════════════════════

// ── Xbox Controller Configuration ─────────────────────────────────────────────
const SERVO_TYPES={
  ANGULAR_270: 1, 
  ROTATIONAL: 2
}
// Xbox controller button indices (standard Gamepad API mapping)
const XBOX_BUTTONS = {
  // Face buttons
  A:             0,  // A (bottom)
  B:             1,  // B (right)
  X:             2,  // X (left)
  Y:             3,  // Y (top)
  // Shoulder / trigger buttons
  LB:            4,  // Left bumper
  RB:            5,  // Right bumper
  LT:            6,  // Left trigger  (analog: value 0.0–1.0)
  RT:            7,  // Right trigger (analog: value 0.0–1.0)
  // Center buttons
  BACK:          8,  // Back / View / Select
  START:         9,  // Start / Menu
  // Guide / Home button (may be absent on some browsers)
  GUIDE:        16,  
  // Stick clicks
  LS:           10,  // Left stick click (L3)
  RS:           11,  // Right stick click (R3)
  // D-Pad
  DPAD_UP:      12,
  DPAD_DOWN:    13,
  DPAD_LEFT:    14,
  DPAD_RIGHT:   15

};

// PlayStation controller button indices (standard Gamepad API mapping)
const PLAYSTATION_BUTTONS = {
  // Face buttons
  X:             0,  // X (bottom)
  CIRCLE:        1,  // O/Circle (right)
  SQUARE:        2,  // Square (left)
  TRIANGLE:      3,  // Triangle (top)
  // Shoulder / trigger buttons
  L1:            4,  // L1 bumper
  R1:            5,  // R1 bumper
  L2:            6,  // L2 trigger (analog: value 0.0–1.0)
  R2:            7,  // R2 trigger (analog: value 0.0–1.0)
  // Center buttons
  SHARE:         8,  // Share / Select
  OPTIONS:       9,  // Options / Start
  // Guide / Home button
  GUIDE:        16,  // PS button
  // Stick clicks
  L3:           10,  // Left stick click
  R3:           11,  // Right stick click
  // D-Pad
  DPAD_UP:      12,
  DPAD_DOWN:    13,
  DPAD_LEFT:    14,
  DPAD_RIGHT:   15
};

// Nintendo Switch Pro controller button indices (standard Gamepad API mapping)
const NINTENDO_SWITCH_BUTTONS = {
  // Face buttons (note: Nintendo layout differs visually)
  B:             0,  // B (bottom)
  A:             1,  // A (right)
  Y:             2,  // Y (left)
  X:             3,  // X (top)
  // Shoulder / trigger buttons
  L:             4,  // L bumper
  R:             5,  // R bumper
  ZL:            6,  // ZL trigger (analog: value 0.0–1.0)
  ZR:            7,  // ZR trigger (analog: value 0.0–1.0)
  // Center buttons
  MINUS:         8,  // Minus / Select
  PLUS:          9,  // Plus / Start
  // Guide / Home button
  HOME:         16,  // Home button
  // Stick clicks
  LS:           10,  // Left stick click
  RS:           11,  // Right stick click
  // D-Pad
  DPAD_UP:      12,
  DPAD_DOWN:    13,
  DPAD_LEFT:    14,
  DPAD_RIGHT:   15
};

// Generic USB gamepad button indices (standard Gamepad API mapping)
// Most generic gamepads follow this standard layout
const GENERIC_GAMEPAD_BUTTONS = {
  // Face buttons
  BUTTON_1:      0,  // Bottom/South button
  BUTTON_2:      1,  // Right/East button
  BUTTON_3:      2,  // Left/West button
  BUTTON_4:      3,  // Top/North button
  // Shoulder / trigger buttons
  LB:            4,  // Left bumper
  RB:            5,  // Right bumper
  LT:            6,  // Left trigger (analog: value 0.0–1.0)
  RT:            7,  // Right trigger (analog: value 0.0–1.0)
  // Center buttons
  BACK:          8,  // Back / Select
  START:         9,  // Start / Menu
  // Guide / Home button
  GUIDE:        16,  // Home button (may be absent)
  // Stick clicks
  LS:           10,  // Left stick click
  RS:           11,  // Right stick click
  // D-Pad
  DPAD_UP:      12,
  DPAD_DOWN:    13,
  DPAD_LEFT:    14,
  DPAD_RIGHT:   15
};

// Xbox controller axis indices (standard Gamepad API mapping)
// Values range from -1.0 to +1.0
const STICK_AXES = {
  LEFT_X:   0,  // Left stick horizontal  (-1 = left,  +1 = right)
  LEFT_Y:   1,  // Left stick vertical    (-1 = up,    +1 = down)
  RIGHT_X:  2,  // Right stick horizontal (-1 = left,  +1 = right)
  RIGHT_Y:  3   // Right stick vertical   (-1 = up,    +1 = down)
};


let pollIntervalMs = 50;

let gamepadConnected = false;
let gamepadIndex = -1;
let animationFrameId = null;
let lastGamepadPollTime = 0;

// Button state tracking (to detect press/release)
const buttonStates = {};

// Keyboard state tracking
const keyStates = {};

// ── Gamepad SVG Overlay ────────────────────────────────────────────────────────
//
// showGamepad(true/false) — Show/hide a live SVG overlay of the detected
// gamepad. Buttons light up on press, sticks move with analog input.
// SVGs are loaded from gamepadxbox.svg, gamepadsony.svg, gamepadnintendo.svg.
// Elements are matched by their inkscape:label attribute.
//
// Usage:  showGamepad(true);   // enable overlay
//         showGamepad(false);  // hide overlay
// ──────────────────────────────────────────────────────────────────────────────

let _gpadOverlayEnabled = false;
let _gpadSVGLoaded = false;
let _gpadSVGType = '';          // 'xbox' | 'sony' | 'nintendo'
let _gpadSVGCache = {};         // { label: { el, origFill } }

// SVG file paths per detected gamepad family
const _GPAD_SVG_FILES = {
  xbox:     './gamepadxbox.svg',
  sony:     './gamepadsony.svg',
  nintendo: './gamepadnintendo.svg'
};

// Button index → inkscape:label per gamepad family
const _GPAD_BTN_MAP = {
  xbox: {
    0: 'btnA',  1: 'btnB',  2: 'btnX',  3: 'btnY',
    4: 'trigLeftTop',  5: 'trigRightTop',
    6: 'trigLeftBottom',  7: 'trigRightBottom',
    8: 'btnBack',  9: 'btnStart',  16: 'btnSelect',
    12: 'padUp',  13: 'padDown',  14: 'padLeft',  15: 'padRight'
  },
  sony: {
    0: 'btnCross',  1: 'btnCircle',  2: 'btnSquare',  3: 'btnTriangle',
    4: 'trigLeftTop',  5: 'trigRightTop',
    8: 'btnShare',  9: 'btnOptions',
    12: 'padUp',  13: 'padDown',  14: 'padLeft',  15: 'padRight'
  },
  nintendo: {
    0: 'btnCross',  1: 'btnCircle',  2: 'btnSquare',  3: 'btnTriangle',
    12: 'padUp',  13: 'padDown',  14: 'padLeft',  15: 'padRight'
  }
};

// Stick click buttons also flash the stick element
const _GPAD_STICK_CLICK = { 10: 'stickLeft', 11: 'stickRight' };

// Visual tuning
const _GPAD_COLOR_PRESSED  = '#FFD700'; // gold
const _GPAD_COLOR_STICK    = '#00BFFF'; // sky blue
const _GPAD_STICK_OFFSET   = 10;        // max px translation
const _GPAD_DEADZONE        = 0.12;

/**
 * Show or hide the gamepad SVG overlay.
 * When enabled, auto-detects gamepad type and loads the matching SVG file.
 * Button presses and stick movements update the SVG in real-time.
 *
 * @param {boolean} enable - true to show, false to hide
 */
/**
 * Toggle the gamepad SVG overlay on/off and update the button UI.
 * Called by the 🎮 Show/Hide Gamepad button in BotScript.html.
 */
function _toggleGamepadOverlay() {
  const btn = document.getElementById('btnShowGamepad');
  const wasOn = _gpadOverlayEnabled;
  showGamepad(!wasOn);
  if (btn) {
    btn.textContent = wasOn ? '🎮 Show Gamepad' : '🎮 Hide Gamepad';
    btn.className   = wasOn ? 'btn btn-primary'  : 'btn btn-danger';
  }
}

function showGamepad(enable) {
  _gpadOverlayEnabled = !!enable;

  if (enable) {
    _gpadEnsureContainer();
    const c = document.getElementById('gamepadSVGContainer');
    c.style.display = 'block';

    // If a gamepad is already connected, load SVG immediately
    if (gamepadConnected && gamepadIndex >= 0) {
      const gp = navigator.getGamepads()[gamepadIndex];
      if (gp) {
        _gpadLoadSVG(_gpadDetectType(gp.id));
        return;
      }
    }
    c.innerHTML = '<p style="color:#888;text-align:center;font-size:14px;">' +
                  '🎮 Connect a gamepad…</p>';
    _gpadSVGLoaded = false;
  } else {
    const c = document.getElementById('gamepadSVGContainer');
    if (c) { c.style.display = 'none'; c.innerHTML = ''; }
    _gpadSVGLoaded = false;
    _gpadSVGCache = {};
  }
}

/** Create the overlay container if it doesn't exist. @private */
function _gpadEnsureContainer() {
  if (document.getElementById('gamepadSVGContainer')) return;
  const c = document.createElement('div');
  c.id = 'gamepadSVGContainer';
  c.style.cssText = 'width:100%;max-width:480px;margin:12px auto;' +
    'text-align:center;position:relative;user-select:none;pointer-events:none';
  // Insert after controlPanel
  const anchor = document.getElementById('controlPanel')
              || document.getElementById('scriptPanel');
  if (anchor) anchor.parentNode.insertBefore(c, anchor.nextSibling);
  else document.body.prepend(c);
}

/**
 * Detect gamepad family from the navigator id string.
 * @param {string} gpId
 * @returns {string} 'xbox' | 'sony' | 'nintendo'
 * @private
 */
function _gpadDetectType(gpId) {
  const id = (gpId || '').toLowerCase();
  if (id.includes('dualsense') || id.includes('dualshock') ||
      id.includes('playstation') || id.includes('ps3') ||
      id.includes('ps4') || id.includes('ps5') ||
      id.includes('054c'))                             return 'sony';
  if (id.includes('switch') || id.includes('pro controller') ||
      id.includes('057e'))                             return 'nintendo';
  return 'xbox'; // default / XInput / generic
}

/**
 * Fetch the SVG and inject it into the overlay container.
 * @param {string} type - 'xbox' | 'sony' | 'nintendo'
 * @private
 */
function _gpadLoadSVG(type) {
  const c = document.getElementById('gamepadSVGContainer');
  if (!c) return;
  _gpadSVGLoaded = false;
  _gpadSVGCache = {};
  _gpadSVGType = type;
  c.innerHTML = '<p style="color:#888;text-align:center;font-size:13px;">Loading…</p>';

  const file = _GPAD_SVG_FILES[type] || _GPAD_SVG_FILES.xbox;
  fetch(file)
    .then(r => { if (!r.ok) throw new Error(r.status); return r.text(); })
    .then(svg => {
      if (!_gpadOverlayEnabled) return;
      c.innerHTML = svg;
      const svgEl = c.querySelector('svg');
      if (svgEl) {
        svgEl.style.width = '100%';
        svgEl.style.height = 'auto';
        svgEl.style.maxHeight = '300px';
        svgEl.removeAttribute('width');
        svgEl.removeAttribute('height');
      }
      _gpadCacheElements(c, type);
      _gpadSVGLoaded = true;
      _scriptLog('🎮 Gamepad SVG loaded: ' + file);
    })
    .catch(err => {
      c.innerHTML = '<p style="color:#c66;text-align:center;font-size:13px;">' +
        '⚠️ SVG not found: ' + file + '</p>';
      _scriptLog('⚠️ Failed to load gamepad SVG: ' + err.message);
    });
}

/**
 * Find SVG elements by inkscape:label and cache their references + original fills.
 * @param {HTMLElement} container
 * @param {string} type
 * @private
 */
function _gpadCacheElements(container, type) {
  _gpadSVGCache = {};
  const all = container.querySelectorAll('*');

  // Build a set of labels we care about
  const btnMap = _GPAD_BTN_MAP[type] || {};
  const wantedLabels = new Set();
  Object.values(btnMap).forEach(l => wantedLabels.add(l));
  Object.values(_GPAD_STICK_CLICK).forEach(l => wantedLabels.add(l));
  wantedLabels.add('stickLeft');
  wantedLabels.add('stickRight');

  all.forEach(el => {
    const label = el.getAttribute('inkscape:label');
    if (label && wantedLabels.has(label)) {
      // Read original fill from inline style or attribute
      const cs = el.style.fill || el.getAttribute('fill') || '';
      _gpadSVGCache[label] = { el: el, origFill: cs || '#bbbbbb' };
    }
  });

  const n = Object.keys(_gpadSVGCache).length;
  if (n === 0) {
    _scriptLog('⚠️ SVG loaded but no matching inkscape:label found.');
  } else {
    _scriptLog('🎮 Cached ' + n + ' SVG elements for live feedback');
  }
}

/**
 * Set fill color on a cached SVG element.
 * @param {string} label - inkscape:label
 * @param {string} color - CSS color, or null to restore original
 * @private
 */
function _gpadSetFill(label, color) {
  const c = _gpadSVGCache[label];
  if (!c) return;
  c.el.style.fill = color || c.origFill;
}

/**
 * Update all SVG button + stick visuals from live gamepad state.
 * Called every poll frame when the overlay is enabled.
 * @param {Gamepad} gamepad
 * @private
 */
function _gpadUpdateSVG(gamepad) {
  if (!_gpadSVGLoaded || !_gpadOverlayEnabled) return;

  const btnMap = _GPAD_BTN_MAP[_gpadSVGType] || {};

  // ── Buttons ──
  for (const [idx, label] of Object.entries(btnMap)) {
    const btn = gamepad.buttons[idx];
    if (!btn) continue;
    const pressed = btn.pressed || btn.value > 0.5;
    _gpadSetFill(label, pressed ? _GPAD_COLOR_PRESSED : null);
  }

  // ── Stick clicks (flash the stick element) ──
  for (const [idx, label] of Object.entries(_GPAD_STICK_CLICK)) {
    const btn = gamepad.buttons[idx];
    if (!btn) continue;
    const pressed = btn.pressed || btn.value > 0.5;
    if (pressed) _gpadSetFill(label, _GPAD_COLOR_PRESSED);
    // don't reset here — stick position color takes over below
  }

  // ── Sticks ──
  _gpadUpdateStick('stickLeft',  gamepad.axes[0] || 0, gamepad.axes[1] || 0);
  _gpadUpdateStick('stickRight', gamepad.axes[2] || 0, gamepad.axes[3] || 0);
}

/**
 * Translate and tint a stick element based on axis values.
 * @param {string} label - 'stickLeft' or 'stickRight'
 * @param {number} x - axis X  (-1 … +1)
 * @param {number} y - axis Y  (-1 … +1)
 * @private
 */
function _gpadUpdateStick(label, x, y) {
  const c = _gpadSVGCache[label];
  if (!c) return;

  const mag = Math.sqrt(x * x + y * y);
  const dead = mag < _GPAD_DEADZONE;

  // Translate
  const ox = dead ? 0 : x * _GPAD_STICK_OFFSET;
  const oy = dead ? 0 : y * _GPAD_STICK_OFFSET;
  const orig = c.origTransform || '';

  // Store original transform once
  if (c.origTransform === undefined) {
    c.origTransform = c.el.getAttribute('transform') || '';
  }

  c.el.setAttribute('transform',
    (c.origTransform ? c.origTransform + ' ' : '') +
    'translate(' + ox.toFixed(1) + ',' + oy.toFixed(1) + ')');

  // Tint based on deflection magnitude
  if (dead) {
    c.el.style.fill = c.origFill;
  } else {
    // Interpolate from original grey to sky blue based on magnitude
    const t = Math.min(mag, 1);
    c.el.style.fill = _gpadLerpColor(c.origFill, _GPAD_COLOR_STICK, t);
  }
}

/**
 * Simple hex color lerp.
 * @param {string} a - start color (hex or named)
 * @param {string} b - end color (hex)
 * @param {number} t - 0…1
 * @returns {string} hex color
 * @private
 */
function _gpadLerpColor(a, b, t) {
  const pa = _gpadParseColor(a);
  const pb = _gpadParseColor(b);
  const r = Math.round(pa[0] + (pb[0] - pa[0]) * t);
  const g = Math.round(pa[1] + (pb[1] - pa[1]) * t);
  const bl = Math.round(pa[2] + (pb[2] - pa[2]) * t);
  return '#' + ((1 << 24) | (r << 16) | (g << 8) | bl).toString(16).slice(1);
}

/** Parse hex color to [r,g,b]. Falls back to [187,187,187] (#bbbbbb). @private */
function _gpadParseColor(c) {
  if (!c || c.charAt(0) !== '#') return [187, 187, 187];
  const hex = c.length === 4
    ? c[1]+c[1]+c[2]+c[2]+c[3]+c[3]
    : c.slice(1,7);
  const n = parseInt(hex, 16);
  return [(n >> 16) & 255, (n >> 8) & 255, n & 255];
}

/**
 * Called from onGamepadConnected when overlay is active.
 * @param {Gamepad} gamepad
 * @private
 */
function _gpadOnConnect(gamepad) {
  if (!_gpadOverlayEnabled) return;
  _gpadEnsureContainer();
  _gpadLoadSVG(_gpadDetectType(gamepad.id));
}

/**
 * Called from onGamepadDisconnected when overlay is active.
 * @private
 */
function _gpadOnDisconnect() {
  if (!_gpadOverlayEnabled) return;
  _gpadSVGLoaded = false;
  _gpadSVGCache = {};
  const c = document.getElementById('gamepadSVGContainer');
  if (c) {
    c.innerHTML = '<p style="color:#888;text-align:center;font-size:14px;">' +
                  '🎮 Connect a gamepad…</p>';
  }
}

// ── Initialization ────────────────────────────────────────────────────────────

const CUSTOMCONTROL = {
  _onKeyUpWarned: false,
  onKeyUp: function (event) {
    if (this._onKeyUpWarned) return;
    this._onKeyUpWarned = true;
    _scriptLog('');
    _scriptLog('❌ ERROR: CUSTOMCONTROL.onKeyUp() not defined!');
    _scriptLog('═════════════════════════════════════════════════════════════');
    _scriptLog('WHY: The keyboard key-up event was triggered, but you haven\'t');
    _scriptLog('     defined what should happen when keys are released.');
    _scriptLog('');
    _scriptLog('HOW TO FIX: Add this to your code in the Script Editor textarea:');
    _scriptLog('');
    _scriptLog('CUSTOMCONTROL.onKeyUp = function(event) {');
    _scriptLog('  const key = event.key.toLowerCase();');
    _scriptLog('  _scriptLog("Key released: " + key);');
    _scriptLog('  if (key === "w") stop();  // Example: stop on W key release');
    _scriptLog('};');
    _scriptLog('');
    _scriptLog('LEARN MORE: See "Full Coding Guide" → "Step 3: Handle Events"');
    _scriptLog('═════════════════════════════════════════════════════════════');
    _scriptLog('Key released: ' + event.key);
  },

  _onKeyDownWarned: false,
  onKeyDown: function (event) {
    if (this._onKeyDownWarned) return;
    this._onKeyDownWarned = true;
    _scriptLog('');
    _scriptLog('❌ ERROR: CUSTOMCONTROL.onKeyDown() not defined!');
    _scriptLog('═════════════════════════════════════════════════════════════');
    _scriptLog('WHY: A keyboard key was pressed, but you haven\'t defined');
    _scriptLog('     what should happen when keys are pressed.');
    _scriptLog('');
    _scriptLog('HOW TO FIX: Add this to your code in the Script Editor textarea:');
    _scriptLog('');
    _scriptLog('CUSTOMCONTROL.onKeyDown = function(event) {');
    _scriptLog('  const key = event.key.toLowerCase();');
    _scriptLog('  _scriptLog("Key pressed: " + key);');
    _scriptLog('  if (key === "w") moveForward();  // Example: move on W key');
    _scriptLog('};');
    _scriptLog('');
    _scriptLog('LEARN MORE: See "Full Coding Guide" → "Step 3: Handle Events"');
    _scriptLog('═════════════════════════════════════════════════════════════');
    _scriptLog('Key pressed: ' + event.key);
  },

  _processGamepadInputWarned: false,
  processGamepadInput: function (gamepad) {
    if (this._processGamepadInputWarned) return;
    this._processGamepadInputWarned = true;
    _scriptLog('');
    _scriptLog('❌ ERROR: CUSTOMCONTROL.processGamepadInput() not defined!');
    _scriptLog('═════════════════════════════════════════════════════════════');
    _scriptLog('WHY: A gamepad was detected, but you haven\'t defined');
    _scriptLog('     what buttons/sticks should do.');
    _scriptLog('');
    _scriptLog('HOW TO FIX: Add this to your code in the Script Editor textarea:');
    _scriptLog('');
    _scriptLog('CUSTOMCONTROL.processGamepadInput = function(gamepad) {');
    _scriptLog('  if (gamepad.buttons[XBOX_BUTTONS.DPAD_UP].pressed) {');
    _scriptLog('    moveForward();');
    _scriptLog('  } else if (gamepad.buttons[XBOX_BUTTONS.DPAD_DOWN].pressed) {');
    _scriptLog('    moveBackward();');
    _scriptLog('  } else {');
    _scriptLog('    stop();');
    _scriptLog('  }');
    _scriptLog('};');
    _scriptLog('');
    _scriptLog('LEARN MORE: See "Full Coding Guide" → "Step 3: Handle Events"');
    _scriptLog('═════════════════════════════════════════════════════════════');
    _scriptLog('Gamepad detected: ' + gamepad.id);
    _scriptLog('Define CUSTOMCONTROL.processGamepadInput() to use this gamepad');
  }
};
// ── Keep this  ───────────────────────────────────────────────────

function onKeyDown(event) {   CUSTOMCONTROL.onKeyDown(event);  }
function onKeyUp(event) {  CUSTOMCONTROL.onKeyUp(event);}
function processGamepadInput(gamepad) {  CUSTOMCONTROL.processGamepadInput(gamepad);}


// ── Main Event Listeners ───────────────────────────────────────────────────

document.addEventListener('DOMContentLoaded', () => {
  // Initialize button states
  Object.values(XBOX_BUTTONS).forEach(btnIdx => {
    buttonStates[btnIdx] = false;
  });
  
  // Listen for gamepad connection
  window.addEventListener('gamepadconnected', onGamepadConnected);
  window.addEventListener('gamepaddisconnected', onGamepadDisconnected);
  
  // Listen for keyboard events
  window.addEventListener('keydown', onKeyDown);
  window.addEventListener('keyup', onKeyUp);
  
  // Start polling if gamepad already connected
  checkGamepadConnection();
});

// ── Gamepad Connection Handlers ───────────────────────────────────────────────

/**
 * Check if gamepad is already connected on page load
 */
function checkGamepadConnection() {
  const gamepads = navigator.getGamepads();
  for (let i = 0; i < gamepads.length; i++) {
    if (gamepads[i]) {
      onGamepadConnected({ gamepad: gamepads[i] });
      break;
    }
  }
}

/**
 * Public entry point to detect a gamepad (called from HTML button).
 */
function detectGamepad() {
  checkGamepadConnection();
  if (!gamepadConnected) {
    showStatus('No controller detected. Press any button on your controller.', false);
  }
  _scriptLog('⚠️ Missing your own function in CUSTOMCONTROL.processGamepadInput(gamepad).');
}

/**
 * Handle gamepad connection
 */
function onGamepadConnected(event) {
  const gamepad = event.gamepad;
  _scriptLog('Gamepad connected:', gamepad.id);
  
  gamepadConnected = true;
  gamepadIndex = gamepad.index;
  
  // Update UI
  updateGamepadStatus(true, gamepad.id);
  
  // Update SVG overlay if enabled
  _gpadOnConnect(gamepad);
  
  // Start polling loop
  if (!animationFrameId) {
    pollGamepad();
  }
}

/**
 * Handle gamepad disconnection
 */
function onGamepadDisconnected(event) {
  _scriptLog('Gamepad disconnected:', event.gamepad.id);
  
  gamepadConnected = false;
  gamepadIndex = -1;
  
  // Update UI
  updateGamepadStatus(false, '');
  
  // Update SVG overlay
  _gpadOnDisconnect();
  
  // Stop polling
  if (animationFrameId) {
    cancelAnimationFrame(animationFrameId);
    animationFrameId = null;
  }
  
  // Reset all button indicators
  Object.keys(XBOX_BUTTONS).forEach(btnName => {
    updateButtonIndicator(btnName, false);
  });
}



// ── Gamepad Polling ───────────────────────────────────────────────────────────

/**
 * Poll gamepad state with 50ms minimum interval throttling
 */
function pollGamepad() {
  if (!gamepadConnected) return;
  
  const now = performance.now();
  const timeSinceLastPoll = now - lastGamepadPollTime;
  
  // Only process if pollIntervalMs  has elapsed since last poll
  if (timeSinceLastPoll >= pollIntervalMs ) {
    const gamepads = navigator.getGamepads();
    const gamepad = gamepads[gamepadIndex];
    
    if (gamepad) {
      processGamepadInput(gamepad);
      _gpadUpdateSVG(gamepad);
    }
    
    lastGamepadPollTime = now;
  }
  
  // Continue polling
  animationFrameId = requestAnimationFrame(pollGamepad);
}


/**
 * Handle button press/release with callbacks
 */
function handleGamepaButton(gamepad, buttonIndex, buttonName, onPress, onRelease) {
  const button = gamepad.buttons[buttonIndex];
  const isPressed = button.pressed || button.value > 0.5;
  const wasPressed = buttonStates[buttonIndex];
  
  // Update indicator
  updateButtonIndicator(buttonName, isPressed);
  
  // Detect press (rising edge)
  if (isPressed && !wasPressed) {
    _scriptLog(`Button ${buttonName} pressed`);
    if (onPress) onPress();
  }
  
  // Detect release (falling edge)
  if (!isPressed && wasPressed) {
    _scriptLog(`Button ${buttonName} released`);
    if (onRelease) onRelease();
  }
  
  // Update state
  buttonStates[buttonIndex] = isPressed;
}



// ── Servo Control Functions ───────────────────────────────────────────────────

/**
 * Set servo angle (for angular servos).
 * Uses fire-and-forget over the WebSocket WS bridge so that rapid
 * joystick inputs are never queued behind a pending response.
 *
 * @param {number} channel - Servo channel (0-5)
 * @param {number} angle - Angle in degrees (0 to 270 depending on type)
 */
function setServoAngle(channel, angle) {
  setServo270(channel, angle); // For simplicity, use the 270° function which can handle both types
}

/**
 * Set a 270° servo to a precise angle using calibrated PWM endpoints.
 * Provides accurate 1:1 degree mapping (0° → 0, 270° → 270).
 * Uses fire-and-forget over the WebSocket WS bridge.
 *
 * @param {number} channel - Servo channel (0-5)
 * @param {number} angle   - Angle in degrees (0 to 270)
 */
function setServo270(channel, angle) {
  if (typeof isMasterRegistered !== 'undefined' && !isMasterRegistered) {
    return;
  }

  const clamped  = Math.max(0, Math.min(270, Math.round(angle)));
  const mask     = (1 << channel) & 0xFF;
  const angleHi  = (clamped >> 8) & 0xFF;
  const angleLo  = clamped & 0xFF;
  const packet   = new Uint8Array([MOTOR_SERVO_ACTION.SET_SERVO270_ANGLE, mask, angleHi, angleLo]);

  if (typeof sendWSFireAndForget !== 'undefined') {
    sendWSFireAndForget(packet);
  }
}

/**
 * Attach or detach a servo by configuring its type.
 * Uses fire-and-forget over the WebSocket bridge.
 *
 * @param {number} channel - Servo channel (0-5)
 * @param {number} type - Servo type ( 1=ANGULAR_270, 2=ROTATIONAL)
 */
function attachServo(channel, type) {
  
  if (typeof isMasterRegistered !== 'undefined' && !isMasterRegistered) {
    return;
  }

  // Build packet: SET_SERVO_TYPE [servo_mask:u8] [type:u8]
  const mask = (1 << channel) & 0xFF;
  const packet = new Uint8Array([MOTOR_SERVO_ACTION.SET_SERVO_TYPE, mask, type & 0xFF]);

  // Fire-and-forget (WS-like): no response awaited
  if (typeof sendWSFireAndForget !== 'undefined') {
    sendWSFireAndForget(packet);
  }
}

/**
 * Set servo speeds (for rotational servos).
 * Uses fire-and-forget over the WebSocket WS bridge so that rapid
 * joystick inputs are never queued behind a pending response.
 *
 * @param {Array<[number, number]>} pairs - Array of [channel, speed] pairs
 *   channel: servo channel number (0-5)
 *   speed: speed value (-100 to +100)
 * @example
 *   setServoSpeeds([[0, 100]]);           // servo 0 forward full speed
 *   setServoSpeeds([[0, 100], [1, -100]]); // servo 0 fwd, servo 1 rev
 */
function setServoSpeeds(pairs) {
  if (typeof isMasterRegistered !== 'undefined' && !isMasterRegistered) {
    return;
  }

  // Protocol: SET_SERVOS_SPEED [servo_mask:u8] [speed:i8] — one speed for all masked channels.
  // Group channels that share the same speed into one packet; send separate packets otherwise.
  const speedMap = new Map();
  for (const [channel, speed] of pairs) {
    speedMap.set(speed, (speedMap.get(speed) || 0) | (1 << channel));
  }

  if (typeof sendWSFireAndForget !== 'undefined') {
    speedMap.forEach((mask, speed) => {
      const packet = new Uint8Array([MOTOR_SERVO_ACTION.SET_SERVOS_SPEED, mask & 0xFF, speed & 0xFF]);
      sendWSFireAndForget(packet);
    });
  }
}

// ── UI Updates ────────────────────────────────────────────────────────────────

/**
 * Update gamepad connection status display
 */
function updateGamepadStatus(connected, name) {
  const statusElem = document.getElementById('gamepadStatus');
  const nameElem = document.getElementById('gamepadName');
  
  if (statusElem) {
    if (connected) {
      statusElem.textContent = 'Connected';
      statusElem.className = 'status-badge status-connected';
    } else {
      statusElem.textContent = 'Not Connected';
      statusElem.className = 'status-badge status-disconnected';
    }
  }
  
  if (nameElem) {
    nameElem.textContent = name || '—';
  }
}

/**
 * Update button indicator (visual feedback)
 */
function updateButtonIndicator(buttonName, pressed) {
  const elem = document.getElementById(`btn-${buttonName}`);
  if (elem) {
    elem.textContent = pressed ? '◉' : '◯';
    elem.style.color = pressed ? '#fff700ff' : '#666';
  }
}
