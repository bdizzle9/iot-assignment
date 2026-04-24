"""
COM3505 – IoT Assignment
Python Flask Server

Architecture:
  ESP32  --POST /data-->  Flask server  <--poll /api/data--  Browser
  ESP32  <--GET /pattern-- Flask server  <--POST /api/pattern-- Browser

Run:
  pip install flask
  python server.py

The ESP32 should be updated to POST JSON to http://<server-ip>:5000/data
every 2-5 seconds, and GET http://<server-ip>:5000/pattern to check for
pattern change commands from the browser.
"""

from flask import Flask, request, jsonify, render_template_string
from collections import deque
from datetime import datetime
import threading

app = Flask(__name__)

# ── Shared state (thread-safe with a lock) ──────────────────────────────────
lock = threading.Lock()

state = {
    "temperature": None,          # latest reading from ESP32
    "pattern": 0,                 # current LED pattern index
    "pattern_name": "Off",        # human-readable name
    "last_seen": None,            # ISO timestamp of last ESP32 POST
}

PATTERN_NAMES = ["Off", "Blink", "Chase", "Pulse", "Temp Sensitive", "Temp Binary"]

# Rolling 60-point history (one entry per ESP32 POST, ~2 s apart → ~2 min)
history: deque = deque(maxlen=60)


# ── ESP32 endpoints ──────────────────────────────────────────────────────────

@app.route("/data", methods=["POST"])
def receive_data():
    """
    ESP32 POSTs JSON: {"temperature": 23.4}
    Server stores it and returns the current pattern so the ESP32 can act on it.
    """
    payload = request.get_json(silent=True) or {}
    temp = payload.get("temperature")

    with lock:
        if temp is not None:
            state["temperature"] = round(float(temp), 1)
            state["last_seen"] = datetime.now().strftime("%H:%M:%S")
            history.append({
                "time": state["last_seen"],
                "temperature": state["temperature"],
            })

    with lock:
        response = {
            "pattern": state["pattern"],
            "pattern_name": state["pattern_name"],
        }
    return jsonify(response), 200


@app.route("/pattern", methods=["GET"])
def get_pattern():
    """
    ESP32 polls this endpoint to pick up pattern changes set via the browser.
    Returns: {"pattern": 2, "pattern_name": "Chase"}
    """
    with lock:
        return jsonify({
            "pattern": state["pattern"],
            "pattern_name": state["pattern_name"],
        }), 200


# ── Browser / AJAX endpoints ─────────────────────────────────────────────────

@app.route("/api/data", methods=["GET"])
def api_data():
    """Browser polls this every 2 s for live sensor + pattern state."""
    with lock:
        return jsonify({
            "temperature": state["temperature"],
            "pattern": state["pattern"],
            "pattern_name": state["pattern_name"],
            "last_seen": state["last_seen"],
            "history": list(history),
        }), 200


@app.route("/api/pattern", methods=["POST"])
def api_set_pattern():
    """
    Browser POSTs {"pattern": 2} to change the LED pattern.
    The new value is stored here; the ESP32 picks it up on its next /pattern poll.
    """
    payload = request.get_json(silent=True) or {}
    pid = int(payload.get("pattern", 0))

    if 0 <= pid < len(PATTERN_NAMES):
        with lock:
            state["pattern"] = pid
            state["pattern_name"] = PATTERN_NAMES[pid]
        return jsonify({"ok": True, "pattern": pid, "pattern_name": PATTERN_NAMES[pid]}), 200

    return jsonify({"ok": False, "error": "Invalid pattern id"}), 400


# ── Dashboard web page ────────────────────────────────────────────────────────

DASHBOARD_HTML = """
<!DOCTYPE html>
<html>
<head>
  <title>ESP32</title>
  <meta charset='utf-8'>
  <meta name='viewport' content='width=device-width, initial-scale=1.0'>
  <style>body{font-family:sans-serif;max-width:500px;margin:20px auto;padding:0 12px}canvas{width:100%}button{margin:4px 2px;padding:6px 12px;cursor:pointer}button.active{font-weight:bold;text-decoration:underline}</style>
</head>
<body>
  <h2>ESP32 Dashboard</h2>

  <p id='esp-alert' style='display:none'></p>

  <p>Temperature: <b id='temp'>--</b> &deg;C<br>
  LED Pattern: <span id='pname'>--</span><br>
  Last seen: <span id='lastseen'>--</span></p>

  <p><b>Temperature History</b></p>
  <canvas id='chart' height='200'></canvas>

  <p><b>LED Pattern</b></p>
  <div>
    <button class='pattern-btn' onclick='setPattern(0)'>Off</button>
    <button class='pattern-btn' onclick='setPattern(1)'>Blink</button>
    <button class='pattern-btn' onclick='setPattern(2)'>Chase</button>
    <button class='pattern-btn' onclick='setPattern(3)'>Pulse</button>
    <button class='pattern-btn' onclick='setPattern(4)'>Temp</button>
    <button class='pattern-btn' onclick='setPattern(5)'>Temp Binary</button>
  </div>

  <p id='binary-info' style='display:none'><small>
    <b>Temp Binary</b> shows the temperature as a 3-bit signed offset from 20&deg;C.<br>
    LED1 = sign bit (lit = below 20&deg;C), LED2 = +2&deg;C, LED3 = +1&deg;C.<br>
    Range: 16&deg;C (100) &rarr; 20&deg;C (000) &rarr; 23&deg;C+ (011).
  </small></p>

  <p><small>Updating every 2s &mdash; last poll: <span id='polltime'>--</span></small></p>

<script>
  // ── Chart (plain canvas, no library) ──────────────────────────────────────
  const canvas = document.getElementById('chart');
  const ctx    = canvas.getContext('2d');
  let tempHistory = [];
  const MAX = 60;

  function drawChart() {
    const W = canvas.offsetWidth  || 600;
    const H = canvas.offsetHeight || 220;
    canvas.width  = W;
    canvas.height = H;
    const PAD = { top: 20, right: 20, bottom: 30, left: 46 };

    ctx.clearRect(0, 0, W, H);

    if (tempHistory.length < 2) {
      ctx.fillStyle = '#999';
      ctx.font = '13px sans-serif';
      ctx.textAlign = 'center';
      ctx.fillText('Waiting for data…', W / 2, H / 2);
      return;
    }

    const vals   = tempHistory.map(p => p.temperature);
    const rawMin = Math.min(...vals);
    const rawMax = Math.max(...vals);
    const pad    = (rawMax - rawMin) < 2 ? 2 : (rawMax - rawMin) * 0.15;
    const minT   = rawMin - pad;
    const maxT   = rawMax + pad;
    const range  = maxT - minT || 1;

    const plotW = W - PAD.left - PAD.right;
    const plotH = H - PAD.top  - PAD.bottom;

    const toX = i   => PAD.left + (i / (tempHistory.length - 1)) * plotW;
    const toY = val => PAD.top  + (1 - (val - minT) / range) * plotH;

    // grid lines + y labels
    ctx.strokeStyle = '#ddd';
    ctx.lineWidth   = 1;
    ctx.fillStyle   = '#666';
    ctx.font        = '11px sans-serif';
    ctx.textAlign   = 'right';
    const STEPS = 4;
    for (let i = 0; i <= STEPS; i++) {
      const v = minT + (i / STEPS) * range;
      const y = toY(v);
      ctx.beginPath();
      ctx.moveTo(PAD.left, y);
      ctx.lineTo(W - PAD.right, y);
      ctx.stroke();
      ctx.fillText(v.toFixed(1) + '°', PAD.left - 6, y + 4);
    }

    // x-axis time labels (first, middle, last)
    ctx.fillStyle = '#666';
    ctx.textAlign = 'center';
    const labelIdxs = [0, Math.floor((tempHistory.length-1)/2), tempHistory.length-1];
    labelIdxs.forEach(i => {
      if (tempHistory[i]) {
        ctx.fillText(tempHistory[i].time, toX(i), H - 6);
      }
    });

    // filled area under line
    ctx.beginPath();
    ctx.moveTo(toX(0), toY(tempHistory[0].temperature));
    tempHistory.forEach((p, i) => ctx.lineTo(toX(i), toY(p.temperature)));
    ctx.lineTo(toX(tempHistory.length - 1), H - PAD.bottom);
    ctx.lineTo(toX(0), H - PAD.bottom);
    ctx.closePath();
    ctx.fillStyle = 'rgba(0,0,200,0.08)';
    ctx.fill();

    // line
    ctx.beginPath();
    ctx.strokeStyle = '#0044cc';
    ctx.lineWidth   = 2;
    ctx.lineJoin    = 'round';
    tempHistory.forEach((p, i) => {
      if (i === 0) ctx.moveTo(toX(i), toY(p.temperature));
      else         ctx.lineTo(toX(i), toY(p.temperature));
    });
    ctx.stroke();

    // latest dot
    const last = tempHistory[tempHistory.length - 1];
    ctx.beginPath();
    ctx.arc(toX(tempHistory.length - 1), toY(last.temperature), 4, 0, Math.PI * 2);
    ctx.fillStyle = '#0044cc';
    ctx.fill();
  }

  // ── Polling ───────────────────────────────────────────────────────────────
  let currentPattern = -1;

  function poll() {
    fetch('/api/data')
      .then(r => r.json())
      .then(d => {
        document.getElementById('temp').textContent =
          d.temperature !== null ? d.temperature : '--';
        document.getElementById('pname').textContent = d.pattern_name || '--';
        document.getElementById('lastseen').textContent = d.last_seen || '--';

        // show alert if ESP32 has never sent data
        const alert = document.getElementById('esp-alert');
        if (d.temperature === null) {
          alert.style.display = 'block';
          alert.innerHTML = '<b>No data received from the ESP32 yet.</b><br>' +
            'Check that the ESP32 is powered on and connected to Wi-Fi.<br>' +
            '<small>If you just provisioned it via the ESP32-Setup hotspot, ' +
            'make sure you have now reconnected this device to your normal Wi-Fi network &mdash; ' +
            'the same one the ESP32 joined. You cannot reach this server while connected to the ESP32-Setup hotspot.</small>';
        } else {
          alert.style.display = 'none';
        }

        // rebuild history from server
        if (d.history && d.history.length) {
          tempHistory = d.history.slice(-MAX);
          drawChart();
        }

        // sync active button
        if (d.pattern !== currentPattern) {
          currentPattern = d.pattern;
          document.querySelectorAll('.pattern-btn').forEach((btn, i) => {
            btn.classList.toggle('active', i === currentPattern);
          });
          document.getElementById('binary-info').style.display =
            currentPattern === 5 ? 'block' : 'none';
        }

        document.getElementById('polltime').textContent =
          new Date().toLocaleTimeString();
      })
      .catch(() => {
        document.getElementById('polltime').textContent = 'error – retrying…';
      });
  }

  function setPattern(id) {
    fetch('/api/pattern', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ pattern: id }),
    }).then(() => poll());
  }

  poll();
  setInterval(poll, 2000);
  window.addEventListener('resize', drawChart);
</script>
</body>
</html>
"""


@app.route("/")
def dashboard():
    return render_template_string(DASHBOARD_HTML)


# ── Run ───────────────────────────────────────────────────────────────────────
if __name__ == "__main__":
    print("=" * 55)
    print("  COM3505 Flask IoT Server")
    print("  Dashboard : http://0.0.0.0:5000/")
    print("  ESP32 POST: http://<this-ip>:5000/data")
    print("  ESP32 GET : http://<this-ip>:5000/pattern")
    print("=" * 55)
    app.run(host="0.0.0.0", port=5000, debug=True)
