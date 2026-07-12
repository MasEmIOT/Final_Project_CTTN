import { useState } from 'react'
import { apiLogin, saveSession, getBase } from '../api'

export default function Login({ onDone }) {
  const [host, setHost] = useState(getBase() || 'http://192.168.1.50')
  const [user, setUser] = useState('admin')
  const [pass, setPass] = useState('')
  const [err, setErr] = useState('')
  const [busy, setBusy] = useState(false)

  async function submit(e) {
    e.preventDefault()
    setBusy(true); setErr('')
    try {
      const base = host.startsWith('http') ? host : 'http://' + host
      const j = await apiLogin(base, user, pass)
      if (!j.role || j.role === 'none') {
        setErr('Sai tài khoản hoặc mật khẩu')
      } else {
        saveSession(base, user, j.role, j.role === 'Admin' ? pass : '')
        onDone()
      }
    } catch (e) {
      setErr('Không kết nối được Gateway: ' + e.message)
    }
    setBusy(false)
  }

  return (
    <div className="login-wrap">
      <form className="login" onSubmit={submit}>
        <h1>🐔 LoRa Farm</h1>
        <p>Giám sát vi khí hậu chăn nuôi — kết nối Gateway ESP32</p>

        <div className="field">
          <label>Địa chỉ Gateway (IP hoặc host)</label>
          <input value={host} onChange={(e) => setHost(e.target.value)} placeholder="http://192.168.1.50" />
        </div>
        <div className="field">
          <label>Tài khoản</label>
          <input value={user} onChange={(e) => setUser(e.target.value)} autoCapitalize="none" />
        </div>
        <div className="field">
          <label>Mật khẩu</label>
          <input type="password" value={pass} onChange={(e) => setPass(e.target.value)} />
        </div>

        <button className="btn primary" style={{ width: '100%', padding: 12 }} disabled={busy}>
          {busy ? 'Đang kết nối…' : 'Đăng nhập'}
        </button>
        <div className="err">{err}</div>
        <div className="hint">
          Mặc định demo: <b>admin / admin123</b> (toàn quyền) hoặc <b>user / user123</b> (chỉ xem).<br />
          IP Gateway hiển thị trên màn OLED và log serial khi Gateway nối WiFi.
        </div>
      </form>
    </div>
  )
}
