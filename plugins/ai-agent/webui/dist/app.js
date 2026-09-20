/* openbus AI workbench client — talks to stdlib bridge (/api/*).
   Prebuilt static UI (no npm). Replace with assistant-ui Vite build later. */

(function () {
  "use strict";

  const $ = (id) => document.getElementById(id);
  const messagesEl = $("messages");
  const inputEl = $("input");
  const btnSend = $("btnSend");
  const attBar = $("attBar");
  const snapHint = $("snapHint");
  const healthLine = $("healthLine");
  const runtimeBadge = $("runtimeBadge");
  const settingsForm = $("settingsForm");
  const settingsMsg = $("settingsMsg");

  let busy = false;

  function setPanel(name) {
    document.querySelectorAll(".nav-item").forEach((b) => {
      b.classList.toggle("active", b.dataset.panel === name);
    });
    $("panel-chat").classList.toggle("hidden", name !== "chat");
    $("panel-settings").classList.toggle("hidden", name !== "settings");
  }

  document.querySelectorAll(".nav-item").forEach((btn) => {
    btn.addEventListener("click", () => setPanel(btn.dataset.panel));
  });

  function addMsg(role, text, extraClass) {
    const div = document.createElement("div");
    div.className = "msg " + role + (extraClass ? " " + extraClass : "");
    const label = document.createElement("span");
    label.className = "role";
    label.textContent = role;
    div.appendChild(label);
    div.appendChild(document.createTextNode(text || ""));
    messagesEl.appendChild(div);
    messagesEl.scrollTop = messagesEl.scrollHeight;
    return div;
  }

  async function api(path, opts) {
    const res = await fetch(path, opts);
    if (!res.ok) {
      const t = await res.text();
      throw new Error(t || res.statusText);
    }
    const ct = res.headers.get("content-type") || "";
    if (ct.includes("application/json")) return res.json();
    return res;
  }

  async function refreshHealth() {
    try {
      const h = await api("/api/health");
      healthLine.textContent = "Session " + (h.session || "ok");
    } catch (e) {
      healthLine.textContent = "Bridge offline";
    }
  }

  async function loadSettings() {
    try {
      const s = await api("/api/settings");
      settingsForm.provider.value = s.provider || "";
      settingsForm.base_url.value = s.base_url || "";
      settingsForm.model.value = s.model || "";
      settingsForm.api_key.value = s.api_key || "";
      settingsForm.role.value = s.role || "Analyst";
      settingsForm.policy.value = s.policy || "readonly";
      settingsForm.inject_activity_snapshot.checked =
        s.inject_activity_snapshot !== false;
      runtimeBadge.textContent = s.agents_sdk
        ? "Agents SDK"
        : "Orchestrator";
    } catch (e) {
      settingsMsg.textContent = String(e.message || e);
    }
  }

  async function saveSettings(ev) {
    if (ev) ev.preventDefault();
    const body = {
      provider: settingsForm.provider.value.trim(),
      base_url: settingsForm.base_url.value.trim(),
      model: settingsForm.model.value.trim(),
      api_key: settingsForm.api_key.value,
      role: settingsForm.role.value.trim() || "Analyst",
      policy: settingsForm.policy.value,
      inject_activity_snapshot: settingsForm.inject_activity_snapshot.checked,
    };
    try {
      await api("/api/settings", {
        method: "PUT",
        headers: { "Content-Type": "application/json" },
        body: JSON.stringify(body),
      });
      settingsMsg.textContent = "Saved.";
      await loadSettings();
    } catch (e) {
      settingsMsg.textContent = "Save failed: " + (e.message || e);
    }
  }

  async function refreshAttachments() {
    try {
      const data = await api("/api/attachments");
      const items = data.items || [];
      attBar.innerHTML = "";
      if (!items.length) {
        attBar.classList.add("hidden");
        return;
      }
      attBar.classList.remove("hidden");
      items.forEach((it) => {
        const chip = document.createElement("span");
        chip.className = "chip";
        chip.title = it.preview || it.uri || "";
        chip.textContent = (it.kind || "att") + ": " + (it.title || it.id);
        const x = document.createElement("button");
        x.type = "button";
        x.textContent = "×";
        x.addEventListener("click", async () => {
          await api("/api/attachments/" + encodeURIComponent(it.id), {
            method: "DELETE",
          });
          refreshAttachments();
        });
        chip.appendChild(x);
        attBar.appendChild(chip);
      });
    } catch (_) {
      attBar.classList.add("hidden");
    }
  }

  async function refreshSnapshot() {
    try {
      const data = await api("/api/snapshot");
      const t = (data.text || "").replace(/\s+/g, " ").trim();
      snapHint.textContent = t
        ? t.slice(0, 120) + (t.length > 120 ? "…" : "")
        : "No activity snapshot";
    } catch (_) {
      snapHint.textContent = "";
    }
  }

  function parseAiSdkStream(buffer, onEvent) {
    const lines = buffer.split("\n");
    let rest = "";
    for (let i = 0; i < lines.length; i++) {
      const line = lines[i];
      if (i === lines.length - 1 && !buffer.endsWith("\n")) {
        rest = line;
        break;
      }
      if (!line || line === "data: [DONE]") continue;
      const idx = line.indexOf(":");
      if (idx < 0) continue;
      const prefix = line.slice(0, idx);
      const payload = line.slice(idx + 1);
      try {
        onEvent(prefix, JSON.parse(payload));
      } catch (_) {
        onEvent(prefix, payload);
      }
    }
    return rest;
  }

  async function sendMessage() {
    const text = (inputEl.value || "").trim();
    if (!text || busy) return;
    busy = true;
    btnSend.disabled = true;
    inputEl.value = "";
    addMsg("user", text);
    const assistantEl = addMsg("assistant", "");
    let bodyText = "";

    try {
      const res = await fetch("/api/chat", {
        method: "POST",
        headers: { "Content-Type": "application/json" },
        body: JSON.stringify({
          messages: [
            {
              role: "user",
              parts: [{ type: "text", text: text }],
            },
          ],
        }),
      });
      if (!res.ok) {
        const err = await res.text();
        throw new Error(err || res.statusText);
      }
      const reader = res.body.getReader();
      const decoder = new TextDecoder();
      let pending = "";
      while (true) {
        const { done, value } = await reader.read();
        if (done) break;
        pending += decoder.decode(value, { stream: true });
        pending = parseAiSdkStream(pending, (prefix, payload) => {
          if (prefix === "0") {
            bodyText += typeof payload === "string" ? payload : "";
            assistantEl.lastChild.textContent = bodyText;
            messagesEl.scrollTop = messagesEl.scrollHeight;
          } else if (prefix === "9") {
            const name = (payload && payload.toolName) || "tool";
            addMsg("tool", "→ " + name + " " + JSON.stringify((payload && payload.args) || {}));
          } else if (prefix === "a") {
            const out = payload && payload.result;
            addMsg(
              "tool",
              "← " +
                (typeof out === "string" ? out : JSON.stringify(out || {})),
            );
          } else if (prefix === "3") {
            addMsg(
              "assistant",
              String(payload || "error"),
              "error",
            );
          }
        });
      }
      if (!bodyText) {
        assistantEl.lastChild.textContent = "(no reply)";
      }
    } catch (e) {
      assistantEl.classList.add("error");
      assistantEl.lastChild.textContent = String(e.message || e);
    } finally {
      busy = false;
      btnSend.disabled = false;
      inputEl.focus();
      refreshAttachments();
      refreshSnapshot();
    }
  }

  btnSend.addEventListener("click", sendMessage);
  inputEl.addEventListener("keydown", (ev) => {
    if (ev.key === "Enter" && !ev.shiftKey) {
      ev.preventDefault();
      sendMessage();
    }
  });
  $("btnSave").addEventListener("click", saveSettings);
  $("btnRefreshAtt").addEventListener("click", refreshAttachments);
  $("btnClearAtt").addEventListener("click", async () => {
    await api("/api/attachments/clear", { method: "POST", body: "{}" });
    refreshAttachments();
  });

  refreshHealth();
  loadSettings();
  refreshAttachments();
  refreshSnapshot();
  setInterval(refreshHealth, 15000);
  setInterval(refreshAttachments, 5000);
  setInterval(refreshSnapshot, 8000);
})();
