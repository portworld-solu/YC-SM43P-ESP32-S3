/* eslint-disable */
// Interactive hub runtime — shared state + logical navigation for every theme.
const { useState, useCallback, useEffect, useMemo } = React;

function hubClockNow() {
  const d = new Date();
  return `${String(d.getHours()).padStart(2, "0")}:${String(d.getMinutes()).padStart(2, "0")}`;
}

function HubProvider({ children, initialRoute = "home" }) {
  const [route, setRoute] = useState(initialRoute);
  const [roomId, setRoomId] = useState("living");
  const [roomWidgets, setRoomWidgets] = useState(makeInitialRoomWidgets);
  const devices = useMemo(() => devicesFromWidgets(roomWidgets), [roomWidgets]);
  const [protocols, setProtocols] = useState(makeProtocols);
  const [armed, setArmed] = useState(true);
  const [keypad, setKeypad] = useState(false);
  const [activeScene, setActiveScene] = useState("home");
  const [toast, setToast] = useState("");
  const [powerKw, setPowerKw] = useState(() => estimatePowerWidgets(makeInitialRoomWidgets()));
  const [clock, setClock] = useState(hubClockNow);
  const [wifiJoin, setWifiJoin] = useState(null);
  const [settings, setSettings] = useState({
    brightness: 72, nightMode: false, lang: "en", clickSound: true,
    standbyEn: true, standbyTimeoutSec: 120,
  });
  /* Sensor calibration offsets (demo; parity with hub SENSOR_CAL). */
  const [sensorCal, setSensorCal] = useState({ tempOffset: 0, rhOffset: 0 });
  const [network, setNetwork] = useState({
    wifiOn: true, ethOn: false, ssid: "Office-AP", ip: "192.168.1.42", ethIp: "192.168.1.10",
  });
  const [schedules, setSchedules] = useState([
    { id: "s1", time: "07:00", title: "晨起唤醒", days: "周一至周五", action: "情景 · 回家", sceneId: "home", on: true },
    { id: "s2", time: "22:30", title: "夜间模式", days: "每天", action: "情景 · 睡眠", sceneId: "sleep", on: true },
    { id: "s3", time: "09:00", title: "离家布防", days: "工作日", action: "情景 · 离家", sceneId: "away", on: false },
  ]);

  useEffect(() => {
    const t = setInterval(() => setClock(hubClockNow()), 15000);
    return () => clearInterval(t);
  }, []);

  const flash = useCallback((msg) => {
    setToast(msg);
    clearTimeout(flash._t);
    flash._t = setTimeout(() => setToast(""), 1600);
  }, []);

  const go = useCallback((r, opts = {}) => {
    if (opts.roomId) setRoomId(opts.roomId);
    setRoute(r);
    setKeypad(false);
  }, []);

  const openRoom = useCallback((id) => {
    setRoomId(id || "living");
    setRoute("room");
  }, []);

  const commitWidgets = useCallback((updater) => {
    setRoomWidgets((prev) => {
      const next = typeof updater === "function" ? updater(prev) : updater;
      setPowerKw(estimatePowerWidgets(next));
      return next;
    });
  }, []);

  const patchRoom = useCallback((id, patch) => {
    commitWidgets((prev) => ({
      ...prev,
      [id]: mapLegacyPatch(prev[id] || [], patch),
    }));
  }, [commitWidgets]);

  const patchWidget = useCallback((room, wid, patch) => {
    commitWidgets((prev) => ({
      ...prev,
      [room]: (prev[room] || []).map((w) => (w.id === wid ? { ...w, ...patch } : w)),
    }));
  }, [commitWidgets]);

  const setWidgetEnabled = useCallback((room, wid, enabled) => {
    patchWidget(room, wid, { enabled });
    flash(enabled ? "控件已显示" : "控件已隐藏");
  }, [patchWidget, flash]);

  const renameWidget = useCallback((room, wid, name) => {
    patchWidget(room, wid, { name, nameEn: name });
    flash("已重命名");
  }, [patchWidget, flash]);

  const removeWidget = useCallback((room, wid) => {
    commitWidgets((prev) => ({
      ...prev,
      [room]: (prev[room] || []).filter((w) => w.id !== wid),
    }));
    flash("已移除控件");
  }, [commitWidgets, flash]);

  const addWidget = useCallback((room, type) => {
    let blocked = false;
    commitWidgets((prev) => {
      const list = prev[room] || [];
      if (list.length >= 8) {
        blocked = true;
        return prev;
      }
      return { ...prev, [room]: [...list, createWidget(type)] };
    });
    flash(blocked ? "每房间最多 8 个控件" : "已添加控件");
  }, [commitWidgets, flash]);

  const beginWifiJoin = useCallback((ap) => {
    if (!ap) return;
    if (!ap.secure) {
      const ip = "192.168.1." + (20 + Math.abs(ap.rssi) % 40);
      setNetwork((n) => ({ ...n, wifiOn: true, ssid: ap.ssid, ip }));
      setProtocols((rows) => rows.map((r) => (r.id === "wifi" ? { ...r, ok: true, detail: `${ap.ssid} · ${ap.rssi} dBm` } : r)));
      setWifiJoin(null);
      flash(`已连接开放网络 ${ap.ssid}`);
      return;
    }
    setWifiJoin({ ssid: ap.ssid, rssi: ap.rssi, secure: true, status: "input", error: "" });
  }, [flash]);

  const cancelWifiJoin = useCallback(() => setWifiJoin(null), []);

  const submitWifiPassword = useCallback((password) => {
    if (!wifiJoin) return;
    const pwd = (password || "").trim();
    if (pwd.length < 8) {
      setWifiJoin((j) => j && ({ ...j, status: "input", error: "密码至少 8 位" }));
      return;
    }
    setWifiJoin((j) => j && ({ ...j, status: "connecting", error: "" }));
    const ap = wifiJoin;
    setTimeout(() => {
      // Demo rule: password "12345678" or length>=8 ending with "8" succeeds; "00000000" fails
      const ok = pwd !== "00000000" && (pwd === "12345678" || pwd.length >= 8);
      if (!ok) {
        setWifiJoin((j) => j && ({ ...j, status: "input", error: "密码错误，请重试" }));
        flash("连接失败");
        return;
      }
      const ip = "192.168.1." + (20 + Math.abs(ap.rssi) % 40);
      setNetwork((n) => ({ ...n, wifiOn: true, ssid: ap.ssid, ip }));
      setProtocols((rows) => rows.map((r) => (r.id === "wifi" ? { ...r, ok: true, detail: `${ap.ssid} · ${ap.rssi} dBm` } : r)));
      setWifiJoin(null);
      flash(`已连接 ${ap.ssid}`);
    }, 900);
  }, [wifiJoin, flash]);

  const toggleProto = useCallback((id) => {
    if (id === "wifi") {
      setNetwork((n) => {
        const wifiOn = !n.wifiOn;
        flash(wifiOn ? "Wi-Fi 已开启" : "Wi-Fi 已关闭");
        return { ...n, wifiOn };
      });
    }
    setProtocols((rows) => rows.map((r) => {
      if (r.id !== id) return r;
      const ok = !r.ok;
      let detail = r.detail;
      if (r.id === "rs485") detail = ok ? "9600 8N1 · 8 nodes" : "9600 8N1 · timeout";
      if (r.id === "wifi") detail = ok ? "Office-AP · -58 dBm" : "disconnected";
      return { ...r, ok, detail };
    }));
    if (id !== "wifi") flash("已更新总线状态");
  }, [flash]);

  const applyScene = useCallback((sceneId) => {
    const sid = sceneId === "alloff" ? "away" : sceneId;
    setActiveScene(sid);
    if (sceneId === "away" || sceneId === "alloff" || sceneId === "sleep") setArmed(true);
    else if (sceneId === "home" || sceneId === "comfort" || sceneId === "movie" || sceneId === "guest") setArmed(false);
    commitWidgets((prev) => applySceneToWidgets(prev, sceneId));
    const tips = {
      away: "离家：全关 + 布防", alloff: "全关已执行", home: "回家模式已执行", comfort: "回家模式已执行",
      movie: "观影：灯光调暗 · 窗帘关闭", sleep: "睡眠模式已执行", eco: "节能：降低负荷", guest: "会客模式已执行",
    };
    flash(tips[sceneId] || "情景已执行");
  }, [flash, commitWidgets]);

  const roomActive = useCallback((id) => roomWidgetsActive(roomWidgets[id]), [roomWidgets]);

  const okCount = protocols.filter((p) => p.ok).length;
  const metrics = useMemo(() => computeMetricsWidgets(roomWidgets), [roomWidgets]);

  const patchSettings = useCallback((patch) => {
    setSettings((prev) => {
      const next = { ...prev, ...patch };
      if (patch.nightMode === true && prev.nightMode === false) {
        flash("夜间模式：已降亮");
      }
      return next;
    });
  }, [flash]);

  const patchNetwork = useCallback((patch) => {
    setNetwork((prev) => {
      const next = { ...prev, ...patch };
      if (Object.prototype.hasOwnProperty.call(patch, "wifiOn") || patch.ssid) {
        setProtocols((rows) => rows.map((r) => {
          if (r.id !== "wifi") return r;
          const ok = next.wifiOn;
          return {
            ...r,
            ok,
            detail: ok ? `${next.ssid} · -58 dBm` : "disconnected",
          };
        }));
      }
      return next;
    });
  }, []);

  const toggleSchedule = useCallback((id) => {
    setSchedules((rows) => rows.map((r) => (r.id === id ? { ...r, on: !r.on } : r)));
    flash("日程已更新");
  }, [flash]);

  const runSchedule = useCallback((id) => {
    setSchedules((rows) => {
      const item = rows.find((r) => r.id === id);
      if (item && item.sceneId) {
        setTimeout(() => {
          applyScene(item.sceneId);
          flash(`执行日程：${item.title}`);
        }, 0);
      }
      return rows;
    });
  }, [applyScene, flash]);

  const roomLabel = useCallback((r) => {
    if (!r) return "";
    return settings.lang === "en" ? (r.nameEn || r.name) : r.name;
  }, [settings.lang]);

  const sceneLabel = useCallback((id) => {
    const zh = settings.lang !== "en";
    const map = {
      home: zh ? "回家" : "Home", away: zh ? "离家" : "Away", movie: zh ? "观影" : "Movie",
      sleep: zh ? "睡眠" : "Sleep", guest: zh ? "会客" : "Guest", eco: zh ? "节能" : "Eco",
      morning: zh ? "晨起" : "Morning", alloff: zh ? "全关" : "All off",
    };
    return map[id] || id || "";
  }, [settings.lang]);

  const screenFilter = useMemo(() => {
    const b = (settings.brightness / 100) * (settings.nightMode ? 0.7 : 1);
    return `brightness(${Math.max(0.35, Math.min(1.15, b))})`;
  }, [settings.brightness, settings.nightMode]);

  const value = {
    route, go, openRoom, roomId, rooms: ROOMS,
    devices, roomWidgets, patchRoom, patchWidget,
    setWidgetEnabled, renameWidget, removeWidget, addWidget,
    roomActive, roomLabel, sceneLabel,
    protocols, toggleProto, okCount,
    armed, setArmed, keypad, setKeypad,
    activeScene, applyScene,
    toast, flash, powerKw, setPowerKw,
    settings, patchSettings, network, patchNetwork,
    sensorCal, setSensorCal,
    wifiJoin, beginWifiJoin, cancelWifiJoin, submitWifiPassword,
    schedules, toggleSchedule, runSchedule,
    clock, metrics, screenFilter,
  };

  return <HubCtx.Provider value={value}>{children}</HubCtx.Provider>;
}

const ROOMS = [
  { id: "living", name: "客厅", nameEn: "Living", devices: 6 },
  { id: "bed", name: "主卧", nameEn: "Bedroom", devices: 4 },
  { id: "kitchen", name: "厨房", nameEn: "Kitchen", devices: 4 },
  { id: "study", name: "书房", nameEn: "Study", devices: 3 },
];

function makeProtocols() {
  return [
    { id: "mqtt", name: "MQTT Broker", detail: "mqtt://192.168.1.8:1883", ok: true },
    { id: "modbus", name: "Modbus TCP", detail: "192.168.1.20:502 · 12 slaves", ok: true },
    { id: "rs485", name: "RS485 Bus A", detail: "9600 8N1", ok: false },
    { id: "wifi", name: "Wi-Fi STA", detail: "Office-AP · -58 dBm", ok: true },
  ];
}

function renderRoomWidget(w, hub, ambient, hints = {}) {
  const zh = hub.settings.lang !== "en";
  const name = zh ? w.name : (w.nameEn || w.name);
  const patch = (p) => hub.patchWidget(hub.roomId, w.id, p);
  const compact = hints.full ? false : (hints.compact !== false);
  const page = !!hints.page;
  const wide = !!hints.wide;
  switch (w.type) {
    case "onoff":
      return <OnOffWidget name={name} icon={<LIco.bulb />} on={w.on} compact={compact} page={page} onChange={(on) => patch({ on })} />;
    case "plug":
      return <OnOffWidget name={name} icon={<LIco.plug2 />} on={w.on} compact={compact} page={page} hue="#818cf8" onChange={(on) => patch({ on })} />;
    case "fan":
      return <OnOffWidget name={name} icon={<LIco.fan />} on={w.on} compact={compact} page={page} hue="#22d3ee" onChange={(on) => patch({ on })} />;
    case "dimmer":
      return <DimmerWidget name={name} level={w.level} on={w.on} compact={compact} page={page} onChange={({ on, level }) => patch({ on, level })} />;
    case "curtain":
      return <CurtainWidget name={name} position={w.position} compact={compact} page={page} onChangePos={(position) => patch({ position })} />;
    case "shutter":
      return <ShutterWidget name={name} position={w.position} compact={compact} page={page} onChangePos={(position) => patch({ position })} />;
    case "clim":
      return (
        <ClimWidget name={name} ambient={ambient} setpoint={w.setpoint} mode={w.on ? w.mode : "off"}
          compact={compact} wide={wide}
          onChange={({ mode, setpoint }) => patch({ mode, on: mode !== "off", setpoint: setpoint ?? w.setpoint })} />
      );
    case "thermo":
      return (
        <ThermostatArc name={name} ambient={Math.max(18, ambient - 2)} setpoint={w.setpoint} on={w.on} compact={compact}
          onChange={({ on, setpoint }) => patch({ on: on ?? w.on, setpoint: setpoint ?? w.setpoint })} />
      );
    default:
      return null;
  }
}

/** Prefer clim/thermo in Five's wide top slot (Smart-LVGL pattern) */
function orderWidgetsForPage(slice) {
  if (slice.length !== 5) return slice;
  const climate = slice.filter((w) => w.type === "clim" || w.type === "thermo");
  if (!climate.length) return slice;
  const rest = slice.filter((w) => w.type !== "clim" && w.type !== "thermo");
  return [...climate, ...rest];
}

/** Room page — adaptive grid by widget count + swipe pagination */
function HubRoomPage() {
  const hub = useHub();
  const { useState: useSt, useEffect: useEf } = React;
  const room = hub.rooms.find((r) => r.id === hub.roomId) || hub.rooms[0];
  const widgets = (hub.roomWidgets[room.id] || []).filter((w) => w.enabled !== false);
  const ambient = hub.metrics.indoor;
  const zh = hub.settings.lang !== "en";
  const pageSize = 6;
  const pageCount = Math.max(1, Math.ceil(widgets.length / pageSize) || 1);
  const [subPage, setSubPage] = useSt(0);

  useEf(() => { setSubPage(0); }, [room.id]);
  useEf(() => { setSubPage((p) => Math.min(p, pageCount - 1)); }, [pageCount]);

  const pageWidgets = orderWidgetsForPage(
    widgets.slice(subPage * pageSize, subPage * pageSize + pageSize)
  );
  const n = pageWidgets.length;
  const multi = widgets.length > pageSize;

  const swipeLeft = () => {
    if (subPage < pageCount - 1) setSubPage((p) => p + 1);
    else hub.go("scenes");
  };
  const swipeRight = () => {
    if (subPage > 0) setSubPage((p) => p - 1);
    else hub.go("home");
  };

  return (
    <SwipeSurface onSwipeLeft={swipeLeft} onSwipeRight={swipeRight} style={{ height: "100%" }}>
      <PageShell
        title={hub.roomLabel(room)}
        dots={multi ? pageCount : 4}
        active={multi ? subPage : 1}
        onBack={() => hub.go("home")}
        clock={hub.clock}
        wx={multi ? `${subPage + 1}/${pageCount}` : ""}
      >
        <div style={{ display: "flex", gap: 6, marginBottom: 6, alignItems: "center", flexShrink: 0 }}>
          <div style={{ display: "flex", gap: 6, flex: 1, overflow: "hidden" }}>
            {hub.rooms.map((r) => (
              <button key={r.id} type="button" onClick={() => hub.openRoom(r.id)} style={{
                flex: 1, minWidth: 0, height: 28, borderRadius: 6, fontSize: 11, fontWeight: 700, cursor: "pointer",
                border: "1px solid var(--line)",
                background: r.id === room.id ? "var(--accent)" : "var(--bg-card)",
                color: r.id === room.id ? "var(--ink-on-accent)" : "var(--t2)",
                fontFamily: "inherit",
              }}>{hub.roomLabel(r)}</button>
            ))}
          </div>
          <button type="button" onClick={() => hub.go("room-edit")} style={{
            height: 28, padding: "0 10px", borderRadius: 6, border: "1px solid var(--line)",
            background: "var(--bg-card)", color: "var(--accent)", fontWeight: 700, fontSize: 11,
            cursor: "pointer", fontFamily: "inherit", flexShrink: 0,
          }}>{zh ? "编辑" : "Edit"}</button>
        </div>
        {widgets.length === 0 ? (
          <div style={{
            flex: 1, minHeight: 0, display: "flex", flexDirection: "column", alignItems: "center", justifyContent: "center",
            color: "var(--t3)", gap: 12,
          }}>
            <div style={{ fontWeight: 700 }}>{zh ? "暂无控件" : "No widgets"}</div>
            <button type="button" className="basic-cta" style={{ width: 180 }} onClick={() => hub.go("room-edit")}>
              {zh ? "添加家居控件" : "Add controls"}
            </button>
          </div>
        ) : (
          <div style={{ flex: 1, minHeight: 0, display: "flex", flexDirection: "column" }}>
            <AdaptiveGrid key={room.id + "-" + subPage + "-" + widgets.map((w) => w.id).join(",")} pageIndex={0}>
              {pageWidgets.map((w, i) => (
                <React.Fragment key={w.id}>
                  {renderRoomWidget(w, hub, ambient, layoutHintsForCount(n, i, w.type))}
                </React.Fragment>
              ))}
            </AdaptiveGrid>
          </div>
        )}
      </PageShell>
    </SwipeSurface>
  );
}

/** Customize room widgets: enable / rename / add / remove */
function HubRoomEditPage() {
  const hub = useHub();
  const { useState: useSt } = React;
  const room = hub.rooms.find((r) => r.id === hub.roomId) || hub.rooms[0];
  const list = hub.roomWidgets[room.id] || [];
  const zh = hub.settings.lang !== "en";
  const [renameId, setRenameId] = useSt(null);
  const [renameVal, setRenameVal] = useSt("");

  return (
    <HubShell
      title={zh ? "编辑控件" : "Edit widgets"}
      onBack={() => hub.go("room")}
      right={<span style={{ fontSize: 11, color: "var(--t3)" }}>{list.filter((w) => w.enabled !== false).length}/{list.length}</span>}
    >
      <div className="eyebrow" style={{ marginBottom: 8, flexShrink: 0 }}>
        {hub.roomLabel(room)} · {zh ? "显示 / 命名 / 增删 · 可上下滑动" : "Show / rename / add · scroll"}
      </div>
      <ScrollPane>
        {list.map((w) => {
          const meta = WIDGET_CATALOG.find((c) => c.type === w.type);
          const label = zh ? w.name : (w.nameEn || w.name);
          return (
            <div key={w.id} className="sf-row basic-row" style={{
              width: "100%", marginBottom: 8, gap: 8, padding: "10px 12px", cursor: "default",
              opacity: w.enabled === false ? 0.55 : 1,
            }}>
              <div style={{ flex: 1, minWidth: 0 }}>
                {renameId === w.id ? (
                  <form onSubmit={(e) => {
                    e.preventDefault();
                    if (renameVal.trim()) hub.renameWidget(room.id, w.id, renameVal.trim());
                    setRenameId(null);
                  }} style={{ display: "flex", gap: 6 }}>
                    <input
                      value={renameVal}
                      onChange={(e) => setRenameVal(e.target.value)}
                      autoFocus
                      style={{
                        flex: 1, height: 32, borderRadius: 6, border: "1px solid var(--accent)",
                        background: "var(--bg-card-2)", color: "var(--t1)", padding: "0 8px", fontFamily: "inherit",
                      }}
                    />
                    <button type="submit" style={{
                      height: 32, padding: "0 10px", border: "none", borderRadius: 6,
                      background: "var(--accent)", color: "var(--ink-on-accent)", fontWeight: 700, cursor: "pointer",
                    }}>OK</button>
                  </form>
                ) : (
                  <React.Fragment>
                    <div style={{ fontSize: 14, fontWeight: 700 }}>{label}</div>
                    <div style={{ fontSize: 11, color: "var(--t3)" }}>{meta ? (zh ? meta.label : meta.labelEn) : w.type}</div>
                  </React.Fragment>
                )}
              </div>
              <button type="button" onClick={() => { setRenameId(w.id); setRenameVal(w.name); }} style={editMiniBtn}>{zh ? "改名" : "Name"}</button>
              <button type="button" className={"sf-switch" + (w.enabled !== false ? " on" : "")}
                onClick={() => hub.setWidgetEnabled(room.id, w.id, w.enabled === false)} />
              <button type="button" onClick={() => hub.removeWidget(room.id, w.id)} style={{ ...editMiniBtn, color: "var(--alert)" }}>×</button>
            </div>
          );
        })}
        <div className="eyebrow" style={{ margin: "4px 0 8px" }}>{zh ? "添加控件" : "Add"}</div>
        <div style={{ display: "flex", flexWrap: "wrap", gap: 6, paddingBottom: 4 }}>
          {WIDGET_CATALOG.map((c) => (
            <button key={c.type} type="button" onClick={() => hub.addWidget(room.id, c.type)} style={{
              height: 32, padding: "0 10px", borderRadius: 999, border: "1px solid var(--line)",
              background: "var(--bg-card)", color: "var(--t1)", fontWeight: 700, fontSize: 11,
              cursor: "pointer", fontFamily: "inherit",
            }}>+ {zh ? c.label : c.labelEn}</button>
          ))}
        </div>
      </ScrollPane>
      <button type="button" className="basic-cta" style={{ flexShrink: 0, marginTop: 8 }} onClick={() => hub.go("room")}>
        {zh ? "完成" : "Done"}
      </button>
    </HubShell>
  );
}
const editMiniBtn = {
  height: 28, padding: "0 8px", borderRadius: 6, border: "1px solid var(--line)",
  background: "var(--bg-card-2)", color: "var(--t2)", fontWeight: 700, fontSize: 10,
  cursor: "pointer", fontFamily: "inherit",
};

function HubScenesPage() {
  const hub = useHub();
  const zh = hub.settings.lang !== "en";
  const scenes = [
    { id: "home", titleZh: "回家", titleEn: "Home", subZh: "灯 + 空调", subEn: "Lights + AC", hue: "#34d399", icon: <LIco.home width="18" height="18" /> },
    { id: "away", titleZh: "离家", titleEn: "Away", subZh: "全关 + 布防", subEn: "All off + arm", hue: "#f59e0b", icon: <LIco.away width="18" height="18" /> },
    { id: "movie", titleZh: "观影", titleEn: "Movie", subZh: "暗光 + 关帘", subEn: "Dim + close", hue: "#818cf8", icon: <LIco.moon width="18" height="18" /> },
    { id: "sleep", titleZh: "睡眠", titleEn: "Sleep", subZh: "夜灯模式", subEn: "Night light", hue: "#2dd4bf", icon: <LIco.moonSleep width="18" height="18" /> },
    { id: "guest", titleZh: "会客", titleEn: "Guest", subZh: "客厅明亮", subEn: "Bright living", hue: "#eab308", icon: <LIco.bulb width="18" height="18" /> },
    { id: "eco", titleZh: "节能", titleEn: "Eco", subZh: "降低负荷", subEn: "Lower load", hue: "#22d3ee", icon: <LIco.gauge width="18" height="18" /> },
  ];
  return (
    <HubShell title={zh ? "情景" : "Scenes"} onBack={() => hub.go("home")} dots={4} active={2}
      right={<span style={{ fontSize: 11, color: "var(--accent)", fontWeight: 700 }}>
        {(zh ? "当前 · " : "Now · ") + hub.sceneLabel(hub.activeScene)}
      </span>}>
      <ScrollPane asGrid style={{ gridTemplateColumns: "1fr 1fr" }}>
        {scenes.map((s) => (
          <button key={s.id} type="button" onClick={() => hub.applyScene(s.id)} style={{
            border: hub.activeScene === s.id ? `1px solid ${s.hue}` : "1px solid var(--line)",
            background: hub.activeScene === s.id ? window.A(s.hue, 0.16) : "var(--bg-card)",
            borderRadius: "var(--radius)", padding: 12, color: "var(--t1)", cursor: "pointer",
            textAlign: "left", display: "flex", flexDirection: "column", gap: 8, minHeight: 88,
            fontFamily: "inherit",
          }}>
            <div className="badge" style={{ width: 36, height: 36, background: window.A(s.hue, 0.14), color: s.hue }}>{s.icon}</div>
            <div>
              <div style={{ fontSize: 13, fontWeight: 700 }}>{zh ? s.titleZh : s.titleEn}</div>
              <div style={{ fontSize: 11, color: "var(--t3)" }}>{zh ? s.subZh : s.subEn}</div>
            </div>
          </button>
        ))}
      </ScrollPane>
    </HubShell>
  );
}

function HubGatewayPage() {
  const hub = useHub();
  return (
    <HubShell title="总线" onBack={() => hub.go("home")}
      right={<span style={{ fontSize: 11, color: "var(--t3)" }}>{hub.okCount}/{hub.protocols.length}</span>}>
      <ScrollPane asGrid style={{ gridTemplateColumns: "1fr" }}>
        {hub.protocols.map((r) => (
          <button key={r.id} type="button" onClick={() => hub.toggleProto(r.id)} style={{
            display: "flex", alignItems: "center", gap: 10, padding: "10px 12px", textAlign: "left",
            borderRadius: "var(--radius)", background: "var(--bg-card)", border: "1px solid var(--line)",
            color: "var(--t1)", cursor: "pointer", width: "100%", fontFamily: "inherit",
          }}>
            <div className="badge" style={{
              width: 36, height: 36,
              background: r.ok ? "rgba(52,211,153,0.12)" : "rgba(244,63,94,0.12)",
              color: r.ok ? "var(--on)" : "var(--alert)",
            }}>
              {(() => { const I = LIco.forProto(r.id); return <I width="16" height="16" />; })()}
            </div>
            <div style={{ flex: 1, minWidth: 0 }}>
              <div style={{ fontSize: 14, fontWeight: 700 }}>{r.name}</div>
              <div style={{ fontSize: 11, color: "var(--t3)" }}>{r.detail} · 点击切换</div>
            </div>
            <ProtoPill label={r.ok ? "OK" : "Fault"} ok={r.ok} />
          </button>
        ))}
      </ScrollPane>
      <button type="button" onClick={() => hub.go("points")} style={{
        marginTop: 10, height: 44, borderRadius: 8, border: "1px solid var(--line)",
        background: "var(--bg-card)", color: "var(--t1)", fontWeight: 700, cursor: "pointer",
        fontFamily: "inherit", flexShrink: 0,
      }}>查看点表 →</button>
    </HubShell>
  );
}

function HubSecurityPage() {
  const hub = useHub();
  return (
    <HubShell title="安防" onBack={() => hub.go("home")}>
      <div style={{ flex: 1, minHeight: 0 }}>
        <AlarmWidget
          state={hub.armed ? "armed" : "disarmed"}
          mode="Away"
          page
          onOpen={() => {
            if (hub.armed) hub.setKeypad(true);
            else { hub.setArmed(true); hub.flash("已布防"); }
          }}
        />
      </div>
      <div style={{ display: "grid", gridTemplateColumns: "1fr 1fr", gap: 8, marginTop: 10 }}>
        <button type="button" onClick={() => { hub.setArmed(true); hub.flash("布防"); }} style={secBtn}>布防</button>
        <button type="button" onClick={() => hub.setKeypad(true)} style={secBtn}>撤防 PIN</button>
      </div>
    </HubShell>
  );
}
const secBtn = {
  height: 44, borderRadius: 10, border: "1px solid var(--line)", background: "var(--bg-card)",
  color: "var(--t1)", fontWeight: 700, cursor: "pointer", fontFamily: "inherit",
};

function HubToast() {
  const hub = useHub();
  if (!hub.toast) return null;
  return (
    <div style={{
      position: "absolute", left: 24, right: 24, bottom: 28, zIndex: 30,
      background: "var(--bg-elev)", border: "1px solid var(--line-2)", color: "var(--t1)",
      borderRadius: 10, padding: "10px 14px", fontSize: 13, fontWeight: 600,
      boxShadow: "0 8px 24px rgba(0,0,0,.35)", textAlign: "center",
    }}>{hub.toast}</div>
  );
}

function HubRouteView({ HomeView, theme }) {
  const hub = useHub();
  const Home = HomeView || (THEME_META.find((t) => t.id === theme) || {}).Hub || ThemeHubSlate;
  switch (hub.route) {
    case "home": return <Home />;
    case "room": return <HubRoomPage />;
    case "scenes": return <HubScenesPage />;
    case "energy": return <TplEnergy />;
    case "gateway": return <HubGatewayPage />;
    case "points": return <TplPointTable />;
    case "security": return <HubSecurityPage />;
    case "appliances": return <TplApplianceGrid />;
    case "hvac": return <TplHvacFocus />;
    case "minimal": return <TplMinimalHome />;
    case "settings": return <TplSettings />;
    case "network": return <TplNetwork />;
    case "schedule": return <TplSchedule />;
    case "sensor_cal": return <TplSensorCal />;
    case "room-edit": return <HubRoomEditPage />;
    default: return <Home />;
  }
}

/** Full interactive app for one theme (artboards + chassis) */
function ThemeLiveApp({ theme = "slate", showChrome = false, HomeView, onThemeChange }) {
  return (
    <HubProvider>
      <ThemeLiveInner
        theme={theme}
        showChrome={showChrome}
        HomeView={HomeView}
        onThemeChange={onThemeChange}
      />
    </HubProvider>
  );
}

function ThemeLiveInner({ theme, showChrome, HomeView, onThemeChange }) {
  const hub = useHub();
  const pager = ["home", "room", "scenes", "energy"];
  const idx = pager.indexOf(hub.route);
  const swipe = (d) => {
    if (idx < 0) return;
    hub.go(pager[(idx + d + pager.length) % pager.length]);
  };
  // Room page owns swipe (widget sub-pages + exit to home/scenes)
  const gestureOk = idx >= 0 && hub.route !== "room" && !hub.keypad && !hub.wifiJoin;

  const screen = (
    <ThemeScope theme={theme} style={{
      position: "relative", width: 480, height: 480,
      filter: hub.screenFilter,
      transition: "filter 0.25s ease",
    }}>
      <SwipeSurface
        enabled={gestureOk}
        onSwipeLeft={() => swipe(1)}
        onSwipeRight={() => swipe(-1)}
        style={{ height: "100%" }}
      >
        <HubRouteView HomeView={HomeView} theme={theme} />
      </SwipeSurface>
      {hub.keypad && (
        <div style={{ position: "absolute", inset: 0, zIndex: 40 }}>
          <AlarmKeypad onClose={() => {
            hub.setKeypad(false);
            hub.setArmed(false);
            hub.flash(hub.settings.lang === "en" ? "Disarmed" : "已撤防");
          }} />
        </div>
      )}
      {hub.wifiJoin && (
        <div style={{ position: "absolute", inset: 0, zIndex: 45 }}>
          <WifiJoinSheet />
        </div>
      )}
      <HubToast />
    </ThemeScope>
  );

  if (!showChrome) return screen;

  const tabs = [
    ["home", "总览"], ["room", "房间"], ["scenes", "情景"], ["energy", "能耗"],
    ["gateway", "总线"], ["security", "安防"], ["sensor_cal", "传感校准"], ["hvac", "温控"], ["appliances", "开关"],
    ["settings", "设置"], ["network", "网络"], ["schedule", "日程"], ["minimal", "待机"],
  ];

  return (
    <ThemeScope theme={theme} style={{
      display: "flex", gap: 16, alignItems: "stretch", padding: 16, width: "auto", height: "auto",
      borderRadius: 16, background: "var(--panel-chrome)",
    }}>
      <div className="hub-chassis-side">
        <div className="side-title">THEME</div>
        {THEME_META.map((t) => (
          <button
            key={t.id}
            type="button"
            className={"theme-pick" + (theme === t.id ? " on" : "")}
            onClick={() => {
              onThemeChange && onThemeChange(t.id);
              hub.go("home");
            }}
          >
            {t.label}<small>{t.desc}</small>
          </button>
        ))}
      </div>
      <div className="hub-chassis-main">
        <div className="bezel themed" style={{ background: "var(--bezel-bg)", position: "relative" }}>
          {screen}
          {idx >= 0 && !hub.keypad && (
            <React.Fragment>
              <button type="button" onClick={() => swipe(-1)} style={navStyle("left")}><LIco.left width="18" height="18" /></button>
              <button type="button" onClick={() => swipe(1)} style={navStyle("right")}><LIco.right width="18" height="18" /></button>
            </React.Fragment>
          )}
        </div>
        <div className="hub-tabbar">
          {tabs.map(([id, label]) => (
            <button key={id} type="button" className={hub.route === id ? "on" : ""} onClick={() => hub.go(id)}>{label}</button>
          ))}
        </div>
      </div>
    </ThemeScope>
  );
}

function navStyle(dir) {
  return {
    position: "absolute", top: "50%", transform: "translateY(-50%)", [dir]: 26,
    width: 32, height: 52, borderRadius: 8, zIndex: 15,
    background: "rgba(15,18,20,0.65)", border: "1px solid rgba(255,255,255,0.12)",
    color: "#c5cdd8", cursor: "pointer", display: "flex", alignItems: "center", justifyContent: "center",
  };
}

/** Chassis with theme state outside provider so switching keeps device state */
function InteractiveChassis() {
  const [theme, setTheme] = useState("slate");
  return (
    <HubProvider>
      <ThemeLiveInner theme={theme} showChrome onThemeChange={setTheme} />
    </HubProvider>
  );
}

Object.assign(window, {
  HubProvider, ThemeLiveApp, ThemeLiveInner, InteractiveChassis,
  HubRoomPage, HubRoomEditPage, HubScenesPage, HubGatewayPage, HubSecurityPage, HubToast, HubRouteView,
  ROOMS,
});
