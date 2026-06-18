const state = { history: [], polling: null };
const $ = (id) => document.getElementById(id);

function clamp(v, min, max) { return Math.max(min, Math.min(max, Number(v))); }

async function api(url, options = {}) {
  const res = await fetch(url, { cache: 'no-store', ...options });
  if (!res.ok) throw new Error(await res.text());
  return res.json();
}

function setConnection(ok, text) {
  $('connection_state').textContent = text;
  $('connection_state').style.background = ok ? '#dcfce7' : '#fee2e2';
  $('connection_state').style.color = ok ? '#166534' : '#991b1b';
}

function updateRgbPreview() {
  const r = clamp($('rgb_r').value, 0, 255);
  const g = clamp($('rgb_g').value, 0, 255);
  const b = clamp($('rgb_b').value, 0, 255);
  const br = clamp($('rgb_brightness').value, 0, 100) / 100;
  $('rgb_preview').style.background = `rgb(${Math.round(r*br)},${Math.round(g*br)},${Math.round(b*br)})`;
}

function drawChart() {
  const canvas = $('temp_chart');
  const ctx = canvas.getContext('2d');
  const w = canvas.width = canvas.clientWidth * devicePixelRatio;
  const h = canvas.height = 130 * devicePixelRatio;
  ctx.clearRect(0,0,w,h);
  ctx.lineWidth = 2 * devicePixelRatio;
  ctx.strokeStyle = '#0a84ff';
  const data = state.history.slice(-40);
  if (data.length < 2) return;
  const min = Math.min(...data, 15), max = Math.max(...data, 45);
  ctx.beginPath();
  data.forEach((v, i) => {
    const x = (i / (data.length - 1)) * w;
    const y = h - ((v - min) / Math.max(1, max - min)) * h;
    if (i === 0) ctx.moveTo(x, y); else ctx.lineTo(x, y);
  });
  ctx.stroke();
}

function populateForm(s) {
  $('thermal_mode').value = s.thermal_mode;
  $('desired_temp_c').value = Number(s.desired_temp_c).toFixed(1);
  $('max_temp_c').value = Number(s.max_temp_c).toFixed(1);
  $('manual_fan_percent').value = s.manual_fan_percent;
  $('curtain_mode').value = s.curtain_mode;
  $('curtain_percent').value = s.curtain_percent;
  $('curtain_value').textContent = s.curtain_percent;
  $('rgb_r').value = s.rgb_r; $('rgb_g').value = s.rgb_g; $('rgb_b').value = s.rgb_b;
  $('rgb_brightness').value = s.rgb_brightness;
  updateRgbPreview();
}

function renderState(s) {
  $('temperature_reading').textContent = s.temperature_valid ? Number(s.temperature_c).toFixed(1) : '--';
  $('adc_raw').textContent = s.adc_raw;
  $('adc_mv').textContent = s.adc_mv;
  $('fan_percent').textContent = `${s.fan_percent}%`;
  $('alarm_state').textContent = s.alarm_active ? 'Activa' : 'OK';
  $('alarm_state').className = s.alarm_active ? 'rd' : 'gr';
  if (s.temperature_valid) state.history.push(Number(s.temperature_c));
  if (state.history.length > 80) state.history.shift();
  drawChart();
}

async function refreshState(initial = false) {
  try {
    const s = await api('/api/state');
    setConnection(true, s.time_synchronized ? 'Online · SNTP OK' : 'Online · sin SNTP');
    renderState(s);
    if (initial) populateForm(s);
  } catch (e) {
    setConnection(false, 'Sin conexión');
  }
}

async function saveControl(partial) {
  const payload = {
    thermal_mode: $('thermal_mode').value,
    desired_temp_c: Number($('desired_temp_c').value),
    max_temp_c: Number($('max_temp_c').value),
    manual_fan_percent: Number($('manual_fan_percent').value),
    curtain_mode: $('curtain_mode').value,
    curtain_percent: Number($('curtain_percent').value),
    rgb_r: Number($('rgb_r').value),
    rgb_g: Number($('rgb_g').value),
    rgb_b: Number($('rgb_b').value),
    rgb_brightness: Number($('rgb_brightness').value),
    ...partial
  };
  const s = await api('/api/control', { method: 'POST', headers: {'Content-Type':'application/json'}, body: JSON.stringify(payload) });
  renderState(s); populateForm(s);
}

async function refreshRegisters() {
  const data = await api('/read_regs.json');
  const list = $('registers_list');
  list.innerHTML = '';
  (data.registers || []).forEach((r) => {
    const row = document.createElement('div');
    row.className = 'register-row';
    row.innerHTML = `<strong>#${r.id}</strong><span>${r.label}</span>`;
    list.appendChild(row);
  });
}

function selectedDays() {
  return ['day_mon','day_tue','day_wed','day_thu','day_fri','day_sat','day_sun'].map(id => $(id).checked ? '1' : '0');
}

async function saveRegister() {
  await api('/regchange.json', { method:'POST', headers:{'Content-Type':'application/json'}, body: JSON.stringify({
    selectedNumber: $('selectNumber').value,
    hours: $('hours').value,
    minutes: $('minutes').value,
    curtain_percent: Number($('schedule_curtain_percent').value),
    selectedDays: selectedDays()
  })});
  await refreshRegisters();
}

async function eraseRegister() {
  await api('/regerase.json', { method:'POST', headers:{'Content-Type':'application/json'}, body: JSON.stringify({ selectedNumber: $('selectNumber').value })});
  await refreshRegisters();
}


async function saveApConfig() {
  const ssid = $('ap_ssid').value.trim();
  const password = $('ap_password').value;
  if (!ssid) { $('ap_config_status').innerHTML = '<span class="rd">El SSID del AP no puede estar vacío.</span>'; return; }
  try {
    await api('/api/ap_config', { method:'POST', headers:{'Content-Type':'application/json'}, body: JSON.stringify({ ap_ssid: ssid, ap_password: password })});
    $('ap_config_status').innerHTML = '<span class="gr">AP actualizado y guardado en NVS.</span>';
  } catch(e) {
    $('ap_config_status').innerHTML = '<span class="rd">No se pudo actualizar el AP.</span>';
  }
}

async function connectWifi() {
  $('wifi_connect_status').textContent = 'Conectando…';
  try {
    await api('/wifiConnect.json', { method:'POST', headers:{'Content-Type':'application/json'}, body: JSON.stringify({ selectedSSID: $('connect_ssid').value, pwd: $('connect_pass').value })});
    $('wifi_connect_status').innerHTML = '<span class="gr">Solicitud enviada. Revisa el estado en unos segundos.</span>';
  } catch(e) {
    $('wifi_connect_status').innerHTML = '<span class="rd">Error enviando credenciales.</span>';
  }
}

function getFileInfo() {
  const f = $('selected_file').files[0];
  $('file_info').textContent = f ? `${f.name} · ${f.size} bytes` : 'Sin archivo';
}

async function updateFirmware() {
  const f = $('selected_file').files[0];
  if (!f) { $('ota_update_status').textContent = 'Selecciona un .bin primero'; return; }
  const fd = new FormData(); fd.set('file', f, f.name);
  $('ota_update_status').textContent = 'Subiendo firmware…';
  const res = await fetch('/OTAupdate', { method:'POST', body: fd });
  $('ota_update_status').textContent = res.ok ? 'OTA recibida. El ESP32 reiniciará si fue válida.' : 'Error OTA';
}

async function refreshOtaStatus() {
  try {
    const s = await api('/OTAstatus', { method:'POST', body:'ota_update_status' });
    $('latest_firmware').textContent = `${s.compile_date} ${s.compile_time}`;
  } catch(e) {}
}

function init() {
  for (let i=1;i<=10;i++) $('selectNumber').insertAdjacentHTML('beforeend', `<option value="${i}">${i}</option>`);
  $('curtain_percent').addEventListener('input', () => $('curtain_value').textContent = $('curtain_percent').value);
  ['rgb_r','rgb_g','rgb_b','rgb_brightness'].forEach(id => $(id).addEventListener('input', updateRgbPreview));
  $('save_thermal').addEventListener('click', () => saveControl({}));
  $('save_curtain').addEventListener('click', () => saveControl({}));
  $('save_rgb').addEventListener('click', () => saveControl({}));
  $('save_register').addEventListener('click', saveRegister);
  $('erase_register').addEventListener('click', eraseRegister);
  $('connect_wifi').addEventListener('click', connectWifi);
  $('save_ap_config').addEventListener('click', saveApConfig);
  $('show_password').addEventListener('change', () => $('connect_pass').type = $('show_password').checked ? 'text' : 'password');
  $('select_file').addEventListener('click', () => $('selected_file').click());
  $('selected_file').addEventListener('change', getFileInfo);
  $('update_firmware').addEventListener('click', updateFirmware);
  refreshState(true); refreshRegisters(); refreshOtaStatus();
  state.polling = setInterval(() => { refreshState(false); refreshOtaStatus(); }, 2000);
}

document.addEventListener('DOMContentLoaded', init);
