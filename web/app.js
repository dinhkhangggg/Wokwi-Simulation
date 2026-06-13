import { initializeApp } from "https://www.gstatic.com/firebasejs/10.8.0/firebase-app.js";
import { getDatabase, ref, onValue, set, serverTimestamp } from "https://www.gstatic.com/firebasejs/10.8.0/firebase-database.js";

let latencyHistory = [];
const controlRequests = {};
let last_t1 = 0;
let last_t2 = 0;
let last_t3 = 0;

const firebaseConfig = {
  apiKey: "AIzaSyDUTppfzrKaS_nHiOG4Nb37aVkK1sCsYsQ",
  authDomain: "home-4a6c1.firebaseapp.com",
  databaseURL: "https://home-4a6c1-default-rtdb.firebaseio.com",
  projectId: "home-4a6c1",
  storageBucket: "home-4a6c1.firebasestorage.app",
  messagingSenderId: "211076875048",
  appId: "1:211076875048:web:8c17bca6e4755acaf3461d",
  measurementId: "G-9Z8QYT40LY"
};

const app = initializeApp(firebaseConfig);
const db = getDatabase(app);

// DOM Elements
const tempEl = document.getElementById('temp-value');
const humEl = document.getElementById('hum-value');
const mqEl = document.getElementById('mq-value');
const ldrEl = document.getElementById('ldr-value');
const ledToggle = document.getElementById('led-toggle');
const motorSlider = document.getElementById('motor-slider');
const motorValue = document.getElementById('motor-value');
const statusIndicator = document.getElementById('status-indicator');
const statusDot = document.getElementById('status-dot');
const statusText = document.getElementById('status-text');

let lastDataTimestamp = Date.now();

function checkConnectionStatus() {
    const timeSinceLastData = Date.now() - lastDataTimestamp;
    // 15 seconds timeout
    if (timeSinceLastData > 15000) {
        statusIndicator.classList.add('offline');
        statusDot.classList.add('offline');
        statusText.classList.add('offline');
        statusText.innerText = "SYSTEM OFFLINE";
    } else {
        statusIndicator.classList.remove('offline');
        statusDot.classList.remove('offline');
        statusText.classList.remove('offline');
        statusText.innerText = "SYSTEM ONLINE";
    }
}
setInterval(checkConnectionStatus, 2000);

// Helper to flash cards on update
function flashCard(element) {
    // Find the closest parent with 'card' class
    const card = element.closest('.card');
    if (!card) return;
    
    card.classList.remove('update-flash');
    void card.offsetWidth; // Trigger reflow to restart animation
    card.classList.add('update-flash');
}

// Listen to sensors
onValue(ref(db, 'sensors/temperature'), (snapshot) => {
    tempEl.innerText = snapshot.exists() ? snapshot.val() : '--';
    flashCard(tempEl);
});

onValue(ref(db, 'sensors/humidity'), (snapshot) => {
    humEl.innerText = snapshot.exists() ? snapshot.val() : '--';
    flashCard(humEl);
});

onValue(ref(db, 'sensors/mq'), (snapshot) => {
    mqEl.innerText = snapshot.exists() ? snapshot.val() : '--';
    flashCard(mqEl);
});

onValue(ref(db, 'sensors/ldr'), (snapshot) => {
    ldrEl.innerText = snapshot.exists() ? snapshot.val() : '--';
    flashCard(ldrEl);
});

// Log data to the console
onValue(ref(db, 'sensors'), (snapshot) => {
    if (snapshot.exists()) {
        const data = snapshot.val();
        const web_time = Date.now();
        lastDataTimestamp = web_time; // update health timestamp
        checkConnectionStatus(); // instantly trigger UI update
        
        // Only print actual sensor data if it contains temperature
        if (data.temperature !== undefined) {
            if (data.server_time && data.esp_time)
            {
                const t3 = Math.max(0, web_time - data.server_time);

                if (t3 > 10000) {
                    console.log("[Firebase RECEIVE] Bỏ qua dữ liệu cũ (t3 = " + t3 + " ms)");
                    return;
                }

                const t1 = data.t1 || 0;
                const t2 = data.t2 || 0;
                
                last_t1 = t1;
                last_t2 = t2;
                last_t3 = t3;

                const total = t1 + t2 + t3;

                latencyHistory.push(total);

                if (latencyHistory.length > 20)
                {
                    latencyHistory.shift();
                }

                const avg =
                    latencyHistory.reduce(
                        (a, b) => a + b,
                        0
                    ) / latencyHistory.length;

                const min =
                    Math.min(...latencyHistory);

                const max =
                    Math.max(...latencyHistory);

                set(ref(db, 'control/monitoring_report'), {
                    t1: t1,
                    t2: t2,
                    t3: Math.round(t3),
                    total: Math.round(total)
                });

                console.log(`
========================
t2 Upload   : ${t2.toFixed(0)} ms
t3 Download : ${t3.toFixed(0)} ms
Total       : ${total.toFixed(0)} ms

AVG         : ${avg.toFixed(1)} ms
MIN         : ${min.toFixed(1)} ms
MAX         : ${max.toFixed(1)} ms
========================
`);
            }
            console.log(`[Data] Temp: ${data.temperature}°C | Hum: ${data.humidity}% | MQ: ${data.mq} | LDR: ${data.ldr}`);
            // Format esp_time to remove scientific notation for display purposes
            const displayData = { ...data };
            if (displayData.esp_time) {
                displayData.esp_time = Math.round(displayData.esp_time);
            }
            console.log("[Firebase RECEIVE] Raw JSON:", JSON.stringify(displayData, null, 2));
        }
    }
});

// Update initial states from DB for controls
onValue(ref(db, 'control/led'), (snapshot) => {
    if (snapshot.exists()) {
        const data = snapshot.val();
        ledToggle.checked = !!(typeof data === 'object' ? data.value : data);
    }
});

onValue(ref(db, 'control/motor'), (snapshot) => {
    if (snapshot.exists()) {
        const data = snapshot.val();
        const val = typeof data === 'object' ? data.value : data;
        motorSlider.value = val;
        motorValue.innerText = `${val}°`;
    }
});

// Control Events
ledToggle.addEventListener('change', (e) => {
    const cmd_id = Date.now() + Math.floor(Math.random() * 1000);
    controlRequests[cmd_id] = performance.now();
    
    const payload = {
        value: e.target.checked,
        cmd_id: cmd_id
    };
    console.log("[Firebase SEND] /control/led JSON:", payload);
    const send_time = performance.now();
    set(ref(db, 'control/led'), payload).then(() => {
        controlRequests[cmd_id + "_t4"] = performance.now() - send_time;
    });
});

motorSlider.addEventListener('input', (e) => {
    motorValue.innerText = `${e.target.value}°`;
});

motorSlider.addEventListener('change', (e) => {
    const cmd_id = Date.now() + Math.floor(Math.random() * 1000);
    controlRequests[cmd_id] = performance.now();
    
    const angle = parseInt(e.target.value);
    const payload = {
        value: angle,
        cmd_id: cmd_id
    };
    console.log("[Firebase SEND] /control/motor JSON:", payload);
    const send_time = performance.now();
    set(ref(db, 'control/motor'), payload).then(() => {
        controlRequests[cmd_id + "_t4"] = performance.now() - send_time;
    });
});

// Lắng nghe ACK từ ESP32 để tính thời gian t4 (Dashboard -> Actuator)
onValue(ref(db, 'control/ack'), (snapshot) => {
    if (snapshot.exists()) {
        const data = snapshot.val();
        if (data && data.cmd_id && controlRequests[data.cmd_id]) {
            const end_time = performance.now();
            const start_time = controlRequests[data.cmd_id];
            const total_rtt = end_time - start_time;
            
            // Độ trễ 1 chiều t4 = Tổng thời gian khứ hồi / 2 (Giả định mạng 2 chiều tương đương)
            const t4 = total_rtt / 2.0;
            const comm = (last_t1 + last_t2 + last_t3 + t4) / 2.0;
            
            set(ref(db, 'control/latency_report'), {
                t4: Math.round(t4),
                comm: Math.round(comm)
            });
            
            delete controlRequests[data.cmd_id];
            delete controlRequests[data.cmd_id + "_t4"];
        }
    }
});
