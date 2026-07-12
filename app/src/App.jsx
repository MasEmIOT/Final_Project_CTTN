import { useEffect, useRef, useState } from 'react'
import { apiNodes, apiStatus, apiCmd, getRole, getUser, isAdmin, logout } from './api'
import Login from './components/Login.jsx'
import StatusBar from './components/StatusBar.jsx'
import NodeCard from './components/NodeCard.jsx'
import NodeDetail from './components/NodeDetail.jsx'

export default function App() {
  const [authed, setAuthed] = useState(!!getRole())
  const [nodes, setNodes] = useState([])
  const [status, setStatus] = useState(null)
  const [selId, setSelId] = useState(null)
  const [toast, setToast] = useState('')
  const [err, setErr] = useState('')
  const toastTimer = useRef(null)

  useEffect(() => {
    if (!authed) return
    let alive = true
    const poll = async () => {
      try {
        const [n, s] = await Promise.all([apiNodes(), apiStatus()])
        if (!alive) return
        setNodes(Array.isArray(n) ? n : [])
        setStatus(s)
        setErr('')
      } catch (e) {
        if (alive) setErr('Mất kết nối Gateway…')
      }
    }
    poll()
    const id = setInterval(poll, 2000)
    return () => { alive = false; clearInterval(id) }
  }, [authed])

  const showToast = (msg) => {
    setToast(msg)
    clearTimeout(toastTimer.current)
    toastTimer.current = setTimeout(() => setToast(''), 2500)
  }

  const onCmd = async (payload) => {
    const j = await apiCmd(payload)
    showToast(j.ok ? '✓ Đã gửi lệnh tới Node ' + payload.node : '✗ ' + (j.err || 'Lỗi gửi lệnh'))
  }

  if (!authed) return <Login onDone={() => setAuthed(true)} />

  const admin = isAdmin()
  const selected = nodes.find((n) => n.node === selId) || null

  return (
    <>
      <div className="topbar">
        <div className="brand"><span className="dot" /> LoRa Farm</div>
        {err && <span className="chip" style={{ color: '#f85149' }}>{err}</span>}
        <span className="chip role">{getUser()} · {getRole()}</span>
        <button className="btn ghost" onClick={() => { logout(); setAuthed(false) }}>Đăng xuất</button>
      </div>

      <div className="wrap">
        <StatusBar status={status} />
        {nodes.length === 0 ? (
          <div className="card">Chưa nhận được Node nào. Kiểm tra Node đã phát và Gateway đang thu LoRa.</div>
        ) : (
          <div className="grid">
            {nodes.map((n) => (
              <NodeCard key={n.node} node={n} admin={admin}
                onOpen={(d) => setSelId(d.node)} onCmd={onCmd} />
            ))}
          </div>
        )}
      </div>

      {selected && (
        <NodeDetail node={selected} admin={admin} onClose={() => setSelId(null)} onCmd={onCmd} />
      )}
      {toast && <div className="toast">{toast}</div>}
    </>
  )
}
