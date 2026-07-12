import { useEffect, useState } from 'react'
import {
  ResponsiveContainer, LineChart, Line, XAxis, YAxis, CartesianGrid, Tooltip, Legend,
} from 'recharts'
import { apiHistory, CMD, ACT } from '../api'

const VIEWS = {
  climate: { label: 'Nhiệt / Ẩm', lines: [['temp', '#f85149', '°C'], ['hum', '#39d0d8', '%']] },
  gas: { label: 'NH₃ / CO₂', lines: [['nh3', '#d29922', 'ppm'], ['co2', '#a371f7', 'ppm']] },
  env: { label: 'Áp suất / Sáng', lines: [['press', '#7ee787', 'hPa'], ['lux', '#ffd166', 'lx']] },
}

export default function NodeDetail({ node, admin, onClose, onCmd }) {
  const [hist, setHist] = useState([])
  const [view, setView] = useState('climate')

  useEffect(() => {
    let alive = true
    const load = () => apiHistory(node.node).then((h) => {
      if (!alive) return
      setHist(h.map((r) => ({ ...r, t: fmtTime(r.epoch) })))
    }).catch(() => {})
    load()
    const id = setInterval(load, 3000)
    return () => { alive = false; clearInterval(id) }
  }, [node.node])

  const v = VIEWS[view]
  const manual = node.act_mode === 'manual'
  const ota = () => {
    const url = prompt('URL firmware .bin (http:// hoặc https://):', 'http://')
    if (url) onCmd({ node: node.node, cmd: CMD.OTA, url })
  }

  return (
    <div className="overlay" onClick={onClose}>
      <div className="modal" onClick={(e) => e.stopPropagation()}>
        <div className="row" style={{ justifyContent: 'space-between' }}>
          <div>
            <h2>Node {node.node} <span className={'badge ' + (node.online ? node.fsm : 'OFFLINE')}>
              {node.online ? node.fsm : 'OFFLINE'}</span></h2>
            <div className="sub">MAC {node.mac} · {node.algo} · epoch {node.epoch}</div>
          </div>
          <button className="btn ghost" onClick={onClose}>✕ Đóng</button>
        </div>

        <div className="kv" style={{ gridTemplateColumns: 'repeat(4,1fr)' }}>
          <Info k="Nhiệt độ" v={node.temp + ' °C'} />
          <Info k="Độ ẩm" v={node.hum + ' %'} />
          <Info k="THI" v={node.thi} />
          <Info k="Áp suất" v={node.press + ' hPa'} />
          <Info k="NH₃" v={node.nh3 + ' ppm'} />
          <Info k="CO₂" v={node.co2 + ' ppm'} />
          <Info k="RSSI / SNR" v={node.rssi + ' / ' + node.snr} />
          <Info k="Khoảng cách" v={node.dist_m + ' m'} />
          <Info k="RTT" v={node.rtt + ' ms'} />
          <Info k="Edge decide" v={node.decide_us + ' µs'} />
          <Info k="Buffer offline" v={node.buf} />
          <Info k="Chế độ" v={node.act_mode === 'manual' ? `Manual (còn ${node.manual_left}s)` : 'Auto'} />
          <Info k="Firmware" v={'v' + node.fw_ver} />
          <Info k="OTA lúc" v={node.ota_epoch ? new Date(node.ota_epoch * 1000).toLocaleString('vi-VN') : 'chưa cập nhật'} />
        </div>

        <div className="seg">
          {Object.entries(VIEWS).map(([k, val]) => (
            <button key={k} className={view === k ? 'active' : ''} onClick={() => setView(k)}>{val.label}</button>
          ))}
        </div>

        <div className="chart-box">
          <ResponsiveContainer width="100%" height={260}>
            <LineChart data={hist} margin={{ top: 8, right: 16, bottom: 0, left: -10 }}>
              <CartesianGrid stroke="#22293a" />
              <XAxis dataKey="t" tick={{ fill: '#8b949e', fontSize: 11 }} minTickGap={40} />
              <YAxis tick={{ fill: '#8b949e', fontSize: 11 }} />
              <Tooltip contentStyle={{ background: '#161b22', border: '1px solid #263041' }} />
              <Legend />
              {v.lines.map(([key, color, unit]) => (
                <Line key={key} type="monotone" dataKey={key} name={key + ' (' + unit + ')'}
                  stroke={color} dot={false} strokeWidth={2} isAnimationActive={false} />
              ))}
            </LineChart>
          </ResponsiveContainer>
        </div>

        {admin ? (
          <>
            <div className="row" style={{ marginTop: 16 }}>
              {manual
                ? <button className="btn primary" onClick={() => onCmd({ node: node.node, cmd: CMD.ACT_AUTO })}>✓ Bật lại Auto (Active)</button>
                : <button className="btn" onClick={() => onCmd({ node: node.node, cmd: CMD.ACT_SET, act_mask: 0, act_val: 0 })}>⏻ Tắt Auto → Manual 60s</button>}
              <span className="sub">
                {manual ? `Manual: Active — còn ${node.manual_left}s rồi tự về Auto` : 'Auto: Active — node tự điều khiển tại biên (FSM)'}
              </span>
            </div>
            <div className="acts">
              <ActBtn node={node} bit={ACT.FAN} label="Quạt" onCmd={onCmd} manual={manual} />
              <ActBtn node={node} bit={ACT.MIST} label="Phun sương" onCmd={onCmd} manual={manual} />
              <ActBtn node={node} bit={ACT.AUX} label="Act3" onCmd={onCmd} manual={manual} />
              <button className="btn" onClick={ota}>⤓ OTA Update</button>
              <button className="btn" onClick={() => confirm('Khởi động lại Node ' + node.node + '?') && onCmd({ node: node.node, cmd: CMD.REBOOT })}>⟳ Reboot</button>
            </div>
          </>
        ) : (
          <div className="hint" style={{ marginTop: 14 }}>Bạn đang ở chế độ <b>User</b> (chỉ xem). Đăng nhập Admin để điều khiển.</div>
        )}
      </div>
    </div>
  )
}

function ActBtn({ node, bit, label, onCmd, manual }) {
  const on = (node.act & bit) !== 0
  return (
    <button className={'tgl' + (on ? ' on' : '')} disabled={!manual}
      onClick={() => onCmd({ node: node.node, cmd: CMD.ACT_SET, act_mask: bit, act_val: on ? 0 : bit })}>
      <span className="led" /> {label}: {on ? 'ON' : 'OFF'}
    </button>
  )
}

const Info = ({ k, v }) => (<div><div className="k">{k}</div><div className="v">{v}</div></div>)
const fmtTime = (ep) => {
  if (!ep) return ''
  const d = new Date(ep * 1000)
  return d.toLocaleTimeString('vi-VN', { hour12: false })
}
