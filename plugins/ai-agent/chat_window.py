# -*- coding: utf-8 -*-
"""Chat window: transcript, tool trace, provider settings, HITL approval."""

from __future__ import annotations

import json
import threading

from PyQt6.QtCore import QThread, pyqtSignal
from PyQt6.QtWidgets import (
    QComboBox,
    QHBoxLayout,
    QLabel,
    QLineEdit,
    QMainWindow,
    QMessageBox,
    QPlainTextEdit,
    QPushButton,
    QTextEdit,
    QVBoxLayout,
    QWidget,
)

from _shared import vscode_theme, plugin_shell

from agent.llm_client import LLMClient
from agent.orchestrator import Orchestrator
from agent.prompts import ROLES
from agent.session_store import SessionStore, load_settings, save_settings
from tools.host import SinHost
from tools.policy import Policy
from tools.registry import ToolRegistry

ANALYZE_PROMPT = (
    "Which CAN IDs transmit the most in the recent trace? "
    "Use frames_stats. Rank IDs in hex, give counts and share, "
    "and include DBC message names and decoded signals from the tool result. "
    "Do not invent names that the tool did not return."
)

_PRESETS = {
    "OpenAI": ("https://api.openai.com/v1", "gpt-4o-mini"),
    "Ollama": ("http://127.0.0.1:11434/v1", "llama3.2"),
    "Custom": ("", ""),
}


class _RunThread(QThread):
    step = pyqtSignal(str)
    done = pyqtSignal(str)
    failed = pyqtSignal(str)
    approve_needed = pyqtSignal(dict)

    def __init__(self, orchestrator, text: str):
        super().__init__()
        self._orchestrator = orchestrator
        self._text = text
        self._approval_cond = threading.Condition()
        self._approval_answer = None

    def wait_approval(self, info: dict) -> bool:
        with self._approval_cond:
            self._approval_answer = None
        self.approve_needed.emit(info)
        with self._approval_cond:
            while self._approval_answer is None:
                self._approval_cond.wait(timeout=0.2)
            return bool(self._approval_answer)

    def resolve_approval(self, ok: bool) -> None:
        with self._approval_cond:
            self._approval_answer = bool(ok)
            self._approval_cond.notify_all()

    def run(self):
        try:
            answer = self._orchestrator.run(self._text, on_step=self.step.emit)
            self.done.emit(answer)
        except Exception as e:
            self.failed.emit("%s: %s" % (type(e).__name__, e))


class ChatWindow(QMainWindow):
    def __init__(self):
        super().__init__()
        self.setWindowTitle("AI Agent")
        self.setMinimumSize(1100, 720)
        self.resize(1180, 800)

        self._store = SessionStore()
        self._policy = Policy("readonly")
        self._registry = ToolRegistry(SinHost(), self._policy, approval_fn=None)
        self._llm = LLMClient("", "", "")
        self._orch = Orchestrator(self._llm, self._registry, self._store)
        self._thread = None
        self._busy = False
        self._report_lines = []

        vscode_theme.apply(self)
        plugin_shell.attach_status_bar(self, "Session %s" % self._store.session_id)

        central = QWidget()
        central.setObjectName("SuiteContent")
        self.setCentralWidget(central)
        root = QVBoxLayout(central)
        root.setContentsMargins(0, 0, 0, 0)
        root.setSpacing(0)

        toolbar = QWidget()
        toolbar.setObjectName("SuiteToolbar")
        settings = QHBoxLayout(toolbar)
        settings.setContentsMargins(12, 6, 12, 6)
        settings.setSpacing(8)
        _t = QLabel("PROVIDER")
        _t.setObjectName("SuiteToolbarTitle")
        settings.addWidget(_t)
        self.provider = QComboBox()
        self.provider.addItems(list(_PRESETS.keys()))
        self.base_url = QLineEdit()
        self.base_url.setPlaceholderText("Base URL")
        self.model = QLineEdit()
        self.model.setPlaceholderText("Model")
        self.api_key = QLineEdit()
        self.api_key.setPlaceholderText("API key")
        self.api_key.setEchoMode(QLineEdit.EchoMode.Password)
        save_btn = QPushButton("Save")
        save_btn.setToolTip("Stored in the local ai-agent settings file. Not written to git.")
        settings.addWidget(self.provider)
        settings.addWidget(self.base_url, 2)
        settings.addWidget(self.model, 1)
        settings.addWidget(self.api_key, 1)
        settings.addWidget(save_btn)
        root.addWidget(toolbar)

        actions_bar = QWidget()
        actions = QHBoxLayout(actions_bar)
        actions.setContentsMargins(12, 8, 12, 8)
        actions.setSpacing(8)
        self.role = QComboBox()
        self.role.addItems(list(ROLES))
        self.analyze_btn = QPushButton("Analyze recent Trace")
        self.analyze_btn.setToolTip(
            "Asks who transmits the most, using read-only frame tools")
        new_btn = QPushButton("New chat")
        self.policy_label = QLabel("Policy: readonly")
        actions.addWidget(QLabel("Role"))
        actions.addWidget(self.role)
        actions.addWidget(self.analyze_btn)
        actions.addWidget(new_btn)
        actions.addStretch(1)
        actions.addWidget(self.policy_label)
        root.addWidget(actions_bar)

        body = QWidget()
        body_l = QVBoxLayout(body)
        body_l.setContentsMargins(16, 8, 16, 12)
        body_l.setSpacing(8)

        self.transcript = QTextEdit()
        self.transcript.setReadOnly(True)
        self.transcript.setPlaceholderText(
            "Ask about the recent trace. Example: who is transmitting the most?")
        body_l.addWidget(self.transcript, 1)

        tool_title = QLabel("TOOL TRACE")
        tool_title.setObjectName("SuiteToolbarTitle")
        body_l.addWidget(tool_title)
        self.trace = QPlainTextEdit()
        self.trace.setReadOnly(True)
        self.trace.setFixedHeight(140)
        body_l.addWidget(self.trace)

        row = QHBoxLayout()
        self.input = QLineEdit()
        self.input.setPlaceholderText(
            "Message, or /read-only  /allow-tx  /diag-write  /report")
        self.send_btn = QPushButton("Send")
        row.addWidget(self.input, 1)
        row.addWidget(self.send_btn)
        body_l.addLayout(row)
        root.addWidget(body, 1)

        self.provider.currentTextChanged.connect(self._apply_preset)
        save_btn.clicked.connect(self._save_settings)
        self.role.currentTextChanged.connect(self._orch.set_role)
        self.analyze_btn.clicked.connect(self._analyze)
        new_btn.clicked.connect(self._new_chat)
        self.send_btn.clicked.connect(self._send)
        self.input.returnPressed.connect(self._send)

        self._load_settings()
        self._sync_policy_label()

    def _load_settings(self):
        data = load_settings()
        provider = data.get("provider") or "Ollama"
        if provider not in _PRESETS:
            provider = "Custom"
        self.provider.setCurrentText(provider)
        self.base_url.setText(data.get("base_url") or _PRESETS[provider][0])
        self.model.setText(data.get("model") or _PRESETS[provider][1])
        self.api_key.setText(data.get("api_key") or "")
        role = data.get("role") or "Analyst"
        if role in ROLES:
            self.role.setCurrentText(role)
        level = data.get("policy") or "readonly"
        try:
            self._policy.set_level(level)
        except ValueError:
            self._policy.set_level("readonly")
        self._push_client()

    def _apply_preset(self, name: str):
        if name not in _PRESETS or name == "Custom":
            return
        url, model = _PRESETS[name]
        self.base_url.setText(url)
        self.model.setText(model)
        if name == "Ollama" and not self.api_key.text().strip():
            self.api_key.setText("ollama")

    def _save_settings(self):
        data = {
            "provider": self.provider.currentText(),
            "base_url": self.base_url.text().strip(),
            "model": self.model.text().strip(),
            "api_key": self.api_key.text().strip(),
            "role": self.role.currentText(),
            "policy": self._policy.level,
        }
        path = save_settings(data)
        self._push_client()
        plugin_shell.set_status(self, "Settings saved (%s)" % path, 4000)

    def _push_client(self):
        self._llm.base_url = self.base_url.text().strip().rstrip("/")
        self._llm.api_key = self.api_key.text().strip()
        self._llm.model = self.model.text().strip()
        self._orch.set_role(self.role.currentText())
        self._orch.refresh_policy_prompt()

    def _sync_policy_label(self):
        self.policy_label.setText("Policy: %s" % self._policy.level)

    def _append(self, title: str, body: str):
        self.transcript.append("%s\n%s\n" % (title, body))
        self._report_lines.append("## %s\n\n%s" % (title, body))

    def _set_busy(self, busy: bool):
        self._busy = busy
        self.send_btn.setEnabled(not busy)
        self.analyze_btn.setEnabled(not busy)
        self.input.setEnabled(not busy)

    def _analyze(self):
        self._start(ANALYZE_PROMPT, echo="Analyze recent Trace")

    def _send(self):
        text = self.input.text().strip()
        if not text:
            return
        self.input.clear()
        if text.startswith("/"):
            self._slash(text)
            return
        self._start(text, echo=text)

    def _slash(self, text: str):
        cmd = text.split()[0].lower()
        if cmd == "/read-only":
            self._policy.set_level("readonly")
            self._sync_policy_label()
            self._orch.refresh_policy_prompt()
            self._append("System", "Policy is readonly. Write tools stay hidden.")
            return
        if cmd == "/allow-tx":
            self._policy.set_level("tx_allowed")
            self._sync_policy_label()
            self._orch.refresh_policy_prompt()
            self._append(
                "System",
                "Policy is tx_allowed. frames_send / uds_read_did / obd_read_pid "
                "are available and still require approval.")
            return
        if cmd in ("/diag-write", "/diag"):
            self._policy.set_level("diag_write")
            self._sync_policy_label()
            self._orch.refresh_policy_prompt()
            self._append(
                "System",
                "Policy is diag_write. Diagnostic write tools may appear when registered.")
            return
        if cmd == "/report":
            if not self._report_lines:
                self._append("System", "Nothing to export yet.")
                return
            path = self._store.export_markdown(self._report_lines)
            self._append("System", "Report written to %s" % path)
            return
        self._append(
            "System",
            "Unknown command. Try /read-only, /allow-tx, /diag-write, /report.")

    def _new_chat(self):
        if self._busy:
            return
        self._orch.reset()
        self.transcript.clear()
        self.trace.clear()
        self._report_lines = []
        plugin_shell.set_status(
            self, "New chat in session %s" % self._store.session_id, 3000)

    def _format_approval(self, info: dict) -> str:
        args = info.get("arguments") or {}
        pretty = json.dumps(args, indent=2, ensure_ascii=False)
        return (
            "Approve tool call?\n\n"
            "Tool: %s\n"
            "Permission: %s\n"
            "Policy: %s\n\n"
            "Arguments:\n%s"
        ) % (
            info.get("tool"),
            info.get("permission"),
            info.get("policy"),
            pretty,
        )

    def _on_approve_needed(self, info: dict):
        text = self._format_approval(info)
        box = QMessageBox(self)
        box.setIcon(QMessageBox.Icon.Warning)
        box.setWindowTitle("Approve tool")
        box.setText(text)
        box.setStandardButtons(
            QMessageBox.StandardButton.Yes | QMessageBox.StandardButton.No)
        box.setDefaultButton(QMessageBox.StandardButton.No)
        ok = box.exec() == QMessageBox.StandardButton.Yes
        if self._thread is not None:
            self._thread.resolve_approval(ok)

    def _start(self, text: str, echo: str):
        if self._busy:
            return
        self._push_client()
        self._append("You", echo)
        self._set_busy(True)
        plugin_shell.set_status(self, "Running")
        self._thread = _RunThread(self._orch, text)
        # Bind approval to this worker so the GUI thread can answer.
        self._registry.approval_fn = self._thread.wait_approval
        self._thread.step.connect(self._on_step)
        self._thread.done.connect(self._on_done)
        self._thread.failed.connect(self._on_failed)
        self._thread.approve_needed.connect(self._on_approve_needed)
        self._thread.start()

    def _on_step(self, line: str):
        self.trace.appendPlainText(line)

    def _on_done(self, answer: str):
        self._append("Agent", answer)
        self._set_busy(False)
        self._registry.approval_fn = None
        plugin_shell.set_status(self, "Ready")

    def _on_failed(self, message: str):
        self._append("Error", message)
        self._set_busy(False)
        self._registry.approval_fn = None
        plugin_shell.set_status(self, "Failed")

    def shutdown(self):
        if self._thread is not None and self._thread.isRunning():
            self._thread.resolve_approval(False)
            self._thread.wait(1500)
