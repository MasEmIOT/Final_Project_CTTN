// api.js - Lop giao tiep REST voi Local Server tren Gateway (ESP32).
// Base URL = http://<IP gateway>. Luu trong localStorage de nho giua cac lan mo.

const LS = { base: 'lf_base', role: 'lf_role', token: 'lf_token', user: 'lf_user' };

// Ma lenh downlink (khop packet.h ben firmware)
export const CMD = { ACT_SET: 1, ACT_AUTO: 2, OTA: 3, REBOOT: 4, PING: 5 };
export const ACT = { FAN: 1, MIST: 2, AUX: 4 };   // Act1, Act2, Act3

export const getBase = () => localStorage.getItem(LS.base) || '';
export const setBase = (b) => localStorage.setItem(LS.base, (b || '').replace(/\/+$/, ''));
export const getRole = () => localStorage.getItem(LS.role) || '';
export const getUser = () => localStorage.getItem(LS.user) || '';
export const getToken = () => localStorage.getItem(LS.token) || '';
export const isAdmin = () => getRole() === 'Admin';
export function saveSession(base, user, role, token) {
  setBase(base);
  localStorage.setItem(LS.user, user);
  localStorage.setItem(LS.role, role);
  localStorage.setItem(LS.token, token || '');
}
export function logout() {
  [LS.role, LS.token, LS.user].forEach((k) => localStorage.removeItem(k));
}

async function jget(path) {
  const r = await fetch(getBase() + path, { headers: { Accept: 'application/json' } });
  if (!r.ok) throw new Error('HTTP ' + r.status);
  return r.json();
}

export const apiStatus = () => jget('/api/status');
export const apiNodes = () => jget('/api/nodes');
export const apiHistory = (node) => jget('/api/history?node=' + node);

export async function apiLogin(base, user, pass) {
  const r = await fetch(base.replace(/\/+$/, '') + '/api/login', {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify({ user, pass }),
  });
  if (!r.ok) throw new Error('HTTP ' + r.status);
  return r.json(); // { role }
}

export async function apiCmd({ node, cmd, act_mask = 0, act_val = 0, url = '' }) {
  const r = await fetch(getBase() + '/api/cmd', {
    method: 'POST',
    headers: { 'Content-Type': 'application/json', 'X-Token': getToken() },
    body: JSON.stringify({ node, cmd, act_mask, act_val, url }),
  });
  const j = await r.json().catch(() => ({ ok: false, err: 'bad response' }));
  if (!r.ok && r.status === 403) return { ok: false, err: 'Cần quyền Admin' };
  return j;
}
