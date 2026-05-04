let charts = {};

const maxDataPoints = 30;
const FALLBACK_API_BASE_URL = 'http://192.168.137.250';
const { protocol, hostname } = window.location;
const API_BASE_URL = (['http:', 'https:'].includes(protocol) && !['localhost', '127.0.0.1', '0.0.0.0'].includes(hostname))
  ? `${protocol}//${hostname}`
  : FALLBACK_API_BASE_URL;

const thermometerKeys = ['temperature1', 'temperature2', 'temperature3'];
const thermometerLabelIds = ['tempLabel1', 'tempLabel2', 'tempLabel3'];
const levelSensorKeys = ['level1', 'level2', 'level3'];
const levelSensorLabelIds = ['levelStatus1', 'levelStatus2', 'levelStatus3'];
const levelActionIds = ['levelAction1', 'levelAction2', 'levelAction3'];
const levelSensorNames = ['Tank 1', 'Tank 2', 'Tank 3'];
const pumpBtnMap = [
  { key: 'mainPump', btnId: 'toggleMainPump' },
  { key: 'airPump', btnId: 'toggleAirPump' },
  { key: 'smallPump1', btnId: 'toggleWaterPump1' },
  { key: 'smallPump2', btnId: 'toggleWaterPump2' }
];

const pumpToggleState = {};
const pumpToggleBusy = {};
const pumpControlConfig = {
  mainPump: { params: ['mainPump'], channel: 1, stateKeys: ['mainPump'] },
  airPump: { params: ['airPump'], channel: 2, stateKeys: ['airPump'] },
  smallPump1: { params: ['smallPump1', 'waterPump1'], channel: 3, stateKeys: ['smallPump1', 'waterPump1'] },
  smallPump2: { params: ['smallPump2', 'waterPump2'], channel: 4, stateKeys: ['smallPump2', 'waterPump2'] }
};

let eStopState = false;

function baseLineChartOptions(yMax, yStep, xTicks = 5) {
  return {
    responsive: true,
    maintainAspectRatio: false,
    plugins: { legend: { display: true, position: 'bottom' }, filler: { propagate: true } },
    scales: {
      y: { beginAtZero: true, ...(yMax !== undefined ? { max: yMax } : {}), ...(yStep !== undefined ? { ticks: { stepSize: yStep } } : {}) },
      x: { ticks: { maxTicksLimit: xTicks } }
    }
  };
}

function makeChart(id, type, dataset, options) {
  return new Chart(document.getElementById(id), {
    type,
    data: { labels: [], datasets: [dataset] },
    options
  });
}

function createLineChart(canvasId, label, borderColor, backgroundColor, options) {
  return makeChart(canvasId, 'line', { label, data: [], borderColor, backgroundColor, borderWidth: 2, tension: 0.1, fill: true }, options);
}

function initializeCharts() {
  [
    { key: 'temperature1', id: 'temperatureCanvas1', color: 'rgb(74, 144, 226)' },
    { key: 'temperature2', id: 'temperatureCanvas2', color: 'rgb(56, 117, 215)' },
    { key: 'temperature3', id: 'temperatureCanvas3', color: 'rgb(40, 92, 182)' }
  ].forEach((cfg, index) => {
    charts[cfg.key] = new Chart(document.getElementById(cfg.id), {
      type: 'bar',
      data: {
        labels: ['Now'],
        datasets: [{
          label: `Temp ${index + 1} (°C)`,
          data: [0],
          backgroundColor: cfg.color,
          borderColor: cfg.color,
          borderWidth: 1,
          borderRadius: 4,
          barPercentage: 0.9,
          categoryPercentage: 1
        }]
      },
      options: {
        responsive: true,
        maintainAspectRatio: false,
        plugins: { legend: { display: false } },
        scales: { x: { display: false, grid: { display: false } }, y: { beginAtZero: true, max: 50, ticks: { stepSize: 10 } } }
      }
    });
  });

  charts.airPump = new Chart(document.getElementById('airPumpCanvas'), {
    type: 'doughnut',
    data: {
      labels: ['Active', 'Inactive'],
      datasets: [{ data: [0, 100], backgroundColor: ['rgba(0, 147, 121, 0.8)', 'rgba(184, 184, 184, 0.3)'], borderWidth: 0 }]
    },
    options: { responsive: true, maintainAspectRatio: false, plugins: { legend: { display: true, position: 'bottom' } } }
  });

  charts.flowSensor = new Chart(document.getElementById('flowSensorCanvas'), {
    type: 'bar',
    data: { labels: [], datasets: [{ label: 'Flow Rate (L/min)', data: [], backgroundColor: 'rgba(75, 150, 255, 0.8)', borderColor: 'rgb(75, 150, 255)', borderWidth: 1 }] },
    options: { indexAxis: 'y', responsive: true, maintainAspectRatio: false, plugins: { legend: { display: true, position: 'bottom' } }, scales: { x: { ticks: { maxTicksLimit: 5 } } } }
  });

  charts.waterPump1 = createLineChart('waterPump1Canvas', 'Pump 1 (RPM)', 'rgb(1, 109, 90)', 'rgba(1, 109, 90, 0.1)', baseLineChartOptions());
  charts.waterPump2 = createLineChart('waterPump2Canvas', 'Pump 2 (RPM)', 'rgb(1, 109, 90)', 'rgba(1, 109, 90, 0.1)', baseLineChartOptions());
}

function addDataPoint(chartKey, value, label) {
  const chart = charts[chartKey];
  if (!chart) return;
  if (thermometerKeys.includes(chartKey)) {
    chart.data.datasets[0].data = [value];
    chart.update();
    return;
  }
  const timestamp = label || new Date().toLocaleTimeString();
  if (chart.data.labels.length >= maxDataPoints) chart.data.labels.shift();
  if (chart.data.datasets[0].data.length >= maxDataPoints) chart.data.datasets[0].data.shift();
  chart.data.labels.push(timestamp);
  chart.data.datasets[0].data.push(value);
  chart.update();
}

const toBoolean = (value) => typeof value === 'boolean' ? value : typeof value === 'number' ? value !== 0 : ['true', '1', 'on'].includes(String(value).toLowerCase());

function dualUrls(path) {
  return [`${API_BASE_URL}${path}`, `${FALLBACK_API_BASE_URL}${path}`];
}

async function fetchFirstOk(urls, method = 'GET') {
  for (const url of urls) {
    try {
      const resp = await fetch(url, { method });
      if (resp.ok) return resp;
    } catch (_) {
      // Try next URL.
    }
  }
  return null;
}

async function tryRequest(urls, methods = ['POST', 'GET']) {
  for (const method of methods) {
    if (await fetchFirstOk(urls, method)) return true;
  }
  return false;
}

function updatePumpButton(btnId, isOn) {
  const btn = document.getElementById(btnId);
  if (!btn) return;
  btn.textContent = isOn ? 'ON' : 'OFF';
  btn.classList.toggle('pump-toggle-on', isOn);
  btn.classList.toggle('pump-toggle-off', !isOn);
  btn.classList.add('pump-toggle-btn');
  btn.setAttribute('aria-pressed', String(isOn));
}

async function togglePump(pumpParam, btnId) {
  if (pumpToggleBusy[pumpParam]) return;
  const config = pumpControlConfig[pumpParam];
  if (!config) return;

  const previousState = !!pumpToggleState[pumpParam];
  const newState = !previousState;
  const stateText = newState ? 'on' : 'off';
  pumpToggleBusy[pumpParam] = true;
  pumpToggleState[pumpParam] = newState;
  updatePumpButton(btnId, newState);

  try {
    const pumpUrls = config.params.flatMap((param) => dualUrls(`/api/pumps?${param}=${stateText}`));
    let success = await tryRequest(pumpUrls);
    if (!success) success = await tryRequest(dualUrls(`/api/relay?channel=${config.channel}&state=${stateText}`));
    if (!success) throw new Error('Pump API did not accept request');
    await syncPumpStates();
  } catch (_) {
    pumpToggleState[pumpParam] = previousState;
    updatePumpButton(btnId, previousState);
  } finally {
    pumpToggleBusy[pumpParam] = false;
  }
}

async function syncPumpStates() {
  const response = await fetchFirstOk(dualUrls('/api/relays'));
  if (!response) return;

  const ch = (await response.json()).channels || {};
  pumpBtnMap.forEach(({ key, btnId }) => {
    if (pumpToggleBusy[key]) return;
    const stateValue = pumpControlConfig[key].stateKeys.map((k) => ch[k]).find((v) => v !== undefined);
    if (stateValue === undefined) return;
    pumpToggleState[key] = toBoolean(stateValue);
    updatePumpButton(btnId, pumpToggleState[key]);
  });
}

function updateDoughnutChart(chartKey, activeValue, inactiveValue) {
  const chart = charts[chartKey];
  if (!chart) return;
  chart.data.datasets[0].data = [activeValue, inactiveValue];
  chart.update();
}

function resolveTemperatures(data) {
  const values = { temperature1: data.temperature1, temperature2: data.temperature2, temperature3: data.temperature3 };
  const byId = data.temperatures && typeof data.temperatures === 'object' ? data.temperatures : null;
  const ids = byId ? Object.keys(byId) : [];

  for (let i = 0; i < Math.min(ids.length, 3); i++) values[thermometerKeys[i]] = byId[ids[i]];

  thermometerLabelIds.forEach((labelId, index) => {
    const labelEl = document.getElementById(labelId);
    if (!labelEl) return;
    if (byId) {
      labelEl.textContent = ids[index] ? `Sensor ${index + 1} (ID ...${ids[index].slice(-6)})` : `Sensor ${index + 1} (waiting...)`;
      return;
    }
    const value = values[thermometerKeys[index]];
    labelEl.textContent = (value === null || value === undefined || Number.isNaN(Number(value))) ? `Sensor ${index + 1} (no data)` : `Sensor ${index + 1}`;
  });

  return values;
}

function updateLevelStatus(index, rawValue) {
  const statusEl = document.getElementById(levelSensorLabelIds[index]);
  if (!statusEl) return;
  const isUnknown = rawValue === undefined || rawValue === null;
  const isFull = rawValue === true || rawValue === 'true' || rawValue === 1 || rawValue === '1';

  statusEl.textContent = isUnknown ? 'Waiting…' : (isFull ? 'Full' : 'Not full');
  statusEl.classList.toggle('level-status-on', !isUnknown && isFull);
  statusEl.classList.toggle('level-status-off', !isUnknown && !isFull);
  statusEl.classList.toggle('level-status-unknown', isUnknown);
}

function updateLevelAction(index, actionText) {
  const actionEl = document.getElementById(levelActionIds[index]);
  if (!actionEl) return;
  actionEl.textContent = actionText === undefined || actionText === null ? 'Waiting…' : actionText;
}

function resolveLevelStatuses(data) {
  const values = {
    level1: data.level1,
    level2: data.level2,
    level3: data.level3
  };

  if (data.levelSensors && typeof data.levelSensors === 'object') {
    values.level1 = data.levelSensors.tank1 ?? values.level1;
    values.level2 = data.levelSensors.tank2 ?? values.level2;
    values.level3 = data.levelSensors.tank3 ?? values.level3;
  }

  const actionValues = {
    level1: 'Waiting…',
    level2: 'Waiting…',
    level3: 'Waiting…'
  };

  if (data.levelActions && typeof data.levelActions === 'object') {
    actionValues.level1 = data.levelActions.tank1 ?? actionValues.level1;
    actionValues.level2 = data.levelActions.tank2 ?? actionValues.level2;
    actionValues.level3 = data.levelActions.tank3 ?? actionValues.level3;
  } else {
    const mainPumpBlocked = data.mainPumpBlocked === true || data.mainPumpBlocked === 'true' || data.mainPumpBlocked === 1 || data.mainPumpBlocked === '1';
    const small1Active = data.autoPump1Active === true || data.autoPump1Active === 'true' || data.autoPump1Active === 1 || data.autoPump1Active === '1';
    const small2Active = data.autoPump2Active === true || data.autoPump2Active === 'true' || data.autoPump2Active === 1 || data.autoPump2Active === '1';

    actionValues.level1 = mainPumpBlocked ? 'Main pump blocked' : 'Main tank OK';
    actionValues.level2 = small1Active ? 'Returning to main' : (values.level2 ? 'Full' : 'Ready');
    actionValues.level3 = small2Active ? 'Returning to main' : (values.level3 ? 'Full' : 'Ready');
  }

  levelSensorLabelIds.forEach((labelId, index) => {
    const rawValue = values[levelSensorKeys[index]];
    updateLevelStatus(index, rawValue);
    updateLevelAction(index, actionValues[levelSensorKeys[index]]);
  });

  return values;
}

async function fetchSensorData() {
  const response = await fetchFirstOk(dualUrls('/api/sensors'));
  if (!response) return;

  const data = await response.json();
  const timestamp = new Date().toLocaleTimeString();

  resolveLevelStatuses(data);

  const temperatures = resolveTemperatures(data);
  thermometerKeys.forEach((key) => {
    const value = temperatures[key];
    if (value !== undefined && !Number.isNaN(Number(value))) addDataPoint(key, Number(value), timestamp);
  });

  if (data.airPump !== undefined) updateDoughnutChart('airPump', data.airPump, 100 - data.airPump);
  if (data.flowSensor !== undefined) addDataPoint('flowSensor', data.flowSensor, timestamp);
  if (data.waterPump1 !== undefined) addDataPoint('waterPump1', data.waterPump1, timestamp);
  if (data.waterPump2 !== undefined) addDataPoint('waterPump2', data.waterPump2, timestamp);
}

function updateEStopButton(isActive) {
  eStopState = isActive;
  const btn = document.getElementById('eStopBtn');
  if (!btn) return;

  btn.classList.toggle('estop-active', isActive);
  btn.classList.toggle('estop-inactive', !isActive);
  btn.textContent = isActive ? '⚠ RESUME (E-Stop ON)' : '⚠ EMERGENCY STOP';

  pumpBtnMap.forEach(({ btnId }) => {
    const el = document.getElementById(btnId);
    if (el) el.disabled = isActive;
  });
}

async function syncEStopState() {
  const response = await fetchFirstOk(dualUrls('/api/estop'));
  if (!response) return;
  updateEStopButton(!!(await response.json()).eStopActive);
}

async function toggleEStop() {
  const newState = !eStopState;
  updateEStopButton(newState);

  const success = await tryRequest(dualUrls(`/api/estop?active=${newState ? 'on' : 'off'}`));
  if (!success) {
    updateEStopButton(!newState);
    return;
  }

  if (newState) {
    pumpBtnMap.forEach(({ key, btnId }) => {
      pumpToggleState[key] = false;
      updatePumpButton(btnId, false);
    });
  }
}
document.addEventListener('DOMContentLoaded', () => {
  initializeCharts();
  syncPumpStates();
  syncEStopState();
  fetchSensorData();
  setInterval(syncPumpStates, 1200);
  setInterval(syncEStopState, 1200);
  setInterval(fetchSensorData, 2000);
});
