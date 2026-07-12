export default function StatusBar({ status }) {
  const s = status || {}
  const items = [
    { l: 'Gateway IP', n: s.ip || '—' },
    { l: 'Node online', n: (s.online ?? '—') + ' / ' + (s.total ?? '—') },
    { l: 'RAM trống', n: s.heap ? Math.round(s.heap / 1024) + ' KB' : '—' },
    { l: 'Uptime', n: s.uptime != null ? fmtUp(s.uptime) : '—' },
    { l: 'Mã hóa', n: s.algo || '—' },
    { l: 'Replay chặn', n: s.replay_drops ?? 0 },
  ]
  return (
    <div className="status">
      {items.map((it, i) => (
        <div className="stat" key={i}>
          <div className="l">{it.l}</div>
          <div className="n">{it.n}</div>
        </div>
      ))}
    </div>
  )
}

function fmtUp(sec) {
  const h = Math.floor(sec / 3600), m = Math.floor((sec % 3600) / 60)
  if (h > 0) return h + 'h' + m + 'm'
  return m + 'm' + (sec % 60) + 's'
}
