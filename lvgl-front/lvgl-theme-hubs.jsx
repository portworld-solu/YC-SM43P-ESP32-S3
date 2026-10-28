/* eslint-disable */
// Distinct hub compositions per visual theme — each is fully interactive via useHub().
const { useState: useStateT, useMemo: useMemoT } = React;

function ThemeScope({ theme = "slate", children, className = "", style }) {
  return (
    <div className={"theme-scope " + className} data-theme={theme} style={style}>
      {children}
    </div>
  );
}

function SoftPill({ children, ok, onClick }) {
  const tag = onClick ? "button" : "span";
  return React.createElement(tag, {
    type: onClick ? "button" : undefined,
    onClick,
    style: {
      fontSize: 11, fontWeight: 700, padding: "4px 10px", borderRadius: 999,
      background: ok === false ? "rgba(244,63,94,0.12)" : "color-mix(in srgb, var(--accent) 16%, transparent)",
      color: ok === false ? "var(--alert)" : "var(--accent)",
      border: "1px solid color-mix(in srgb, var(--accent) 35%, transparent)",
      cursor: onClick ? "pointer" : "default",
      fontFamily: "inherit",
    },
  }, children);
}

function useHubOrDemo() {
  const hub = useHub();
  return hub;
}

function clockNow() {
  const d = new Date();
  return `${String(d.getHours()).padStart(2, "0")}:${String(d.getMinutes()).padStart(2, "0")}`;
}

function hubTime(hub, clock) {
  return clock || (hub && hub.clock) || clockNow();
}

/** Shared quick actions — keep every theme able to reach core flows */
function HubQuickBar({ dense = false }) {
  const hub = useHubOrDemo();
  if (!hub) return null;
  const items = [
    { id: "scenes", label: "情景", Icon: LIco.grid },
    { id: "security", label: "安防", Icon: LIco.shield },
    { id: "schedule", label: "日程", Icon: LIco.clock },
    { id: "settings", label: "设置", Icon: LIco.cog },
  ];
  return (
    <div style={{ display: "flex", gap: dense ? 4 : 6 }}>
      {items.map(({ id, label, Icon }) => (
        <button key={id} type="button" onClick={() => hub.go(id)} style={{
          flex: 1, height: dense ? 36 : 40, borderRadius: "var(--radius-sm)",
          border: "1px solid var(--line)", background: "var(--bg-card)", color: "var(--t2)",
          cursor: "pointer", fontFamily: "inherit", fontSize: 10, fontWeight: 700,
          display: "flex", flexDirection: "column", alignItems: "center", justifyContent: "center", gap: 2,
        }}>
          <Icon width="14" height="14" style={{ color: "var(--accent)" }} />
          {label}
        </button>
      ))}
    </div>
  );
}

/** Slate — parity with ui/themes/slate/home.c (no Power card; climate → sensor_cal). */
function ThemeHubSlate({ clock }) {
  const hub = useHubOrDemo();
  const t = hubTime(hub, clock);
  const zh = hub ? hub.settings.lang !== "en" : false;
  const rooms = hub ? hub.rooms : [
    { id: "living", name: "客厅", nameEn: "Living", devices: 6 },
    { id: "bed", name: "主卧", nameEn: "Bedroom", devices: 4 },
    { id: "kitchen", name: "厨房", nameEn: "Kitchen", devices: 4 },
    { id: "study", name: "书房", nameEn: "Study", devices: 3 },
  ];
  const tOff = hub?.sensorCal?.tempOffset || 0;
  const rOff = hub?.sensorCal?.rhOffset || 0;
  const indoor = hub ? `${(Number(hub.metrics.indoor) + tOff).toFixed(1)}°` : "24.5°";
  const rh = hub ? `${Math.round(Number(hub.metrics.rh) + rOff)}%` : "48%";
  const climOk = true;
  const protos = hub ? hub.protocols.slice(0, 3) : [
    { id: "mqtt", name: "MQTT", ok: true },
    { id: "modbus", name: "Modbus", ok: true },
    { id: "rs485", name: "RS485", ok: false },
  ];
  return (
    <div className="lv">
      <div className="lv-head">
        <div className="clock">{t}</div>
        <div className="room">Slate Hub</div>
        <div className="wx" style={{ cursor: "pointer" }} onClick={() => hub && hub.go("network")}>
          {hub && !hub.network.wifiOn
            ? <LIco.wifiOff width="20" height="20" />
            : <LIco.wifi width="20" height="20" />}
        </div>
      </div>
      <div className="lv-body" style={{ display: "flex", flexDirection: "column", gap: 10 }}>
        <div style={{ display: "flex", gap: 6, flexWrap: "wrap" }}>
          {protos.map((p) => (
            <SoftPill key={p.id} ok={p.ok} onClick={() => hub && hub.go("gateway")}>
              {p.name.replace(" Broker", "").replace(" TCP", "").replace(" Bus A", "")}
            </SoftPill>
          ))}
        </div>
        {/* Full-width climate → sensor calibration (Power removed) */}
        <button
          type="button"
          onClick={() => hub && hub.go("sensor_cal")}
          style={{
            width: "100%", height: 112, flexShrink: 0, position: "relative",
            background: "var(--bg-card)", border: "1px solid var(--line)", borderRadius: "var(--radius)",
            color: "var(--t1)", cursor: "pointer", fontFamily: "inherit", textAlign: "left", padding: "10px 12px",
          }}
        >
          <div style={{ fontSize: 12, color: "var(--t3)", fontWeight: 600 }}>
            {zh ? "室内温湿度 · 点此校准" : "Indoor climate · tap to calibrate"}
          </div>
          <div style={{
            position: "absolute", left: 12, bottom: 14,
            fontSize: 28, fontWeight: 600, fontFamily: "var(--font-d)",
            color: climOk ? "var(--accent)" : "var(--t3)",
          }}>{indoor}</div>
          <div style={{
            position: "absolute", right: 14, bottom: 14,
            fontSize: 28, fontWeight: 600, fontFamily: "var(--font-d)",
            color: climOk ? "var(--t1)" : "var(--t3)",
          }}>{rh}</div>
        </button>
        <div style={{ display: "grid", gridTemplateColumns: "1fr 1fr", gap: 8, flex: 1, minHeight: 0 }}>
          {rooms.map((r) => {
            const on = hub ? hub.roomActive(r.id) : true;
            const label = hub ? hub.roomLabel(r) : (zh ? r.name : (r.nameEn || r.name));
            const pts = (hub && hub.roomWidgets && hub.roomWidgets[r.id])
              ? hub.roomWidgets[r.id].filter((w) => w.enabled !== false).length
              : r.devices;
            const RoomIco = LIco.forRoom(r.id);
            return (
              <button key={r.id} type="button" onClick={() => hub && hub.openRoom(r.id)} style={{
                background: "var(--bg-card)", border: "1px solid var(--line)",
                borderLeft: `3px solid ${on ? "var(--accent)" : "var(--line)"}`,
                borderRadius: "var(--radius)", padding: 12, display: "flex", flexDirection: "column",
                alignItems: "center", justifyContent: "center", gap: 6,
                color: "var(--t1)", cursor: "pointer",
                textAlign: "center", fontFamily: "inherit", minHeight: 88,
              }}>
                <RoomIco width="24" height="24" style={{ color: "var(--accent)" }} />
                <div style={{ fontWeight: 700, fontSize: 14 }}>{label}</div>
                <div style={{ fontSize: 11, color: on ? "var(--on)" : "var(--t3)" }}>
                  {pts} pts · {on ? "Active" : "Idle"}
                </div>
              </button>
            );
          })}
        </div>
        <HubQuickBar dense />
      </div>
      <div className="lv-dots"><div className="lv-dot on" /><div className="lv-dot" /><div className="lv-dot" /><div className="lv-dot" /></div>
    </div>
  );
}
const hubBar = {
  flex: 1, height: 40, borderRadius: 8, border: "1px solid var(--line)",
  background: "var(--bg-card)", color: "var(--t1)", fontWeight: 700, cursor: "pointer", fontFamily: "inherit",
};

/** Sand — list + scene CTAs (aligned with slate routes: network / sensor_cal / 4 rooms). */
function ThemeHubSand({ clock }) {
  const hub = useHubOrDemo();
  const t = hubTime(hub, clock);
  const rooms = hub ? hub.rooms : [
    { id: "living", name: "客厅", devices: 6 },
    { id: "bed", name: "主卧", devices: 4 },
    { id: "kitchen", name: "厨房", devices: 4 },
    { id: "study", name: "书房", devices: 3 },
  ];
  const subs = {
    living: "AC / Curtain / Lights",
    bed: "Heat / Lights",
    kitchen: "Outlet / Valve",
    study: "Lights / Curtain",
  };
  const tOff = hub?.sensorCal?.tempOffset || 0;
  const rOff = hub?.sensorCal?.rhOffset || 0;
  const climLine = hub
    ? `Indoor ${(Number(hub.metrics.indoor) + tOff).toFixed(1)} / RH ${Math.round(Number(hub.metrics.rh) + rOff)}% / ${hub.activeScene}`
    : "Indoor 24.0 / RH 48%";
  const sandBtn = {
    flex: 1, height: 48, borderRadius: 14, border: "1px solid var(--line)",
    background: "var(--bg-card)", color: "var(--t1)", fontWeight: 700, fontSize: 15, cursor: "pointer", fontFamily: "inherit",
  };
  return (
    <div className="lv">
      <div style={{ padding: "24px 24px 8px", display: "flex", justifyContent: "space-between", alignItems: "flex-end" }}>
        <div>
          <div style={{ fontSize: 12, fontWeight: 700, color: "var(--t3)", letterSpacing: "0.08em" }}>SAND HUB</div>
          <div style={{ fontSize: 56, fontWeight: 300, lineHeight: 1, letterSpacing: "-0.03em", marginTop: 4 }}>{t}</div>
          <button type="button" onClick={() => hub && hub.go("sensor_cal")} style={{
            background: "none", border: "none", padding: 0, marginTop: 6, cursor: "pointer",
            fontSize: 14, color: "var(--t3)", fontFamily: "inherit", textAlign: "left",
          }}>
            {climLine}
          </button>
        </div>
        <button type="button" onClick={() => hub && hub.go("network")} style={{
          width: 72, height: 72, borderRadius: 20, background: "var(--bg-card)",
          border: "1px solid var(--line)", display: "flex", flexDirection: "column",
          alignItems: "center", justifyContent: "center", gap: 4, cursor: "pointer", color: "var(--t1)", fontFamily: "inherit",
        }}>
          {hub && !hub.network.wifiOn
            ? <LIco.wifiOff width="22" height="22" style={{ color: "var(--accent)" }} />
            : <LIco.wifi width="22" height="22" style={{ color: "var(--accent)" }} />}
          <span style={{ fontSize: 11, fontWeight: 700 }}>WiFi</span>
        </button>
      </div>
      <div style={{ flex: 1, padding: "0 20px", display: "flex", flexDirection: "column", gap: 8, overflowY: "auto", minHeight: 0 }}>
        {rooms.map((r) => {
          const on = hub ? hub.roomActive(r.id) : r.id !== "bed";
          const RoomIco = LIco.forRoom(r.id);
          return (
            <button key={r.id} type="button" onClick={() => hub && hub.openRoom(r.id)} style={{
              display: "flex", alignItems: "center", gap: 14, padding: "12px 16px",
              background: "var(--bg-card)", borderRadius: 16, border: "1px solid var(--line)",
              boxShadow: "0 4px 16px rgba(60,45,30,0.06)", cursor: "pointer",
              color: "var(--t1)", fontFamily: "inherit", textAlign: "left", width: "100%",
            }}>
              <div style={{
                width: 44, height: 44, borderRadius: 14,
                background: on ? "color-mix(in srgb, var(--accent) 14%, transparent)" : "var(--bg-card-2)",
                color: on ? "var(--accent)" : "var(--t3)", display: "flex", alignItems: "center", justifyContent: "center",
              }}><RoomIco width="24" height="24" /></div>
              <div style={{ flex: 1 }}>
                <div style={{ fontWeight: 700, fontSize: 16 }}>{hub ? hub.roomLabel(r) : r.name}</div>
                <div style={{ fontSize: 12, color: "var(--t3)" }}>{subs[r.id] || `${r.devices} pts`}</div>
              </div>
              <span style={{ fontSize: 12, fontWeight: 700, color: on ? "var(--on)" : "var(--t4)" }}>{on ? "Active" : "Idle"}</span>
            </button>
          );
        })}
      </div>
      <div style={{ padding: "8px 20px 0" }}><HubQuickBar dense /></div>
      <div style={{ display: "flex", gap: 10, padding: "10px 20px 18px" }}>
        <button type="button" style={sandBtn} onClick={() => hub && hub.applyScene("home")}>回家</button>
        <button type="button" style={{ ...sandBtn, background: "var(--accent)", color: "var(--ink-on-accent)", border: "none" }}
          onClick={() => hub && hub.applyScene("away")}>离家</button>
      </div>
    </div>
  );
}

/** Ink — 3×3 DO matrix bound to living room + bus */
function ThemeHubInk({ clock }) {
  const hub = useHubOrDemo();
  const t = hubTime(hub, clock);
  const d = hub ? hub.devices.living : null;
  const cells = useMemoT(() => [
    { id: "L1", get: () => (d ? d.ceiling : true), set: () => hub && hub.patchRoom("living", { ceiling: !d.ceiling }) },
    { id: "L2", get: () => (d ? d.strip > 0 : true), set: () => hub && hub.patchRoom("living", { strip: d.strip > 0 ? 0 : 55 }) },
    { id: "AC", get: () => (d ? d.acOn : false), set: () => hub && hub.patchRoom("living", { acOn: !d.acOn, acMode: d.acOn ? "off" : "cool" }) },
    { id: "HEAT", get: () => (d ? d.heatOn : true), set: () => hub && hub.patchRoom("living", { heatOn: !d.heatOn }) },
    { id: "CUR", get: () => (d ? d.curtain > 50 : true), set: () => hub && hub.patchRoom("living", { curtain: d.curtain > 50 ? 0 : 100 }) },
    { id: "SHUT", get: () => (d ? d.shutter > 50 : false), set: () => hub && hub.patchRoom("living", { shutter: d.shutter > 50 ? 0 : 100 }) },
    { id: "FAN", get: () => (d ? d.fanOn : false), set: () => hub && hub.patchRoom("living", { fanOn: !d.fanOn }) },
    { id: "PUMP", get: () => true, set: () => hub && hub.go("appliances") },
    { id: "VALVE", get: () => true, set: () => hub && hub.go("appliances") },
  ], [d, hub]);
  const [local, setLocal] = useStateT(() => cells.map((_, i) => i % 3 !== 2));
  const protos = hub ? hub.protocols : [];
  return (
    <div className="lv">
      <div style={{ display: "flex", height: "100%" }}>
        <div style={{
          width: 108, borderRight: "1px solid var(--line)", padding: "16px 12px",
          display: "flex", flexDirection: "column", gap: 14,
        }}>
          <div style={{ fontSize: 10, letterSpacing: "0.14em", color: "var(--accent)", fontWeight: 700 }}>INK</div>
          <div style={{ fontSize: 22, fontWeight: 600, lineHeight: 1.1 }}>{t}</div>
          <button type="button" onClick={() => hub && hub.go("gateway")} style={{
            fontSize: 10, color: "var(--t3)", lineHeight: 1.5, background: "none", border: "none",
            padding: 0, textAlign: "left", cursor: "pointer", fontFamily: "inherit",
          }}>
            {protos.length ? protos.map((p) => (
              <span key={p.id} style={{ display: "block", color: p.ok ? "var(--on)" : "var(--alert)" }}>
                {p.id.toUpperCase()} {p.ok ? "OK" : "FAULT"}
              </span>
            )) : <>MQTT OK<br />MBUS OK<br />485 FAULT</>}
          </button>
          <div style={{ flex: 1 }} />
          <button type="button" onClick={() => hub && hub.go("energy")} style={{
            fontSize: 10, color: "var(--accent)", background: "none", border: "none", padding: 0,
            cursor: "pointer", fontFamily: "inherit", textAlign: "left",
          }}>PWR {hub ? `${hub.powerKw.toFixed(1)}kW` : "2.1kW"}</button>
        </div>
        <div style={{ flex: 1, padding: 14, display: "flex", flexDirection: "column" }}>
          <div style={{ display: "flex", justifyContent: "space-between", marginBottom: 12 }}>
            <div style={{ fontSize: 12, fontWeight: 700, letterSpacing: "0.06em" }}>ZONE MATRIX</div>
            <button type="button" onClick={() => hub && hub.openRoom("living")} style={{
              fontSize: 10, color: "var(--accent)", background: "none", border: "none", cursor: "pointer", fontWeight: 700,
            }}>客厅 →</button>
          </div>
          <div style={{ display: "grid", gridTemplateColumns: "1fr 1fr 1fr", gap: 8, flex: 1 }}>
            {cells.map((c, i) => {
              const on = hub && d ? c.get() : local[i];
              return (
                <button key={c.id} type="button" onClick={() => {
                  if (hub && d) c.set();
                  else setLocal((a) => a.map((v, n) => (n === i ? !v : v)));
                }} style={{
                  border: "1px solid var(--line)", background: on ? "var(--accent)" : "var(--bg-card)",
                  color: on ? "var(--ink-on-accent)" : "var(--t2)", borderRadius: "var(--radius-sm)",
                  fontWeight: 700, fontSize: 12, fontFamily: "inherit", cursor: "pointer",
                }}>{c.id}</button>
              );
            })}
          </div>
          <div style={{ marginTop: 12, fontSize: 10, color: "var(--t4)" }}>TAP = TOGGLE · 绑定客厅点位</div>
          <div style={{ marginTop: 10 }}><HubQuickBar dense /></div>
        </div>
      </div>
    </div>
  );
}

/** Forest — eco ring + scene chips */
function ThemeHubForest({ clock }) {
  const hub = useHubOrDemo();
  const t = hubTime(hub, clock);
  const scenes = [
    { id: "home", label: "晨起" },
    { id: "guest", label: "会客" },
    { id: "movie", label: "影院" },
    { id: "sleep", label: "睡眠模式" },
    { id: "eco", label: "节能" },
    { id: "alloff", label: "全关" },
  ];
  const kwh = hub ? (12 + hub.powerKw * 2).toFixed(1) : "12.4";
  const busOk = hub ? hub.okCount === hub.protocols.length : false;
  return (
    <div className="lv">
      <div className="lv-head">
        <div className="clock">{t}</div>
        <div className="room">Forest</div>
        <button type="button" className="wx" style={{ color: "var(--accent)", background: "none", border: "none", cursor: "pointer", fontWeight: 700 }}
          onClick={() => hub && hub.go("energy")}>Eco</button>
      </div>
      <div className="lv-body" style={{ display: "flex", flexDirection: "column", alignItems: "center" }}>
        <button type="button" onClick={() => hub && hub.go("energy")} style={{
          width: 148, height: 148, borderRadius: "50%", marginTop: 4,
          border: "10px solid var(--bg-card-2)",
          borderTopColor: "var(--accent)", borderRightColor: "var(--cool)",
          display: "flex", flexDirection: "column", alignItems: "center", justifyContent: "center",
          background: "transparent", color: "var(--t1)", cursor: "pointer", fontFamily: "inherit",
        }}>
          <div style={{ fontSize: 11, color: "var(--t3)", fontWeight: 700 }}>今日用电</div>
          <div style={{ fontSize: 32, fontWeight: 300, lineHeight: 1.1 }}>{kwh}</div>
          <div style={{ fontSize: 12, color: "var(--t3)" }}>kWh</div>
        </button>
        <div style={{ display: "flex", gap: 16, marginTop: 12, width: "100%", justifyContent: "center" }}>
          <button type="button" onClick={() => hub && hub.go("hvac")} style={forestStat}>
            <div style={{ fontSize: 18, fontWeight: 700 }}>{hub ? `${hub.metrics.indoor}°` : "24°"}</div>
            <div style={{ fontSize: 11, color: "var(--t3)" }}>室内</div>
          </button>
          <button type="button" onClick={() => hub && hub.go("hvac")} style={forestStat}>
            <div style={{ fontSize: 18, fontWeight: 700 }}>{hub ? `${hub.metrics.rh}%` : "52%"}</div>
            <div style={{ fontSize: 11, color: "var(--t3)" }}>湿度</div>
          </button>
          <button type="button" onClick={() => hub && hub.go("gateway")} style={forestStat}>
            <div style={{ fontSize: 18, fontWeight: 700, color: busOk ? "var(--on)" : "var(--alert)" }}>{busOk ? "OK" : "CHK"}</div>
            <div style={{ fontSize: 11, color: "var(--t3)" }}>总线</div>
          </button>
        </div>
        <div style={{ display: "flex", flexWrap: "wrap", gap: 8, marginTop: 12, width: "100%", justifyContent: "center" }}>
          {scenes.map((s) => {
            const active = hub && (hub.activeScene === s.id || (s.id === "alloff" && hub.activeScene === "away"));
            return (
              <button key={s.id} type="button" onClick={() => hub && hub.applyScene(s.id)} style={{
                padding: "8px 12px", borderRadius: 999, border: "1px solid var(--line)",
                background: active ? "var(--accent)" : "var(--bg-card)",
                color: active ? "var(--ink-on-accent)" : "var(--t1)",
                fontWeight: 700, fontSize: 12, cursor: "pointer", fontFamily: "inherit",
              }}>{s.label}</button>
            );
          })}
        </div>
        <div style={{ width: "100%", marginTop: 10 }}><HubQuickBar dense /></div>
      </div>
    </div>
  );
}
const forestStat = {
  textAlign: "center", background: "none", border: "none", color: "var(--t1)", cursor: "pointer", fontFamily: "inherit", padding: 0,
};

/** Dusk — evening cards + security */
function ThemeHubDusk({ clock }) {
  const hub = useHubOrDemo();
  const t = hubTime(hub, clock);
  const d = hub ? hub.devices.living : { acSp: 24, acMode: "cool", acOn: true, ceiling: true, strip: 55, curtain: 70 };
  const duskCard = { borderRadius: 20, padding: 14, background: "var(--bg-card)", border: "1px solid var(--line)", cursor: "pointer", color: "var(--t1)", fontFamily: "inherit", textAlign: "left" };
  const lightsOn = hub
    ? Object.values(hub.devices).filter((x) => x.ceiling || x.strip > 0).length
    : 6;
  return (
    <div className="lv">
      <div style={{ padding: "24px 22px 0" }}>
        <div style={{ display: "flex", justifyContent: "space-between", alignItems: "center" }}>
          <div>
            <div style={{ fontSize: 12, color: "var(--violet)", fontWeight: 700 }}>DUSK</div>
            <div style={{ fontSize: 48, fontWeight: 300, letterSpacing: "-0.03em" }}>{t}</div>
          </div>
          <button type="button" onClick={() => hub && hub.go("security")} style={{
            padding: "10px 14px", borderRadius: 16, background: "color-mix(in srgb, var(--accent) 18%, transparent)",
            border: "1px solid color-mix(in srgb, var(--accent) 40%, transparent)", textAlign: "right",
            cursor: "pointer", color: "var(--t1)", fontFamily: "inherit",
          }}>
            <div style={{ fontSize: 11, color: "var(--t3)" }}>安防</div>
            <div style={{ fontWeight: 700, color: hub && !hub.armed ? "var(--t3)" : "var(--on)" }}>
              {hub ? (hub.armed ? "已布防" : "撤防") : "已布防"}
            </div>
          </button>
        </div>
      </div>
      <div style={{ flex: 1, padding: "16px 18px", display: "grid", gridTemplateColumns: "1.2fr 1fr", gap: 10 }}>
        <button type="button" onClick={() => hub && hub.openRoom("living")} style={{
          gridRow: "span 2", borderRadius: 20, padding: 16,
          background: "linear-gradient(160deg, color-mix(in srgb, var(--accent) 28%, transparent), var(--bg-card))",
          border: "1px solid var(--line)", display: "flex", flexDirection: "column",
          cursor: "pointer", color: "var(--t1)", fontFamily: "inherit", textAlign: "left",
        }}>
          <LIco.snow width="28" height="28" style={{ color: "var(--cool)" }} />
          <div style={{ flex: 1 }} />
          <div style={{ fontSize: 13, color: "var(--t3)" }}>客厅空调</div>
          <div style={{ fontSize: 40, fontWeight: 300 }}>{d.acOn ? d.acSp : "--"}°</div>
          <div style={{ fontSize: 12, color: "var(--accent)", fontWeight: 700 }}>
            {d.acOn ? `${d.acMode === "cool" ? "制冷" : d.acMode} · Auto` : "关闭"}
          </div>
        </button>
        <button type="button" style={duskCard} onClick={() => hub && hub.openRoom("living")}>
          <LIco.bulb width="22" height="22" style={{ color: "var(--warm)" }} />
          <div style={{ marginTop: 10, fontWeight: 700 }}>灯光</div>
          <div style={{ fontSize: 12, color: "var(--t3)" }}>{lightsOn} / 4 房间</div>
        </button>
        <button type="button" style={duskCard} onClick={() => hub && hub.openRoom("living")}>
          <LIco.curtain width="22" height="22" style={{ color: "var(--violet)" }} />
          <div style={{ marginTop: 10, fontWeight: 700 }}>窗帘</div>
          <div style={{ fontSize: 12, color: "var(--t3)" }}>{d.curtain}% 开</div>
        </button>
      </div>
      <div style={{ padding: "0 18px 8px", display: "flex", gap: 8 }}>
        {[
          ["回家", "home"], ["观影", "movie"], ["睡眠模式", "sleep"],
        ].map(([label, id]) => (
          <button key={id} type="button" onClick={() => hub && hub.applyScene(id)} style={{
            flex: 1, height: 40, borderRadius: 14, border: "1px solid var(--line)",
            background: hub && hub.activeScene === id ? "var(--accent)" : "rgba(255,255,255,0.04)",
            color: hub && hub.activeScene === id ? "var(--ink-on-accent)" : "var(--t1)",
            fontWeight: 700, fontSize: 12, cursor: "pointer", fontFamily: "inherit",
          }}>{label}</button>
        ))}
      </div>
      <div style={{ padding: "0 18px 16px" }}><HubQuickBar dense /></div>
    </div>
  );
}

/** Ocean — ops dashboard */
function ThemeHubOcean({ clock }) {
  const hub = useHubOrDemo();
  const t = hubTime(hub, clock);
  const buses = hub
    ? hub.protocols.filter((p) => p.id !== "wifi").map((p) => ({
      n: p.name.replace(" Broker", "").replace(" Bus A", ""),
      v: p.ok ? (p.id === "mqtt" ? 98 : p.id === "modbus" ? 86 : 72) : 12,
      ok: p.ok,
      id: p.id,
    }))
    : [
      { n: "MQTT", v: 98, ok: true, id: "mqtt" },
      { n: "Modbus", v: 86, ok: true, id: "modbus" },
      { n: "RS485", v: 12, ok: false, id: "rs485" },
    ];
  const online = hub ? hub.metrics.onlinePts : 42;
  return (
    <div className="lv">
      <div className="lv-head">
        <div className="clock">{t}</div>
        <div className="room">Ocean Hub</div>
        <div className="wx"><SoftPill ok={hub ? hub.network.wifiOn : true} onClick={() => hub && hub.go("network")}>
          {hub && !hub.network.wifiOn ? "OFF" : "LAN"}
        </SoftPill></div>
      </div>
      <div className="lv-body" style={{ display: "flex", flexDirection: "column", gap: 12 }}>
        <button type="button" onClick={() => hub && hub.go("energy")} style={{
          display: "grid", gridTemplateColumns: "1fr 1fr", gap: 8,
          background: "var(--bg-card)", border: "1px solid var(--line)", borderRadius: "var(--radius)", padding: 12,
          color: "var(--t1)", cursor: "pointer", fontFamily: "inherit", textAlign: "left",
        }}>
          <div>
            <div style={{ fontSize: 11, color: "var(--t3)" }}>整屋功率</div>
            <div style={{ fontSize: 28, fontWeight: 600, color: "var(--accent)" }}>
              {hub ? hub.powerKw.toFixed(2) : "3.02"} <span style={{ fontSize: 13 }}>kW</span>
            </div>
          </div>
          <div>
            <div style={{ fontSize: 11, color: "var(--t3)" }}>在线设备</div>
            <div style={{ fontSize: 28, fontWeight: 600 }}>{online} <span style={{ fontSize: 13, color: "var(--t3)" }}>/ 48</span></div>
          </div>
        </button>
        <div style={{ fontSize: 12, fontWeight: 700, color: "var(--t2)" }}>协议健康度 · 点击切换</div>
        {buses.map((b) => (
          <button key={b.n} type="button" onClick={() => hub && hub.toggleProto(b.id)} style={{
            background: "none", border: "none", padding: 0, cursor: "pointer", color: "var(--t1)",
            fontFamily: "inherit", textAlign: "left", width: "100%",
          }}>
            <div style={{ display: "flex", justifyContent: "space-between", fontSize: 12, marginBottom: 4 }}>
              <span style={{ fontWeight: 700 }}>{b.n}</span>
              <span style={{ color: b.ok ? "var(--on)" : "var(--alert)" }}>{b.ok ? `${b.v}%` : "FAULT"}</span>
            </div>
            <div style={{ height: 8, borderRadius: 2, background: "rgba(255,255,255,0.08)" }}>
              <div style={{ width: `${b.v}%`, height: "100%", borderRadius: 2, background: b.ok ? "var(--accent)" : "var(--alert)" }} />
            </div>
          </button>
        ))}
        <div style={{ display: "grid", gridTemplateColumns: "1fr 1fr 1fr", gap: 8, marginTop: "auto" }}>
          {[
            ["房间", <LIco.home width="20" height="20" />, () => hub && hub.openRoom("living")],
            ["点表", <LIco.layers width="20" height="20" />, () => hub && hub.go("points")],
            ["设置", <LIco.cog width="20" height="20" />, () => hub && hub.go("settings")],
          ].map(([l, ic, fn]) => (
            <button key={l} type="button" onClick={fn} style={{
              height: 56, borderRadius: "var(--radius)", border: "1px solid var(--line)", background: "var(--bg-card)",
              color: "var(--t1)", display: "flex", flexDirection: "column", alignItems: "center",
              justifyContent: "center", gap: 4, fontSize: 11, fontWeight: 700, cursor: "pointer", fontFamily: "inherit",
            }}>{ic}{l}</button>
          ))}
        </div>
        <HubQuickBar dense />
      </div>
    </div>
  );
}

/** Zen — 简约：大时钟 + 单焦点房间 + 两枚文字按钮 */
function ThemeHubZen({ clock }) {
  const hub = useHubOrDemo();
  const t = hubTime(hub, clock);
  const room = hub ? hub.rooms[0] : { id: "living", name: "客厅" };
  const on = hub ? hub.roomActive(room.id) : true;
  const d = hub ? hub.devices.living : { acSp: 24, acOn: true };
  return (
    <div className="lv">
      <div style={{ padding: "36px 32px 0", display: "flex", justifyContent: "space-between", alignItems: "flex-start" }}>
        <div>
          <div style={{ fontSize: 11, fontWeight: 600, letterSpacing: "0.2em", color: "var(--t3)" }}>ZEN</div>
          <div style={{ fontSize: 56, fontWeight: 200, letterSpacing: "-0.04em", lineHeight: 1, marginTop: 8, whiteSpace: "nowrap" }}>{t}</div>
        </div>
        <button type="button" onClick={() => hub && hub.go("security")} style={{
          background: "none", border: "none", color: "var(--t3)", fontSize: 11, fontWeight: 600,
          cursor: "pointer", letterSpacing: "0.08em", padding: 0, fontFamily: "inherit",
        }}>{hub && hub.armed ? "ARMED" : "HOME"}</button>
      </div>
      <div style={{ flex: 1, padding: "28px 32px", display: "flex", flexDirection: "column", justifyContent: "center" }}>
        <div style={{ height: 1, background: "var(--line)", marginBottom: 28 }} />
        <button type="button" onClick={() => hub && hub.openRoom(room.id)} style={{
          background: "none", border: "none", padding: 0, cursor: "pointer", textAlign: "left",
          color: "var(--t1)", fontFamily: "inherit", width: "100%",
        }}>
          <div style={{ fontSize: 13, color: "var(--t3)", fontWeight: 600, letterSpacing: "0.06em" }}>FOCUS ROOM</div>
          <div style={{ fontSize: 36, fontWeight: 300, marginTop: 6 }}>{hub ? hub.roomLabel(room) : room.name}</div>
          <div style={{ fontSize: 14, color: "var(--t2)", marginTop: 8 }}>
            {on ? "运行中" : "静音"} · {d.acOn ? `${d.acSp}°` : "空调关"}
            {hub ? ` · ${hub.powerKw.toFixed(1)}kW` : ""} · 点击进入
          </div>
        </button>
        <div style={{ height: 1, background: "var(--line)", marginTop: 28 }} />
        <div style={{ marginTop: 20 }}><HubQuickBar dense /></div>
      </div>
      <div style={{ display: "flex", borderTop: "1px solid var(--line)" }}>
        {[
          ["设置", () => hub && hub.go("settings")],
          ["情景", () => hub && hub.go("scenes")],
          ["全部关闭", () => hub && hub.applyScene("alloff")],
        ].map(([label, fn], i) => (
          <button key={label} type="button" onClick={fn} style={{
            flex: 1, height: 56, border: "none",
            borderLeft: i ? "1px solid var(--line)" : "none",
            background: i === 2 ? "var(--t1)" : "transparent",
            color: i === 2 ? "var(--ink-on-accent)" : "var(--t1)",
            fontWeight: 600, fontSize: 13, cursor: "pointer", fontFamily: "inherit",
            letterSpacing: "0.04em",
          }}>{label}</button>
        ))}
      </div>
    </div>
  );
}

/** Pulse — 科技：HUD 框 + 遥测条 + 对角导航 */
function ThemeHubPulse({ clock }) {
  const hub = useHubOrDemo();
  const t = hubTime(hub, clock);
  const power = hub ? hub.powerKw.toFixed(2) : "2.40";
  const ok = hub ? hub.okCount : 3;
  const total = hub ? hub.protocols.length : 4;
  const hud = {
    border: "1px solid var(--line)",
    background: "var(--bg-card)",
    clipPath: "polygon(0 0, calc(100% - 8px) 0, 100% 8px, 100% 100%, 8px 100%, 0 calc(100% - 8px))",
  };
  return (
    <div className="lv">
      <div className="lv-head" style={{ fontFamily: "var(--font-m)" }}>
        <div className="clock" style={{ color: "var(--accent)", fontSize: 18 }}>{t}</div>
        <div className="room" style={{ letterSpacing: "0.14em", fontSize: 11 }}>PULSE // NODE</div>
        <div className="wx" style={{ fontSize: 10, color: "var(--on)", fontFamily: "var(--font-m)" }}>LINK {ok}/{total}</div>
      </div>
      <div className="lv-body" style={{ display: "flex", flexDirection: "column", gap: 10, fontFamily: "var(--font-m)" }}>
        <div style={{ display: "grid", gridTemplateColumns: "1.2fr 1fr", gap: 8 }}>
          <button type="button" onClick={() => hub && hub.go("energy")} style={{ ...hud, padding: 14, textAlign: "left", color: "var(--t1)", cursor: "pointer", fontFamily: "inherit" }}>
            <div style={{ fontSize: 10, color: "var(--accent)", letterSpacing: "0.12em" }}>PWR_DRAW</div>
            <div style={{ fontSize: 32, fontWeight: 600, color: "var(--accent)", marginTop: 4 }}>{power}<span style={{ fontSize: 12, marginLeft: 4 }}>kW</span></div>
            <div style={{ height: 4, marginTop: 10, background: "rgba(0,240,255,0.12)" }}>
              <div style={{ width: `${Math.min(100, Number(power) * 28)}%`, height: "100%", background: "var(--accent)", boxShadow: "0 0 8px var(--accent)" }} />
            </div>
          </button>
          <button type="button" onClick={() => hub && hub.go("gateway")} style={{ ...hud, padding: 14, textAlign: "left", color: "var(--t1)", cursor: "pointer", fontFamily: "inherit" }}>
            <div style={{ fontSize: 10, color: "var(--violet)", letterSpacing: "0.12em" }}>BUS_HEALTH</div>
            {(hub ? hub.protocols.slice(0, 3) : [{ id: "m", ok: true }, { id: "b", ok: true }, { id: "r", ok: false }]).map((p, i) => (
              <div key={p.id || i} style={{ display: "flex", justifyContent: "space-between", fontSize: 11, marginTop: 6 }}>
                <span style={{ color: "var(--t3)" }}>{(p.id || "x").toUpperCase()}</span>
                <span style={{ color: p.ok ? "var(--on)" : "var(--alert)" }}>{p.ok ? "OK" : "ERR"}</span>
              </div>
            ))}
          </button>
        </div>
        <div style={{ fontSize: 10, color: "var(--t3)", letterSpacing: "0.14em" }}>ZONE_SELECT</div>
        <div style={{ display: "grid", gridTemplateColumns: "1fr 1fr", gap: 8, flex: 1 }}>
          {(hub ? hub.rooms : [
            { id: "living", name: "客厅" }, { id: "bed", name: "主卧" },
            { id: "kitchen", name: "厨房" }, { id: "study", name: "书房" },
          ]).map((r, i) => {
            const active = hub ? hub.roomActive(r.id) : i % 2 === 0;
            return (
              <button key={r.id} type="button" onClick={() => hub && hub.openRoom(r.id)} style={{
                ...hud, padding: 12, textAlign: "left", cursor: "pointer", fontFamily: "inherit",
                color: "var(--t1)", borderColor: active ? "var(--accent)" : "var(--line)",
                boxShadow: active ? "inset 0 0 0 1px var(--accent)" : "none",
              }}>
                <div style={{ fontSize: 10, color: "var(--accent)" }}>Z{i + 1}</div>
                <div style={{ fontSize: 16, fontWeight: 700, marginTop: 4 }}>{r.name}</div>
                <div style={{ fontSize: 10, color: active ? "var(--on)" : "var(--t4)", marginTop: 4 }}>{active ? "ACTIVE" : "IDLE"}</div>
              </button>
            );
          })}
        </div>
        <HubQuickBar dense />
        <div style={{ display: "flex", gap: 8 }}>
          <button type="button" onClick={() => hub && hub.applyScene("home")} style={{ ...hud, flex: 1, height: 36, color: "var(--accent)", fontWeight: 700, cursor: "pointer", fontFamily: "inherit", fontSize: 11, letterSpacing: "0.1em" }}>SCENE_HOME</button>
          <button type="button" onClick={() => hub && hub.applyScene("away")} style={{ ...hud, flex: 1, height: 36, color: "var(--alert)", fontWeight: 700, cursor: "pointer", fontFamily: "inherit", fontSize: 11, letterSpacing: "0.1em" }}>SCENE_AWAY</button>
          <button type="button" onClick={() => hub && hub.go("settings")} style={{ ...hud, flex: 1, height: 36, color: "var(--t2)", fontWeight: 700, cursor: "pointer", fontFamily: "inherit", fontSize: 11, letterSpacing: "0.1em" }}>CFG</button>
        </div>
      </div>
    </div>
  );
}

/** Bloom — 柔和：大圆角气泡入口 + 温馨情景 */
function ThemeHubBloom({ clock }) {
  const hub = useHubOrDemo();
  const t = hubTime(hub, clock);
  const rooms = hub ? hub.rooms.slice(0, 4) : [
    { id: "living", name: "客厅" }, { id: "bed", name: "主卧" },
    { id: "kitchen", name: "厨房" }, { id: "study", name: "书房" },
  ];
  const hues = ["#e891a8", "#7eb8c9", "#e8a06a", "#b8a0d0"];
  return (
    <div className="lv">
      <div style={{ padding: "28px 24px 8px" }}>
        <div style={{ display: "flex", justifyContent: "space-between", alignItems: "center" }}>
          <div>
            <div style={{ fontSize: 13, color: "var(--accent)", fontWeight: 700 }}>Bloom</div>
            <div style={{ fontSize: 40, fontWeight: 300, letterSpacing: "-0.02em" }}>{t}</div>
          </div>
          <button type="button" onClick={() => hub && hub.go("hvac")} style={{
            width: 64, height: 64, borderRadius: 999, border: "1px solid var(--line)",
            background: "var(--bg-card)", color: "var(--cool)", cursor: "pointer",
            display: "flex", flexDirection: "column", alignItems: "center", justifyContent: "center",
            fontFamily: "inherit", boxShadow: "0 8px 20px rgba(90,60,80,0.08)",
          }}>
            <LIco.snow width="20" height="20" />
            <span style={{ fontSize: 11, fontWeight: 700, marginTop: 2 }}>
              {hub && hub.devices.living.acOn ? `${hub.devices.living.acSp}°` : "--"}
            </span>
          </button>
        </div>
      </div>
      <div style={{ flex: 1, padding: "8px 20px", display: "grid", gridTemplateColumns: "1fr 1fr", gap: 12, alignContent: "start" }}>
        {rooms.map((r, i) => {
          const on = hub ? hub.roomActive(r.id) : i !== 1;
          return (
            <button key={r.id} type="button" onClick={() => hub && hub.openRoom(r.id)} style={{
              minHeight: 96, borderRadius: 28, border: "1px solid rgba(255,255,255,0.7)",
              background: on
                ? `linear-gradient(145deg, ${window.A(hues[i], 0.28)}, var(--bg-card))`
                : "var(--bg-card)",
              boxShadow: "0 10px 28px rgba(90,60,80,0.07)",
              padding: 16, textAlign: "left", cursor: "pointer", color: "var(--t1)", fontFamily: "inherit",
            }}>
              <div style={{
                width: 36, height: 36, borderRadius: 999, background: window.A(hues[i], 0.2),
                color: hues[i], display: "flex", alignItems: "center", justifyContent: "center",
              }}><LIco.home width="18" height="18" /></div>
              <div style={{ fontWeight: 700, fontSize: 16, marginTop: 12 }}>{r.name}</div>
              <div style={{ fontSize: 12, color: "var(--t3)", marginTop: 2 }}>{on ? "柔光开启" : "已入睡"}</div>
            </button>
          );
        })}
      </div>
      <div style={{ padding: "0 20px 6px" }}><HubQuickBar dense /></div>
      <div style={{ display: "flex", gap: 10, padding: "4px 20px 16px" }}>
        {[
          ["回家", "home"], ["观影", "movie"], ["晚安", "sleep"],
        ].map(([label, id]) => (
          <button key={id} type="button" onClick={() => hub && hub.applyScene(id)} style={{
            flex: 1, height: 40, borderRadius: 999, border: "none", cursor: "pointer", fontFamily: "inherit",
            background: hub && hub.activeScene === id ? "var(--accent)" : "var(--bg-card)",
            color: hub && hub.activeScene === id ? "var(--ink-on-accent)" : "var(--t1)",
            fontWeight: 700, fontSize: 13, boxShadow: "0 4px 14px rgba(90,60,80,0.08)",
          }}>{label}</button>
        ))}
      </div>
    </div>
  );
}

/** Metro — 色块：马赛克磁贴导航 */
function ThemeHubMetro({ clock }) {
  const hub = useHubOrDemo();
  const t = hubTime(hub, clock);
  const tiles = [
    { id: "living", label: hub ? hub.roomLabel(hub.rooms[0]) : "客厅", sub: hub && hub.roomActive("living") ? "运行" : "待机", color: "#00bcf2", span: 2, fn: () => hub && hub.openRoom("living") },
    { id: "bed", label: hub ? hub.roomLabel(hub.rooms[1]) : "主卧", sub: hub && hub.roomActive("bed") ? "运行" : "待机", color: "#8764b8", span: 1, fn: () => hub && hub.openRoom("bed") },
    { id: "kitchen", label: hub ? hub.roomLabel(hub.rooms[2]) : "厨房", sub: "4 pts", color: "#107c10", span: 1, fn: () => hub && hub.openRoom("kitchen") },
    { id: "scenes", label: "情景", sub: hub ? hub.activeScene : "home", color: "#ff8c00", span: 1, fn: () => hub && hub.go("scenes") },
    { id: "bus", label: "总线", sub: hub ? `${hub.okCount}/${hub.protocols.length}` : "3/4", color: "#e74856", span: 1, fn: () => hub && hub.go("gateway") },
    { id: "energy", label: "能耗", sub: hub ? `${hub.powerKw.toFixed(1)} kW` : "2.4 kW", color: "#0078d4", span: 1, fn: () => hub && hub.go("energy") },
    { id: "sec", label: "安防", sub: hub && hub.armed ? "布防" : "撤防", color: "#5d5a58", span: 1, fn: () => hub && hub.go("security") },
  ];
  return (
    <div className="lv">
      <div className="lv-head">
        <div className="clock" style={{ fontSize: 16 }}>{t}</div>
        <div className="room" style={{ fontWeight: 600 }}>Metro Hub</div>
        <button type="button" className="wx" onClick={() => hub && hub.go("settings")} style={{
          background: "none", border: "none", color: "var(--t2)", cursor: "pointer", fontSize: 11, fontWeight: 700,
        }}>设置</button>
      </div>
      <div className="lv-body" style={{ display: "grid", gridTemplateColumns: "1fr 1fr 1fr", gridTemplateRows: "1.25fr 1fr 1fr", gap: 6, paddingTop: 2, minHeight: 0 }}>
        {tiles.map((tile) => (
          <button
            key={tile.id}
            type="button"
            onClick={tile.fn}
            style={{
              gridColumn: tile.span === 2 ? "span 2" : "span 1",
              background: tile.color,
              border: "none",
              color: "#fff",
              padding: 14,
              cursor: "pointer",
              textAlign: "left",
              fontFamily: "inherit",
              display: "flex",
              flexDirection: "column",
              justifyContent: "flex-end",
              minHeight: 0,
            }}
          >
            <div style={{ fontSize: tile.span === 2 ? 28 : 16, fontWeight: 600, lineHeight: 1.1 }}>{tile.label}</div>
            <div style={{ fontSize: 12, opacity: 0.85, marginTop: 4 }}>{tile.sub}</div>
          </button>
        ))}
      </div>
      <div style={{ padding: "6px 8px 8px" }}><HubQuickBar dense /></div>
    </div>
  );
}

const THEME_META = [
  { id: "slate", label: "Slate", desc: "工业深灰 · 宫格房间", Hub: ThemeHubSlate },
  { id: "sand", label: "Sand", desc: "日间浅色 · 列表导航", Hub: ThemeHubSand },
  { id: "ink", label: "Ink", desc: "高对比 mono · 矩阵 IO", Hub: ThemeHubInk },
  { id: "forest", label: "Forest", desc: "节能环形 · 情景胶囊", Hub: ThemeHubForest },
  { id: "dusk", label: "Dusk", desc: "晚间玻璃卡 · 安防", Hub: ThemeHubDusk },
  { id: "ocean", label: "Ocean", desc: "运维仪表 · 协议健康", Hub: ThemeHubOcean },
  { id: "zen", label: "Zen", desc: "简约留白 · 字重焦点", Hub: ThemeHubZen },
  { id: "pulse", label: "Pulse", desc: "科技 HUD · 遥测切割", Hub: ThemeHubPulse },
  { id: "bloom", label: "Bloom", desc: "柔和雾面 · 圆角气泡", Hub: ThemeHubBloom },
  { id: "metro", label: "Metro", desc: "色块磁贴 · 强色导航", Hub: ThemeHubMetro },
];

Object.assign(window, {
  ThemeScope, THEME_META, HubQuickBar,
  ThemeHubSlate, ThemeHubSand, ThemeHubInk, ThemeHubForest, ThemeHubDusk, ThemeHubOcean,
  ThemeHubZen, ThemeHubPulse, ThemeHubBloom, ThemeHubMetro,
});
