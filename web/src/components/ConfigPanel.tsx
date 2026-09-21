"use client";

import { useCallback, useEffect, useRef, useState } from "react";
import { Cmd, Status, TABLE, TabletSettings, defaults, clamp, statusText, type Values } from "../lib/settings";
import { groupsWithLeftovers } from "../lib/ui-meta";
import { SettingRow } from "./SettingRow";
import { AreaEditor } from "./AreaEditor";

const RECOMMENDED_URL = "s620.json";
const PERSONAL = new Set(["AREA_X0", "AREA_X1", "AREA_Y0", "AREA_Y1", "FLIP_X", "FLIP_Y"]);

type Props = { supported: boolean; onNeedFlash: () => void };

export function ConfigPanel({ supported, onNeedFlash }: Props) {
  const [tablet, setTablet] = useState<TabletSettings | null>(null);
  const [device, setDevice] = useState<Values | null>(null);     // what the tablet is running
  const [draft, setDraft] = useState<Values>(defaults);          // what the sliders show
  const [status, setStatus] = useState(0);
  const [live, setLive] = useState(true);
  const [busy, setBusy] = useState(false);
  const [error, setError] = useState<string | null>(null);
  const [note, setNote] = useState<string | null>(null);
  const timer = useRef<ReturnType<typeof setTimeout>>(undefined);

  const edits = useRef(0);   // counts changes made on the page

  // Shows what the tablet reports. The sliders only follow it if nothing was changed on the page since the request started,
  // otherwise a slow answer would put an old value back under the user's hand.
  const adopt = useCallback((snap: { values: Values; status: number }, seq = edits.current) => {
    setDevice(snap.values);
    setStatus(snap.status);
    if (seq === edits.current) setDraft(snap.values);
  }, []);

  const run = useCallback(async (fn: () => Promise<void>) => {
    setBusy(true);
    setError(null);
    try {
      await fn();
    } catch (e) {
      setError(e instanceof Error ? e.message : String(e));
    } finally {
      setBusy(false);
    }
  }, []);

  const attach = useCallback(
    (t: TabletSettings) =>
      run(async () => {
        setTablet(t);
        adopt(await t.read());
      }),
    [run, adopt],
  );

  // reconnect to a tablet the user allowed before, and notice unplugging
  useEffect(() => {
    if (!supported || !("hid" in navigator)) return;
    TabletSettings.previouslyAllowed().then((t) => t && attach(t)).catch(() => {});
    const onDisconnect = (e: HIDConnectionEvent) => {
      setTablet((cur) => (cur && cur.device === e.device ? null : cur));
      setNote("The tablet was unplugged.");
    };
    navigator.hid.addEventListener("disconnect", onDisconnect);
    return () => navigator.hid.removeEventListener("disconnect", onDisconnect);
  }, [supported, attach]);

  const connect = () =>
    run(async () => {
      const t = await TabletSettings.request();
      setNote(null);
      setTablet(t);
      adopt(await t.read());
    });

  const change = (patch: Values) => {
    const next = { ...draft };
    for (const [k, v] of Object.entries(patch)) next[k] = clamp(k, v);
    setDraft(next);
    edits.current++;
    if (live && tablet) {
      clearTimeout(timer.current);
      const seq = edits.current;
      timer.current = setTimeout(() => {
        run(async () => adopt(await tablet.send(Cmd.apply, next), seq));
      }, 150);
    }
  };

  const send = (cmd: number, text: string, values: Values = draft) =>
    run(async () => {
      if (!tablet) return;
      clearTimeout(timer.current);
      const seq = edits.current;
      adopt(await tablet.send(cmd, values), seq);
      setNote(text);
    });

  const unsent = device !== null && TABLE.some((s) => device[s.name] !== draft[s.name]);
  const off = !tablet || busy;

  const exportJson = () => {
    const blob = new Blob([JSON.stringify({ firmware: "s620-cfw", values: draft }, null, 2)], { type: "application/json" });
    const a = document.createElement("a");
    a.href = URL.createObjectURL(blob);
    a.download = "s620-settings.json";
    a.click();
    URL.revokeObjectURL(a.href);
  };

  // the values of a settings export, without the ones in `skip`
  const valuesFrom = (data: { values?: Record<string, unknown> }, skip: Set<string> = new Set()): Values => {
    const patch: Values = {};
    for (const s of TABLE) {
      const v = data.values?.[s.name];
      if (typeof v === "number" && !skip.has(s.name)) patch[s.name] = v;
    }
    return patch;
  };

  const importJson = async (file: File) => {
    try {
      const patch = valuesFrom(JSON.parse(await file.text()));
      change(patch);
      setNote(`Loaded ${Object.keys(patch).length} values from ${file.name}.`);
    } catch {
      setError("That file is not a settings export.");
    }
  };

  // The recommended settings are meant for everybody, so they leave out what depends on the person: the active area and the axis mirroring.
  const loadRecommended = () =>
    run(async () => {
      const r = await fetch(RECOMMENDED_URL, { cache: "no-store" });
      if (!r.ok) throw new Error("Could not download the recommended settings.");
      const patch = valuesFrom(await r.json(), PERSONAL);
      if (Object.keys(patch).length === 0) throw new Error("The recommended settings file is empty or not a settings export.");
      change(patch);
      setNote(`Loaded ${Object.keys(patch).length} recommended values. Click "Save to tablet" to keep them.`);
    });

  if (!supported) {
    return <section className="card"><p>Configuring needs WebHID, which this browser does not have.</p></section>;
  }

  return (
    <>
      <section className="card connect">
        <div>
          <h2>{tablet ? "Tablet connected" : "Connect your tablet"}</h2>
          <p className="muted small">
            {tablet
              ? statusText(status)
              : "Plug the tablet in (normal mode, with the custom firmware flashed), click Connect and pick “Gaomon Tablet” in the list. Close OpenTabletDriver if the tablet does not show up."}
          </p>
        </div>
        <div className="btns">
          {tablet ? (
            <button className="ghost" onClick={() => tablet.close().then(() => setTablet(null))}>Disconnect</button>
          ) : (
            <button onClick={connect} disabled={busy}>Connect</button>
          )}
        </div>
      </section>

      <section className="card connect">
        <div>
          <h2>Recommended settings</h2>
          <p className="muted small">
            For better stability, load the recommended settings, then click “Save to tablet”. They keep your active area and axis mirroring.
            An update with a calibration tool is coming shortly.
          </p>
        </div>
        <div className="btns">
          <button onClick={loadRecommended} disabled={off}>Load recommended settings</button>
        </div>
      </section>

      {error && (
        <div className="banner err">
          {error} {error.includes("firmware") && <button className="link" onClick={onNeedFlash}>Go to Flash</button>}
        </div>
      )}
      {note && !error && <div className="banner ok">{note}</div>}

      <div className="toolbar">
        <label className="switch">
          <input type="checkbox" checked={live} onChange={(e) => setLive(e.target.checked)} />
          <span>Apply changes live</span>
        </label>
        <div className="btns">
          <button className="ghost" disabled={off || live || !unsent} onClick={() => send(Cmd.apply, "Applied to the running tablet (not saved).")}>Apply</button>
          <button
            disabled={off}
            onClick={() => send(Cmd.applySave, "Saved to the tablet's flash. It keeps these settings after unplugging.")}
          >
            Save to tablet
          </button>
          <button className="ghost" disabled={off} onClick={() => send(Cmd.reload, "Loaded the saved settings.")}>Load saved</button>
          <button className="ghost" disabled={off} onClick={() => send(Cmd.defaults, "Defaults applied (not saved yet).")}>Defaults</button>
          <button
            className="ghost danger"
            disabled={off}
            onClick={() => window.confirm("Erase the saved settings and go back to the defaults?") && send(Cmd.factory, "Saved settings erased, defaults active.")}
          >
            Erase saved
          </button>
        </div>
        <div className="btns">
          <button className="ghost" onClick={exportJson}>Export</button>
          <label className="button ghost">
            Import
            <input type="file" accept="application/json" hidden onChange={(e) => e.target.files?.[0] && importJson(e.target.files[0])} />
          </label>
        </div>
      </div>
      {tablet && (unsent || (status & Status.dirty) !== 0) && (
        <p className="muted small pad">
          {unsent ? "Changes not sent to the tablet yet. " : ""}
          {(status & Status.dirty) !== 0 ? "The tablet is running settings that are not saved: they are lost when it is unplugged." : ""}
        </p>
      )}

      <AreaEditor values={draft} onChange={change} disabled={!tablet} />

      {groupsWithLeftovers().map((g) => (
        <details key={g.id} className="card group" open={!g.advanced}>
          <summary>
            <h2>{g.title}</h2>
            {g.advanced && <span className="tag">advanced</span>}
          </summary>
          {g.note && <p className="muted small">{g.note}</p>}
          {g.names.map((n) => (
            <SettingRow key={n} name={n} value={draft[n]} disabled={!tablet} onChange={(v) => change({ [n]: v })} />
          ))}
        </details>
      ))}
    </>
  );
}
