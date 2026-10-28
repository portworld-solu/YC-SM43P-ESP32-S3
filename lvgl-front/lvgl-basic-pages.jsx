/* eslint-disable */
// Basic product shells: Settings / Network / Schedule (+ Wi-Fi password join).

function SettingRow({ icon, title, sub, right, onClick }) {
  return (
    <div
      className="sf-row basic-row"
      onClick={onClick}
      role={onClick ? "button" : undefined}
      style={{
        width: "100%", textAlign: "left", color: "var(--t1)", fontFamily: "inherit", gap: 12, marginBottom: 8,
        cursor: onClick ? "pointer" : "default",
      }}
    >
      <div className="badge" style={{
        width: 36, height: 36, background: "color-mix(in srgb, var(--accent) 14%, transparent)", color: "var(--accent)",
      }}>{icon}</div>
      <div style={{ flex: 1, minWidth: 0 }}>
        <div style={{ fontSize: 14, fontWeight: 700 }}>{title}</div>
        {sub && <div style={{ fontSize: 11, color: "var(--t3)", marginTop: 2 }}>{sub}</div>}
      </div>
      {right}
    </div>
  );
}

function TplSettings() {
  const hub = useHub();
  const s = hub.settings;
  const set = (patch) => hub.patchSettings(patch);
  return (
    <HubShell title="设置" onBack={() => hub.go("home")} right={<LIco.cog width="18" height="18" />}>
      <ScrollPane>
        <div className="eyebrow" style={{ marginBottom: 8 }}>显示</div>
        <SettingRow
          icon={<LIco.bright width="18" height="18" />}
          title="屏幕亮度"
          sub={`${s.brightness}%`}
          right={
            <div style={{ width: 120 }} onClick={(e) => e.stopPropagation()}>
              <LinearTrack value={s.brightness} hue="var(--accent)" onChange={(v) => set({ brightness: v })} />
            </div>
          }
        />
        <SettingRow
          icon={<LIco.moon width="18" height="18" />}
          title="夜间模式"
          sub={s.nightMode ? "已开启 · 降亮" : "跟随白天"}
          right={<button type="button" className={"sf-switch" + (s.nightMode ? " on" : "")} onClick={(e) => { e.stopPropagation(); set({ nightMode: !s.nightMode }); }} />}
        />
        <SettingRow
          icon={<LIco.clock width="18" height="18" />}
          title="低功耗待机"
          sub={s.standbyEn
            ? `已开启 · ${({ 15: "15 秒", 30: "30 秒", 60: "1 分钟", 120: "2 分钟", 300: "5 分钟", 600: "10 分钟" })[s.standbyTimeoutSec] || "2 分钟"}`
            : "已关闭"}
          right={<button type="button" className={"sf-switch" + (s.standbyEn ? " on" : "")} onClick={(e) => { e.stopPropagation(); set({ standbyEn: !s.standbyEn }); }} />}
        />
        {s.standbyEn && (
          <>
            <div className="eyebrow" style={{ margin: "4px 0 6px" }}>进入待机时长</div>
            <select
              value={s.standbyTimeoutSec}
              onChange={(e) => set({ standbyTimeoutSec: Number(e.target.value) })}
              style={{
                width: "100%", height: 40, marginBottom: 8, fontFamily: "inherit", fontSize: 14, fontWeight: 600,
                borderRadius: "var(--radius-sm)", border: "1px solid var(--line)", background: "var(--bg-card)", color: "var(--t1)",
                padding: "0 10px",
              }}
            >
              <option value={15}>15 秒</option>
              <option value={30}>30 秒</option>
              <option value={60}>1 分钟</option>
              <option value={120}>2 分钟</option>
              <option value={300}>5 分钟</option>
              <option value={600}>10 分钟</option>
            </select>
            <SettingRow
              icon={<LIco.home width="18" height="18" />}
              title="预览待机界面"
              sub="大时钟 · 快捷情景"
              right={<LIco.chevron width="16" height="16" style={{ color: "var(--t3)" }} />}
              onClick={() => hub.go("minimal")}
            />
          </>
        )}
        <div className="eyebrow" style={{ margin: "12px 0 8px" }}>系统</div>
        <SettingRow
          icon={<LIco.lang width="18" height="18" />}
          title={s.lang === "zh" ? "界面语言" : "Language"}
          sub={s.lang === "zh" ? "简体中文" : "English"}
          right={<span style={{ fontSize: 12, color: "var(--accent)", fontWeight: 700 }}>{s.lang === "zh" ? "ZH" : "EN"}</span>}
          onClick={() => {
            set({ lang: s.lang === "zh" ? "en" : "zh" });
            hub.flash(s.lang === "zh" ? "Language: EN" : "语言：中文");
          }}
        />
        <SettingRow
          icon={<LIco.drop width="18" height="18" />}
          title={s.lang === "zh" ? "传感校准" : "Sensor calibration"}
          sub={s.lang === "zh" ? "温度 / 湿度补偿" : "Temp / RH offset"}
          right={<LIco.chevron width="16" height="16" style={{ color: "var(--t3)" }} />}
          onClick={() => hub.go("sensor_cal")}
        />
        <SettingRow
          icon={<LIco.wifi width="18" height="18" />}
          title={s.lang === "zh" ? "网络与配网" : "Network"}
          sub={hub.network.wifiOn ? hub.network.ssid : (s.lang === "zh" ? "WiFi 关闭" : "WiFi off")}
          right={<LIco.chevron width="16" height="16" style={{ color: "var(--t3)" }} />}
          onClick={() => hub.go("network")}
        />
        <SettingRow
          icon={<LIco.clock width="18" height="18" />}
          title="定时与日程"
          sub={`${hub.schedules.filter((x) => x.on).length} 条启用`}
          right={<LIco.chevron width="16" height="16" style={{ color: "var(--t3)" }} />}
          onClick={() => hub.go("schedule")}
        />
        <SettingRow
          icon={<LIco.home width="18" height="18" />}
          title="房间控件"
          sub="自定义家居控件布局"
          right={<LIco.chevron width="16" height="16" style={{ color: "var(--t3)" }} />}
          onClick={() => hub.go("room-edit")}
        />
        <SettingRow
          icon={<LIco.bus width="18" height="18" />}
          title="总线与点表"
          sub={`${hub.okCount}/${hub.protocols.length} 正常`}
          right={<LIco.chevron width="16" height="16" style={{ color: "var(--t3)" }} />}
          onClick={() => hub.go("gateway")}
        />
        <SettingRow
          icon={<LIco.shield width="18" height="18" />}
          title="触控音"
          sub={s.clickSound ? "开启" : "静音"}
          right={<button type="button" className={"sf-switch" + (s.clickSound ? " on" : "")} onClick={(e) => { e.stopPropagation(); set({ clickSound: !s.clickSound }); }} />}
        />
      </ScrollPane>
      <div style={{ fontSize: 11, color: "var(--t4)", marginTop: 6, flexShrink: 0 }}>Hub UI Kit · Demo · v0.3</div>
    </HubShell>
  );
}

/** Soft keyboard for Wi-Fi password (480×480 overlay) — input stays above keys */
function WifiJoinSheet() {
  const hub = useHub();
  const { useState } = React;
  const j = hub.wifiJoin;
  const [pwd, setPwd] = useState("");
  const [show, setShow] = useState(false);
  const [kbOpen, setKbOpen] = useState(true);
  if (!j) return null;
  const connecting = j.status === "connecting";
  const keys = [
    "1234567890",
    "qwertyuiop",
    "asdfghjkl",
    "zxcvbnm",
  ];
  const press = (ch) => {
    if (connecting) return;
    if (ch === "del") setPwd((p) => p.slice(0, -1));
    else if (ch === "space") setPwd((p) => p + " ");
    else setPwd((p) => (p.length < 32 ? p + ch : p));
  };

  return (
    <div className="lv lv-panel" style={{ padding: "12px 16px 0", background: "var(--bg-deep)", display: "flex", flexDirection: "column" }}>
      <div style={{ display: "flex", justifyContent: "space-between", alignItems: "center", flexShrink: 0 }}>
        <button type="button" className="sf-back" onClick={hub.cancelWifiJoin} disabled={connecting}>×</button>
        <div className="eyebrow">Wi-Fi</div>
        <button type="button" disabled={connecting} onClick={() => setKbOpen(false)} style={{
          width: 36, height: 28, border: "1px solid var(--line)", borderRadius: 4, background: "var(--bg-card)",
          color: "var(--accent)", fontWeight: 700, cursor: "pointer",
        }}>√</button>
      </div>
      <div style={{ textAlign: "center", marginTop: 6, flexShrink: 0 }}>
        <div style={{ fontWeight: 700, fontSize: 16 }}>{j.ssid}</div>
        <div style={{ fontSize: 11, color: "var(--t3)", marginTop: 4 }}>
          {connecting ? "正在连接…" : (j.error || "输入密码 · 演示可用 12345678")}
        </div>
      </div>
      <div style={{
        margin: "12px 0 8px", minHeight: 44, borderRadius: "var(--radius)", border: "1px solid var(--line)",
        background: "var(--bg-card)", display: "flex", alignItems: "center", padding: "0 12px", gap: 8, flexShrink: 0,
      }} onClick={() => !connecting && setKbOpen(true)}>
        <LIco.lock width="16" height="16" style={{ color: "var(--t3)" }} />
        <div style={{ flex: 1, fontFamily: "var(--font-m)", fontSize: 16, letterSpacing: show ? 0 : 2 }}>
          {pwd ? (show ? pwd : "•".repeat(pwd.length)) : <span style={{ color: "var(--t4)" }}>Password</span>}
        </div>
        <button type="button" onClick={(e) => { e.stopPropagation(); setShow((s) => !s); }} style={{
          border: "none", background: "transparent", color: "var(--accent)", fontWeight: 700, fontSize: 11, cursor: "pointer",
        }}>{show ? "隐藏" : "显示"}</button>
      </div>
      <button type="button" disabled={connecting || pwd.length < 8} onClick={() => hub.submitWifiPassword(pwd)} style={{
        ...wifiKey, flexShrink: 0, height: 40, marginBottom: 8,
        background: connecting ? "var(--bg-card-2)" : "var(--accent)",
        color: connecting ? "var(--t4)" : "var(--ink-on-accent)",
        border: "none",
      }}>{connecting ? "连接中…" : "连接"}</button>
      <div style={{ flex: 1, minHeight: 8 }} />
      {kbOpen && (
        <div style={{ display: "grid", gap: 4, flexShrink: 0, paddingBottom: 10, background: "var(--bg-deep)" }}>
          {keys.map((row, ri) => (
            <div key={ri} style={{ display: "flex", gap: 4, justifyContent: "center" }}>
              {row.split("").map((k) => (
                <button key={k} type="button" disabled={connecting} onClick={() => press(k)} style={wifiKey}>{k}</button>
              ))}
            </div>
          ))}
          <div style={{ display: "flex", gap: 4, marginTop: 2 }}>
            <button type="button" disabled={connecting} onClick={() => press("space")} style={{ ...wifiKey, flex: 2 }}>空格</button>
            <button type="button" disabled={connecting} onClick={() => press("del")} style={{ ...wifiKey, flex: 1 }}>⌫</button>
            <button type="button" disabled={connecting} onClick={() => setKbOpen(false)} style={{ ...wifiKey, flex: 2, color: "var(--accent)" }}>√ 完成</button>
          </div>
        </div>
      )}
    </div>
  );
}
const wifiKey = {
  minWidth: 28, height: 36, flex: 1, borderRadius: 6, border: "1px solid var(--line)",
  background: "var(--bg-card)", color: "var(--t1)", fontSize: 13, fontWeight: 600,
  cursor: "pointer", fontFamily: "inherit",
};

function TplNetwork() {
  const hub = useHub();
  const { useState } = React;
  const n = hub.network;
  const aps = [
    { ssid: "Office-AP", rssi: -58, secure: true },
    { ssid: "Guest-5G", rssi: -72, secure: true },
    { ssid: "Cafe-Free", rssi: -68, secure: false },
    { ssid: "Lab-IoT", rssi: -81, secure: true },
    { ssid: "Home-Mesh", rssi: -64, secure: true },
    { ssid: "Printer", rssi: -88, secure: false },
  ];
  const [sel, setSel] = useState(0);
  const selected = aps[sel] || aps[0];
  return (
    <HubShell title="网络" onBack={() => hub.go("settings")}
      right={<span style={{ fontSize: 11, color: n.wifiOn ? "var(--on)" : "var(--t3)", fontWeight: 700 }}>{n.wifiOn ? "STA" : "OFF"}</span>}>
      <SettingRow
        icon={n.wifiOn ? <LIco.wifi width="18" height="18" /> : <LIco.wifiOff width="18" height="18" />}
        title="Wi-Fi"
        sub={n.wifiOn ? `${n.ssid} · ${n.ip}` : "已关闭"}
        right={<button type="button" className={"sf-switch" + (n.wifiOn ? " on" : "")} onClick={(e) => {
          e.stopPropagation();
          hub.patchNetwork({ wifiOn: !n.wifiOn });
          hub.flash(n.wifiOn ? "Wi-Fi 已关闭" : "Wi-Fi 已开启");
        }} />}
      />
      <SettingRow
        icon={<LIco.link width="18" height="18" />}
        title="以太网"
        sub={n.ethOn ? (n.ethIp ? `已连接 · ${n.ethIp}` : "已连接 · 100 Mbps") : "未插入"}
        right={<button type="button" className={"sf-switch" + (n.ethOn ? " on" : "")} onClick={(e) => {
          e.stopPropagation();
          hub.patchNetwork({ ethOn: !n.ethOn });
        }} />}
      />
      <div className="eyebrow" style={{ margin: "10px 0 8px", flexShrink: 0 }}>附近热点 · 下拉选择后输入密码</div>
      <div style={{ display: "flex", gap: 8, marginBottom: 8, flexShrink: 0, opacity: n.wifiOn ? 1 : 0.45 }}>
        <button type="button" disabled={!n.wifiOn} onClick={() => hub.flash("扫描完成")} style={{
          flex: 1, height: 40, borderRadius: "var(--radius-sm)", border: "none", background: "var(--accent)",
          color: "var(--ink-on-accent)", fontWeight: 700, fontFamily: "inherit", cursor: n.wifiOn ? "pointer" : "default",
        }}>扫描</button>
        <button type="button" disabled={!n.wifiOn} onClick={() => {
          hub.patchNetwork({ wifiOn: true, ssid: "—", ip: "—" });
          hub.flash("已断开 Wi-Fi");
        }} style={{
          flex: 1, height: 40, borderRadius: "var(--radius-sm)", border: "1px solid var(--line)", background: "var(--bg-card)",
          color: "var(--t1)", fontWeight: 700, fontFamily: "inherit", cursor: n.wifiOn ? "pointer" : "default",
        }}>断开</button>
      </div>
      <label style={{ fontSize: 11, color: "var(--t3)", marginBottom: 4, flexShrink: 0 }}>选择热点</label>
      <select
        disabled={!n.wifiOn}
        value={sel}
        onChange={(e) => setSel(Number(e.target.value))}
        style={{
          width: "100%", height: 40, marginBottom: 10, flexShrink: 0, fontFamily: "inherit", fontSize: 14, fontWeight: 600,
          borderRadius: "var(--radius-sm)", border: "1px solid var(--line)", background: "var(--bg-card)", color: "var(--t1)",
          padding: "0 10px", opacity: n.wifiOn ? 1 : 0.45,
        }}
      >
        {aps.map((ap, i) => (
          <option key={ap.ssid} value={i}>
            {ap.ssid}{ap.secure ? " 🔒" : ""} · {ap.rssi} dBm
            {n.wifiOn && n.ssid === ap.ssid ? " · 已连接" : ""}
          </option>
        ))}
      </select>
      <button type="button" className="basic-cta" disabled={!n.wifiOn} style={{ flexShrink: 0, opacity: n.wifiOn ? 1 : 0.45 }} onClick={() => {
        if (!n.wifiOn) return;
        if (selected.secure) hub.beginWifiJoin(selected);
        else hub.beginWifiJoin({ ...selected, open: true });
      }}>
        {selected.secure ? "输入密码并连接" : "连接开放网络"}
      </button>
      <div style={{ fontSize: 11, color: "var(--t4)", margin: "8px 0 6px", flexShrink: 0 }}>演示：密码 12345678 成功 · 00000000 失败</div>
      <button type="button" className="basic-cta" style={{ flexShrink: 0 }} onClick={() => hub.go("gateway")}>协议总线状态 →</button>
    </HubShell>
  );
}

function TplSchedule() {
  const hub = useHub();
  return (
    <HubShell title="日程" onBack={() => hub.go("settings")}
      right={<span style={{ fontSize: 11, color: "var(--t3)" }}>{hub.schedules.filter((x) => x.on).length}/{hub.schedules.length}</span>}>
      <div className="eyebrow" style={{ marginBottom: 8, flexShrink: 0 }}>开关=启用 · 点「执行」立即跑情景</div>
      <ScrollPane style={{ display: "flex", flexDirection: "column", gap: 8 }}>
        {hub.schedules.map((item) => (
          <div key={item.id} className="sf-row basic-row schedule-card" style={{
            width: "100%", textAlign: "left", color: "var(--t1)", fontFamily: "inherit",
            alignItems: "center", padding: "12px 14px", cursor: "default",
            borderLeft: item.on ? "3px solid var(--accent)" : "3px solid transparent",
          }}>
            <div style={{ flex: 1, minWidth: 0 }}>
              <div style={{ display: "flex", alignItems: "baseline", gap: 10 }}>
                <span style={{ fontSize: 28, fontWeight: 300, fontFamily: "var(--font-d)", lineHeight: 1 }}>{item.time}</span>
                <span style={{ fontSize: 12, color: item.on ? "var(--on)" : "var(--t4)", fontWeight: 700 }}>{item.on ? "ON" : "OFF"}</span>
              </div>
              <div style={{ fontSize: 14, fontWeight: 700, marginTop: 6 }}>{item.title}</div>
              <div style={{ fontSize: 11, color: "var(--t3)", marginTop: 2 }}>{item.days} · {item.action}</div>
            </div>
            <button type="button" disabled={!item.on} onClick={() => hub.runSchedule(item.id)} style={{
              height: 32, padding: "0 10px", marginRight: 8, borderRadius: "var(--radius-sm)",
              border: "1px solid var(--line)", background: item.on ? "var(--accent)" : "var(--bg-card-2)",
              color: item.on ? "var(--ink-on-accent)" : "var(--t4)", fontWeight: 700, fontSize: 11,
              cursor: item.on ? "pointer" : "default", fontFamily: "inherit", opacity: item.on ? 1 : 0.5,
            }}>执行</button>
            <button type="button" className={"sf-switch" + (item.on ? " on" : "")} onClick={() => hub.toggleSchedule(item.id)} />
          </div>
        ))}
      </ScrollPane>
      <button type="button" className="basic-cta" style={{ flexShrink: 0, marginTop: 8 }} onClick={() => hub.go("scenes")}>前往情景中心 →</button>
    </HubShell>
  );
}

/** Sensor calibration — parity with HUB_ROUTE_SENSOR_CAL */
function TplSensorCal() {
  const hub = useHub();
  const zh = hub.settings.lang !== "en";
  const cal = hub.sensorCal || { tempOffset: 0, rhOffset: 0 };
  const setCal = (patch) => hub.setSensorCal((prev) => ({ ...prev, ...patch }));
  const rawT = hub.metrics.indoor;
  const rawRh = hub.metrics.rh;
  const showT = (Number(rawT) + Number(cal.tempOffset)).toFixed(1);
  const showRh = Math.round(Number(rawRh) + Number(cal.rhOffset));
  const step = (key, delta, min, max) => {
    const next = Math.max(min, Math.min(max, Math.round((cal[key] + delta) * 10) / 10));
    setCal({ [key]: next });
  };
  const card = {
    background: "var(--bg-card)", border: "1px solid var(--line)", borderRadius: "var(--radius)",
    padding: 14, marginBottom: 10,
  };
  return (
    <HubShell
      title={zh ? "传感校准" : "Sensor calibration"}
      onBack={() => hub.go("settings")}
      right={<LIco.drop width="18" height="18" />}
    >
      <ScrollPane>
        <div style={{ fontSize: 12, color: "var(--t3)", marginBottom: 10 }}>
          {zh ? "补偿后用于首页与房间示值（演示）" : "Offsets apply to home climate display (demo)"}
        </div>
        <div style={card}>
          <div style={{ display: "flex", alignItems: "center", gap: 10, marginBottom: 10 }}>
            <div className="badge" style={{ width: 32, height: 32, background: "color-mix(in srgb, var(--accent) 14%, transparent)", color: "var(--accent)" }}>
              <LIco.gauge width="16" height="16" />
            </div>
            <div>
              <div style={{ fontWeight: 700 }}>{zh ? "温度补偿" : "Temp offset"}</div>
              <div style={{ fontSize: 11, color: "var(--t3)" }}>{zh ? `示值 ${showT}°` : `Shown ${showT}°`}</div>
            </div>
          </div>
          <div style={{ display: "flex", alignItems: "center", justifyContent: "space-between", gap: 8 }}>
            <button type="button" className="ctrl-btn" style={{ width: 40, height: 40 }} onClick={() => step("tempOffset", -0.1, -5, 5)}>
              <LIco.minus width="16" height="16" />
            </button>
            <div style={{ fontSize: 22, fontWeight: 600, fontFamily: "var(--font-d)" }}>
              {cal.tempOffset > 0 ? "+" : ""}{cal.tempOffset.toFixed(1)}°
            </div>
            <button type="button" className="ctrl-btn" style={{ width: 40, height: 40 }} onClick={() => step("tempOffset", 0.1, -5, 5)}>
              <LIco.plus width="16" height="16" />
            </button>
          </div>
        </div>
        <div style={card}>
          <div style={{ display: "flex", alignItems: "center", gap: 10, marginBottom: 10 }}>
            <div className="badge" style={{ width: 32, height: 32, background: "color-mix(in srgb, var(--accent) 14%, transparent)", color: "var(--accent)" }}>
              <LIco.drop width="16" height="16" />
            </div>
            <div>
              <div style={{ fontWeight: 700 }}>{zh ? "湿度补偿" : "RH offset"}</div>
              <div style={{ fontSize: 11, color: "var(--t3)" }}>{zh ? `示值 ${showRh}%` : `Shown ${showRh}%`}</div>
            </div>
          </div>
          <div style={{ display: "flex", alignItems: "center", justifyContent: "space-between", gap: 8 }}>
            <button type="button" className="ctrl-btn" style={{ width: 40, height: 40 }} onClick={() => step("rhOffset", -1, -20, 20)}>
              <LIco.minus width="16" height="16" />
            </button>
            <div style={{ fontSize: 22, fontWeight: 600, fontFamily: "var(--font-d)" }}>
              {cal.rhOffset > 0 ? "+" : ""}{cal.rhOffset}%
            </div>
            <button type="button" className="ctrl-btn" style={{ width: 40, height: 40 }} onClick={() => step("rhOffset", 1, -20, 20)}>
              <LIco.plus width="16" height="16" />
            </button>
          </div>
        </div>
      </ScrollPane>
      <button type="button" className="basic-cta" style={{ flexShrink: 0 }} onClick={() => hub.go("home")}>
        {zh ? "返回首页" : "Back to home"}
      </button>
    </HubShell>
  );
}

Object.assign(window, { TplSettings, TplNetwork, TplSchedule, TplSensorCal, SettingRow, WifiJoinSheet });
