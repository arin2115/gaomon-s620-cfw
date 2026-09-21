"use client";

import { useEffect, useState } from "react";
import { ConfigPanel } from "../components/ConfigPanel";
import { FlashPanel } from "../components/FlashPanel";

type Tab = "configure" | "flash";

export default function Home() {
  const [tab, setTab] = useState<Tab>("configure");
  const [support, setSupport] = useState<{ hid: boolean; usb: boolean } | null>(null);   // unknown until the browser is checked
  const [flashes, setFlashes] = useState<number | null>(null);

  useEffect(() => {
    setSupport({ hid: "hid" in navigator, usb: "usb" in navigator });
    fetch("api/flashes").then((r) => r.json()).then((j) => setFlashes(j.count)).catch(() => {});   // no counter server: just hide it
  }, []);

  return (
    <main className="shell">
      <header className="top">
        <div>
          <h1>Gaomon S620 Custom Firmware</h1>
          <p className="muted">
            Flash and configure the Gaomon S620 from the browser.
            {flashes !== null && <span className="counter"> · {flashes.toLocaleString()} successful {flashes === 1 ? "flash" : "flashes"}</span>}
          </p>
        </div>
        <nav className="tabs" role="tablist">
          <button role="tab" aria-selected={tab === "configure"} onClick={() => setTab("configure")}>Configure</button>
          <button role="tab" aria-selected={tab === "flash"} onClick={() => setTab("flash")}>Flash</button>
        </nav>
      </header>

      {support && (!support.hid || !support.usb) && (
        <div className="banner warn">
          This page needs a Chromium-based browser (Chrome, Edge, Brave, Opera)
          {!support.hid && !support.usb ? " for WebHID and WebUSB." : !support.hid ? " for WebHID." : " for WebUSB."}
        </div>
      )}

      {support && (tab === "configure"
        ? <ConfigPanel supported={support.hid} onNeedFlash={() => setTab("flash")} />
        : <FlashPanel supported={support.usb} onFlashed={setFlashes} />)}
    </main>
  );
}
