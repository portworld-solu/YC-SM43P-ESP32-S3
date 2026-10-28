/* eslint-disable */
// OEM device widgets — slate/teal visual system.
const { useState, useRef } = React;

const A = (hex, a) => {
  const h = hex.replace("#", "");
  const n = parseInt(h.length === 3 ? h.split("").map((c) => c + c).join("") : h, 16);
  const r = (n >> 16) & 255, g = (n >> 8) & 255, b = n & 255;
  return `rgba(${r},${g},${b},${a})`;
};
window.A = A;

function Badge({ icon, hue, on = true, size = 42 }) {
  return (
    <div className="badge" style={{
      width: size, height: size,
      background: on ? A(hue, 0.15) : "var(--bg-card-2)",
      color: on ? hue : "var(--t3)",
    }}>
      {React.cloneElement(icon, { width: size * 0.48, height: size * 0.48 })}
    </div>
  );
}

function PowerBtn({ on, hue, onClick, size = 40 }) {
  return (
    <button type="button" className="pwr" onClick={(e) => { e.stopPropagation(); onClick && onClick(); }} style={{
      width: size, height: size,
      background: on ? hue : "var(--bg-card-2)",
      color: on ? "var(--ink-on-accent)" : "var(--t3)",
      border: on ? "none" : "1px solid var(--line)",
    }}>
      <LIco.power width={size * 0.42} height={size * 0.42} />
    </button>
  );
}

function LinearTrack({ value, onChange, hue, height = 10 }) {
  const ref = useRef(null);
  const set = (clientX) => {
    const r = ref.current.getBoundingClientRect();
    const f = Math.max(0, Math.min(1, (clientX - r.left) / r.width));
    onChange && onChange(Math.round(f * 100));
  };
  const down = (e) => {
    e.stopPropagation();
    set(e.clientX);
    const mv = (ev) => set(ev.clientX);
    const up = () => { document.removeEventListener("pointermove", mv); document.removeEventListener("pointerup", up); };
    document.addEventListener("pointermove", mv);
    document.addEventListener("pointerup", up);
  };
  return (
    <div ref={ref} className="track" onPointerDown={down} style={{ position: "relative", height: height + 10, cursor: "pointer", display: "flex", alignItems: "center" }}>
      <div className="track-rail" style={{ position: "relative", width: "100%", height, background: "rgba(255,255,255,0.08)", borderRadius: 2 }}>
        <div className="track-fill" style={{ width: `${value}%`, height: "100%", background: hue, borderRadius: 2 }} />
        <div className="track-knob" style={{
          position: "absolute", top: -4,
          left: `clamp(0px, calc(${value}% - 8px), calc(100% - 16px))`,
          width: 16, height: height + 8, borderRadius: 2, background: "#fff",
          boxShadow: "0 1px 4px rgba(0,0,0,.35)",
        }} />
      </div>
    </div>
  );
}

function CtrlBtn({ icon, hue, accent, onClick, big }) {
  return (
    <button type="button" className={"ctrl-btn" + (accent ? " accent" : "")} onClick={(e) => { e.stopPropagation(); onClick && onClick(); }} style={{
      flex: 1, height: big ? 48 : 32, minWidth: 0,
      border: "1px solid var(--line)", cursor: "pointer",
      background: accent ? A(hue, 0.14) : "var(--bg-card-2)",
      color: accent ? hue : "var(--t2)",
      borderRadius: "var(--radius-sm)", display: "flex", alignItems: "center", justifyContent: "center",
    }}>{icon}</button>
  );
}

function OnOffWidget({ name = "Relay", room = "", hue, icon, on: onProp = true, onChange, compact = false, page = false }) {
  const [on, setOn] = useState(onProp);
  React.useEffect(() => { setOn(onProp); }, [onProp]);
  const toggle = () => {
    const n = !on;
    setOn(n);
    onChange && onChange(n);
  };
  const H = hue || "#eab308";
  const ic = icon || <LIco.bulb />;
  if (page) {
    return (
      <div className="w" data-on={on ? "1" : "0"} style={{ height: "100%", alignItems: "center", justifyContent: "center", gap: 20, cursor: "pointer" }} onClick={toggle}>
        <Badge icon={ic} hue={H} on={on} size={120} />
        <div style={{ textAlign: "center" }}>
          <div style={{ fontSize: 24, fontWeight: 700 }}>{name}</div>
          <div style={{ fontSize: 14, color: on ? H : "var(--t3)", marginTop: 4, fontWeight: 600 }}>{on ? "ON" : "OFF"}</div>
        </div>
        <button type="button" className="pwr" onClick={(e) => { e.stopPropagation(); toggle(); }} style={{
          width: "100%", height: 52, borderRadius: 8, gap: 8, fontWeight: 700, fontSize: 15,
          background: on ? H : "var(--bg-card-2)", color: on ? "var(--ink-on-accent)" : "var(--t2)",
          border: on ? "none" : "1px solid var(--line)",
        }}>
          <LIco.power width="20" height="20" /> {on ? "Turn off" : "Turn on"}
        </button>
      </div>
    );
  }
  /* Parity with hub_room_ui Switch: top(badge|pwr) → spacer → name|ON/OFF */
  return (
    <div className={"w" + (compact ? " w-compact" : "")} data-on={on ? "1" : "0"} style={{ height: "100%", cursor: "pointer" }} onClick={toggle}>
      <div style={{ display: "flex", justifyContent: "space-between", alignItems: "center" }}>
        <Badge icon={ic} hue={H} on={on} size={compact ? 32 : 42} />
        <PowerBtn on={on} hue={H} onClick={toggle} size={compact ? 30 : 38} />
      </div>
      <div style={{ flex: 1, minHeight: 0 }} />
      <div style={{ display: "flex", justifyContent: "space-between", alignItems: "baseline", gap: 8 }}>
        <div className="w-title" style={{ fontSize: compact ? 13 : 16, fontWeight: 700, minWidth: 0, overflow: "hidden", textOverflow: "ellipsis", whiteSpace: "nowrap" }}>
          {name}
        </div>
        <div className="w-sub" style={{ fontSize: compact ? 11 : 12, color: on ? H : "var(--t3)", fontWeight: 600, flexShrink: 0 }}>
          {on ? "ON" : "OFF"}
        </div>
      </div>
    </div>
  );
}

function DimmerWidget({ name = "Dimmer", room = "", level = 70, on: onProp = true, onChange, onChangeLevel, compact = false, page = false }) {
  const H = "#eab308";
  const [on, setOn] = useState(onProp);
  const [lvl, setLvl] = useState(level);
  React.useEffect(() => { setOn(onProp); }, [onProp]);
  React.useEffect(() => { setLvl(level); }, [level]);
  const emit = (nextOn, nextLvl) => {
    onChangeLevel && onChangeLevel(nextLvl);
    onChange && onChange({ on: nextOn, level: nextLvl });
  };
  const setPower = () => {
    const n = !on;
    setOn(n);
    emit(n, lvl);
  };
  const setLevel = (v) => {
    setLvl(v);
    if (v > 0 && !on) setOn(true);
    emit(v > 0 ? true : on, v);
  };
  const eff = on ? lvl : 0;
  if (page) {
    return (
      <div className="w" data-on={on ? "1" : "0"} style={{ height: "100%", gap: 14 }}>
        <div style={{ display: "flex", justifyContent: "space-between", alignItems: "center" }}>
          <div style={{ fontSize: 18, fontWeight: 700 }}>{name}</div>
          <PowerBtn on={on} hue={H} onClick={setPower} />
        </div>
        <div style={{ flex: 1, display: "flex", alignItems: "center", justifyContent: "center" }}>
          <div style={{ fontSize: 56, fontWeight: 300, fontFamily: "var(--font-d)", color: on ? H : "var(--t4)" }}>{eff}<span style={{ fontSize: 22 }}>%</span></div>
        </div>
        <div style={{ opacity: on ? 1 : 0.35, pointerEvents: on ? "auto" : "none" }}>
          <LinearTrack value={eff} onChange={setLevel} hue={H} height={12} />
        </div>
      </div>
    );
  }
  return (
    <div className={"w" + (compact ? " w-compact" : "")} data-on={on ? "1" : "0"} style={{ height: "100%" }}>
      <div style={{ display: "flex", justifyContent: "space-between" }}>
        <Badge icon={<LIco.bulb />} hue={H} on={on} size={compact ? 32 : 42} />
        <PowerBtn on={on} hue={H} onClick={setPower} size={compact ? 30 : 38} />
      </div>
      <div style={{ flex: 1, minHeight: 0 }} />
      <div style={{ display: "flex", justifyContent: "space-between", alignItems: "baseline" }}>
        <div className="w-title" style={{ fontSize: compact ? 13 : 16, fontWeight: 700 }}>{name}</div>
        <div style={{ fontFamily: "var(--font-m)", color: on ? H : "var(--t4)", fontWeight: 600, fontSize: compact ? 12 : 14 }}>{eff}%</div>
      </div>
      <div style={{ marginTop: compact ? 4 : 6, opacity: on ? 1 : 0.35, pointerEvents: on ? "auto" : "none" }}>
        <LinearTrack value={eff} onChange={setLevel} hue={H} height={compact ? 7 : 10} />
      </div>
    </div>
  );
}

function ShutterWidget({ name = "Shutter", position = 40, onChangePos, onChange, compact = false, page = false }) {
  const H = "#eab308";
  const [pos, setPos] = useState(position);
  React.useEffect(() => { setPos(position); }, [position]);
  const move = (v) => {
    setPos(v);
    onChangePos && onChangePos(v);
    onChange && onChange(v);
  };
  return (
    <div className={"w" + (compact ? " w-compact" : "")} style={{ height: "100%" }}>
      <div style={{ display: "flex", alignItems: "center", gap: 8 }}>
        <Badge icon={<LIco.shutter />} hue={H} size={compact ? 32 : 42} />
        <div style={{ flex: 1, minWidth: 0 }}>
          <div className="w-title" style={{ fontSize: compact ? 13 : 16, fontWeight: 700 }}>{name}</div>
          <div className="w-sub" style={{ fontSize: compact ? 11 : 12, color: "var(--t3)" }}>{pos}% open</div>
        </div>
      </div>
      {(page || !compact) && <div style={{ marginTop: 8 }}><LinearTrack value={pos} onChange={move} hue={H} /></div>}
      <div style={{ flex: 1, minHeight: 0 }} />
      <div style={{ display: "flex", gap: compact ? 6 : 8, marginTop: compact ? 2 : 8 }}>
        <CtrlBtn icon={<LIco.up width="18" height="18" />} hue={H} accent onClick={() => move(100)} big={!compact} />
        <CtrlBtn icon={<LIco.stop width="14" height="14" />} hue={H} onClick={() => {}} big={!compact} />
        <CtrlBtn icon={<LIco.down width="18" height="18" />} hue={H} accent onClick={() => move(0)} big={!compact} />
      </div>
    </div>
  );
}

function CurtainWidget({ name = "Curtain", position = 60, onChangePos, onChange, compact = false, page = false }) {
  const H = "#818cf8";
  const [pos, setPos] = useState(position);
  React.useEffect(() => { setPos(position); }, [position]);
  const move = (v) => {
    setPos(v);
    onChangePos && onChangePos(v);
    onChange && onChange(v);
  };
  return (
    <div className={"w" + (compact ? " w-compact" : "")} style={{ height: "100%" }}>
      <div style={{ display: "flex", alignItems: "center", gap: 8 }}>
        <Badge icon={<LIco.curtain />} hue={H} size={compact ? 32 : 42} />
        <div style={{ flex: 1, minWidth: 0 }}>
          <div className="w-title" style={{ fontSize: compact ? 13 : 16, fontWeight: 700 }}>{name}</div>
          <div className="w-sub" style={{ fontSize: compact ? 11 : 12, color: "var(--t3)" }}>{pos}% open</div>
        </div>
      </div>
      {(page || !compact) && <div style={{ marginTop: 8 }}><LinearTrack value={pos} onChange={move} hue={H} /></div>}
      <div style={{ flex: 1, minHeight: 0 }} />
      <div style={{ display: "flex", gap: compact ? 6 : 8, marginTop: compact ? 2 : 8 }}>
        <CtrlBtn icon={<LIco.left width="18" height="18" />} hue={H} accent onClick={() => move(100)} big={!compact} />
        <CtrlBtn icon={<LIco.stop width="14" height="14" />} hue={H} onClick={() => {}} big={!compact} />
        <CtrlBtn icon={<LIco.right width="18" height="18" />} hue={H} accent onClick={() => move(0)} big={!compact} />
      </div>
    </div>
  );
}

function AlarmWidget({ state = "armed", mode = "Away", onOpen, compact = false, page = false }) {
  const cfg = {
    disarmed: { hue: "#7d8792", label: "Disarmed", icon: <LIco.shield /> },
    armed: { hue: "#34d399", label: "Armed", icon: <LIco.shieldFill /> },
    alert: { hue: "#f43f5e", label: "ALERT", icon: <LIco.shieldFill /> },
  }[state];
  return (
    <div className="w" onClick={onOpen} style={{
      height: "100%", cursor: onOpen ? "pointer" : "default",
      alignItems: "center", justifyContent: "center", textAlign: "center", gap: page ? 18 : 10,
    }}>
      <div className="badge" style={{
        width: page ? 120 : compact ? 48 : 64, height: page ? 120 : compact ? 48 : 64,
        background: A(cfg.hue, 0.14), color: cfg.hue,
      }}>
        {React.cloneElement(cfg.icon, { width: page ? 64 : compact ? 24 : 32, height: page ? 64 : compact ? 24 : 32 })}
      </div>
      <div style={{ fontSize: page ? 24 : 15, fontWeight: 700 }}>Security</div>
      <div style={{ fontSize: page ? 18 : 13, color: cfg.hue, fontWeight: 700 }}>
        {cfg.label}{state === "armed" ? ` · ${mode}` : ""}
      </div>
    </div>
  );
}

Object.assign(window, { A, Badge, PowerBtn, LinearTrack, CtrlBtn, OnOffWidget, DimmerWidget, ShutterWidget, CurtainWidget, AlarmWidget });
