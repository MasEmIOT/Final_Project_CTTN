import { CMD, ACT } from '../api'

const ACTS = [
  { bit: ACT.FAN, label: 'Quạt' },
  { bit: ACT.MIST, label: 'Phun sương' },
  { bit: ACT.AUX, label: 'Act3' },
]

const epStr = (s) => (s ? new Date(s * 1000).toLocaleString('vi-VN') : 'chưa cập nhật')

export default function NodeCard({ node, admin, onOpen, onCmd }) {
  const d = node
  const fsm = d.online ? d.fsm : 'OFFLINE'
  const pdr = d.tx ? Math.round((100 * d.ack) / d.tx) : 100
  const manual = d.act_mode === 'manual'

  const toggle = (bit) => {
    const on = (d.act & bit) !== 0
    onCmd({ node: d.node, cmd: CMD.ACT_SET, act_mask: bit, act_val: on ? 0 : bit })
  }
  const setAuto = (auto) =>
    onCmd(auto ? { node: d.node, cmd: CMD.ACT_AUTO } : { node: d.node, cmd: CMD.ACT_SET, act_mask: 0, act_val: 0 })

  return (
    <div className="card click" onClick={() => onOpen(d)}>
      <h3>
        <span>Node {d.node}</span>
        <span className={'badge ' + fsm}>{fsm}</span>
      </h3>
      <div className="sub">MAC {d.mac} · {d.algo} · FW v{d.fw_ver} · OTA {epStr(d.ota_epoch)}</div>
      {d.ota_cmd_epoch ? (
        d.fw_ver > d.ota_from
          ? <div className="sub" style={{ color: 'var(--green)', fontWeight: 600 }}>✓ OTA thành công: v{d.ota_from} → v{d.fw_ver}</div>
          : <div className="sub" style={{ color: 'var(--yellow)', fontWeight: 600 }}>
              {d.online ? '⟳ OTA đã gửi, chờ node cập nhật…' : '⟳ OTA đang chạy (node offline tải firmware)…'}
            </div>
      ) : null}

      <div className="kv">
        <div><div className="k">Nhiệt độ</div><div className="v">{fmt(d.temp)}<small>°C</small></div></div>
        <div><div className="k">Độ ẩm</div><div className="v">{fmt(d.hum)}<small>%</small></div></div>
        <div><div className="k">NH₃</div><div className="v">{d.nh3}<small>ppm</small></div></div>
        <div><div className="k">CO₂</div><div className="v">{d.co2}<small>ppm</small></div></div>
        <div><div className="k">THI</div><div className="v">{fmt(d.thi)}</div></div>
        <div><div className="k">PDR</div><div className="v">{pdr}<small>%</small></div></div>
        <div><div className="k">RSSI</div><div className="v">{d.rssi}<small>dBm</small></div></div>
        <div>
          <div className="k">Chế độ</div>
          <div className="v" style={{ color: manual ? 'var(--yellow)' : 'var(--green)' }}>
            {manual ? `Manual: Active (${d.manual_left}s)` : 'Auto: Active'}
          </div>
        </div>
      </div>

      {admin && (
        <div className="acts" onClick={(e) => e.stopPropagation()}>
          {manual
            ? <button className="tgl on" onClick={() => setAuto(true)}><span className="led" /> Bật Auto</button>
            : <button className="tgl" onClick={() => setAuto(false)}>⏻ Tắt Auto (Manual 60s)</button>}
          {ACTS.map((a) => {
            const on = (d.act & a.bit) !== 0
            return (
              <button key={a.bit} className={'tgl' + (on ? ' on' : '')} disabled={!manual} onClick={() => toggle(a.bit)}>
                <span className="led" /> {a.label}: {on ? 'ON' : 'OFF'}
              </button>
            )
          })}
        </div>
      )}
    </div>
  )
}

const fmt = (x) => (typeof x === 'number' ? x.toFixed(1) : x)
