"""Tk desktop controller for the native Windows LDAC transport."""

from __future__ import annotations

from collections import deque
import ctypes
from ctypes import wintypes
from datetime import datetime
import locale
from pathlib import Path
import queue
import subprocess
import threading
import time
import tkinter as tk
from tkinter import messagebox, ttk
import uuid

from agent_config import (
    AgentState,
    AgentTelemetry,
    config_path as default_agent_config_path,
    load_config,
    load_state,
    probe_log_path as default_agent_probe_log_path,
    process_is_running,
    state_path as default_agent_state_path,
    update_config,
)
from daily_state import (
    DailyState,
    daily_state_path as default_daily_state_path,
    daily_transport_result_path as default_daily_transport_result_path,
    daily_ui_log_root,
    derive_daily_presentation,
    load_daily_state,
    load_daily_transport_result,
)
from daily_config_ipc import request_quality

from probe_protocol import (
    AbrChange,
    EndpointMetrics,
    LiveMetrics,
    NativeEndpointSource,
    WasapiSource,
    parse_probe_line,
)


APP_TITLE = "Native LDAC for Windows"
BACKGROUND = "#f3f5f7"
CARD = "#ffffff"
TEXT = "#17202a"
MUTED = "#68737d"
ACCENT = "#c66a00"
GREEN = "#2eae64"
BLUE = "#4f79d8"
RED = "#c23b3b"
GRID = "#dfe4e8"

QUALITY_OPTIONS = {
    "自动（HQ / SQ / MQ）": "auto",
    "HQ（高，990 kbps）": "hq",
    "SQ（中，660 kbps）": "sq",
    "MQ（低，330 kbps）": "mq",
}
QUALITY_LABELS = {value: label for label, value in QUALITY_OPTIONS.items()}
CHANNEL_MODE_OPTIONS = {
    "立体声": "stereo",
    "双声道（独立编码）": "dual",
    "单声道（自动混合）": "mono",
}
CHANNEL_MODE_LABELS = {
    value: label for label, value in CHANNEL_MODE_OPTIONS.items()
}
SAMPLE_RATE_OPTIONS = {
    "44.1 kHz": 44100,
    "48 kHz": 48000,
    "88.2 kHz": 88200,
    "96 kHz": 96000,
}
SAMPLE_RATE_LABELS = {
    value: label for label, value in SAMPLE_RATE_OPTIONS.items()
}
BIT_DEPTH_OPTIONS = {"16 位": 16, "24 位": 24}
BIT_DEPTH_LABELS = {
    value: label for label, value in BIT_DEPTH_OPTIONS.items()
}


def _format_metric(value: float) -> str:
    return f"{value:.4f}".rstrip("0").rstrip(".")


def format_agent_telemetry(telemetry: AgentTelemetry) -> str:
    """Build a compact, read-only summary for fields the agent published."""

    items: list[str] = []
    if telemetry.transport_policy is not None:
        policy = telemetry.transport_policy
        if telemetry.transport_policy_version is not None:
            policy += f" P{telemetry.transport_policy_version}"
        items.append(policy)
    elif telemetry.transport_policy_version is not None:
        items.append(f"Policy P{telemetry.transport_policy_version}")
    if telemetry.acl_generation is not None:
        items.append(f"ACL {telemetry.acl_generation}")
    if telemetry.render_demand is not None:
        items.append(f"Render {telemetry.render_demand}")
    if telemetry.open_attempts is not None:
        attempts = str(telemetry.open_attempts)
        if telemetry.maximum_open_attempts is not None:
            attempts += f"/{telemetry.maximum_open_attempts}"
        items.append(f"OPEN {attempts}")
    if telemetry.retryable_failures is not None:
        items.append(f"Retry {telemetry.retryable_failures}")
    if telemetry.retries_scheduled is not None:
        items.append(f"Scheduled {telemetry.retries_scheduled}")
    if telemetry.retry_reasons:
        latest = telemetry.retry_reasons[-1]
        if len(latest) > 40:
            latest = latest[:37] + "…"
        items.append(f"原因 {latest}")
    if telemetry.pcm_prepare_attempts is not None:
        items.append(f"PCM prepare {telemetry.pcm_prepare_attempts}")
    if telemetry.pcm_epoch_restarts is not None:
        items.append(f"epoch +{telemetry.pcm_epoch_restarts}")

    lease = None
    if telemetry.consumer_lease_acquire_count is not None:
        lease = str(telemetry.consumer_lease_acquire_count)
        if telemetry.consumer_lease_release_count is not None:
            lease += f"/{telemetry.consumer_lease_release_count}"
    elif telemetry.consumer_lease_acquired is not None:
        lease = "已获取" if telemetry.consumer_lease_acquired else "未获取"
    if lease is not None:
        if telemetry.consumer_lease_released is True:
            lease += " 已释放"
        elif telemetry.consumer_lease_released is False:
            lease += " 持有中"
        items.append(f"Lease {lease}")

    if telemetry.maximum_output_peak_ceiling is not None:
        items.append(
            "Limiter " + _format_metric(
                telemetry.maximum_output_peak_ceiling
            )
        )
    if telemetry.maximum_unlimited_post_gain_peak is not None:
        peak = _format_metric(telemetry.maximum_unlimited_post_gain_peak)
        if telemetry.maximum_post_gain_peak is not None:
            peak += "→" + _format_metric(telemetry.maximum_post_gain_peak)
        items.append(f"Peak {peak}")
    elif telemetry.maximum_post_gain_peak is not None:
        items.append(
            f"Peak {_format_metric(telemetry.maximum_post_gain_peak)}"
        )
    if telemetry.limited_output_samples is not None:
        items.append(f"Limited {telemetry.limited_output_samples}")
    if telemetry.stop_reason is not None:
        items.append(f"Stop {telemetry.stop_reason}")
    if (telemetry.graceful_stop_actions is not None or
            telemetry.cancel_actions is not None):
        graceful = (
            "—" if telemetry.graceful_stop_actions is None
            else str(telemetry.graceful_stop_actions)
        )
        cancelled = (
            "—" if telemetry.cancel_actions is None
            else str(telemetry.cancel_actions)
        )
        items.append(f"Stop G/C {graceful}/{cancelled}")
    if telemetry.media_duration_ms is not None:
        items.append(f"Media {telemetry.media_duration_ms} ms")
    if telemetry.final_attempt_archived is not None:
        archived = "yes" if telemetry.final_attempt_archived else "no"
        items.append(f"Archive {archived}")
    if telemetry.resources_released is not None:
        released = "released" if telemetry.resources_released else "held"
        items.append(f"Resources {released}")
    if telemetry.lifecycle_outcome is not None:
        items.append(f"Lifecycle {telemetry.lifecycle_outcome}")
    return " · ".join(items)


def load_agent_state_for_ui(path: Path) -> AgentState | None:
    """Return no state for absent, partial, or invalid atomic snapshots."""

    try:
        return load_state(path)
    except (OSError, ValueError):
        return None


class NamedStopEvent:
    """Manual-reset event shared with transport_probe.exe by name."""

    def __init__(self) -> None:
        if not hasattr(ctypes, "WinDLL"):
            raise OSError("The LDAC UI requires Windows")
        self.name = f"Local\\LdacNativeStop-{uuid.uuid4()}"
        kernel32 = ctypes.WinDLL("kernel32", use_last_error=True)
        self._kernel32 = kernel32
        kernel32.CreateEventW.argtypes = (
            wintypes.LPVOID,
            wintypes.BOOL,
            wintypes.BOOL,
            wintypes.LPCWSTR,
        )
        kernel32.CreateEventW.restype = wintypes.HANDLE
        kernel32.SetEvent.argtypes = (wintypes.HANDLE,)
        kernel32.SetEvent.restype = wintypes.BOOL
        kernel32.CloseHandle.argtypes = (wintypes.HANDLE,)
        kernel32.CloseHandle.restype = wintypes.BOOL
        self._handle = kernel32.CreateEventW(None, True, False, self.name)
        if not self._handle:
            raise ctypes.WinError(ctypes.get_last_error())

    def set(self) -> None:
        if self._handle and not self._kernel32.SetEvent(self._handle):
            raise ctypes.WinError(ctypes.get_last_error())

    def close(self) -> None:
        if self._handle:
            self._kernel32.CloseHandle(self._handle)
            self._handle = None


class BitrateChart(tk.Canvas):
    def __init__(self, master: tk.Misc, **kwargs: object) -> None:
        super().__init__(master, background=CARD, highlightthickness=0, **kwargs)
        self._samples: deque[tuple[float, float]] = deque(maxlen=60)
        self.bind("<Configure>", lambda _event: self.redraw())

    def clear(self) -> None:
        self._samples.clear()
        self.redraw()

    def add(self, ldac_kbps: float, transport_kbps: float) -> None:
        self._samples.append((ldac_kbps, transport_kbps))
        self.redraw()

    def redraw(self) -> None:
        self.delete("all")
        width = max(self.winfo_width(), 400)
        height = max(self.winfo_height(), 180)
        left, right, top, bottom = 50, 18, 14, 28
        plot_width = width - left - right
        plot_height = height - top - bottom
        maximum = 1100.0

        for value in (0, 330, 660, 990):
            y = top + plot_height * (1.0 - value / maximum)
            self.create_line(left, y, width - right, y, fill=GRID)
            self.create_text(
                left - 8,
                y,
                text=str(value),
                fill=MUTED,
                anchor="e",
                font=("Segoe UI", 9),
            )
        self.create_text(
            width - right,
            height - 8,
            text="最近 60 秒",
            fill=MUTED,
            anchor="e",
            font=("Microsoft YaHei UI", 9),
        )
        if not self._samples:
            self.create_text(
                left + plot_width / 2,
                top + plot_height / 2,
                text="等待实时码率数据",
                fill=MUTED,
                font=("Microsoft YaHei UI", 11),
            )
            return

        samples = list(self._samples)
        span = max(len(samples) - 1, 1)

        def points(index: int) -> tuple[float, float, float]:
            x = left + plot_width * index / span
            ldac_y = top + plot_height * (1.0 - min(samples[index][0], maximum) / maximum)
            rtp_y = top + plot_height * (1.0 - min(samples[index][1], maximum) / maximum)
            return x, ldac_y, rtp_y

        ldac_points: list[float] = []
        rtp_points: list[float] = []
        for index in range(len(samples)):
            x, ldac_y, rtp_y = points(index)
            ldac_points.extend((x, ldac_y))
            rtp_points.extend((x, rtp_y))
        if len(samples) == 1:
            x, ldac_y, rtp_y = points(0)
            self.create_oval(x - 2, ldac_y - 2, x + 2, ldac_y + 2, fill=ACCENT, outline="")
            self.create_oval(x - 2, rtp_y - 2, x + 2, rtp_y + 2, fill=BLUE, outline="")
        else:
            self.create_line(*rtp_points, fill=BLUE, width=2, dash=(5, 3), smooth=True)
            self.create_line(*ldac_points, fill=ACCENT, width=2, smooth=True)
        self.create_line(left + 8, top + 8, left + 28, top + 8, fill=ACCENT, width=2)
        self.create_text(left + 34, top + 8, text="LDAC", anchor="w", fill=TEXT, font=("Segoe UI", 9))
        self.create_line(left + 88, top + 8, left + 108, top + 8, fill=BLUE, width=2, dash=(5, 3))
        self.create_text(left + 114, top + 8, text="含 RTP", anchor="w", fill=TEXT, font=("Microsoft YaHei UI", 9))


class LdacControlApp:
    def __init__(self, root: tk.Tk) -> None:
        self.root = root
        self.repo_root = Path(__file__).resolve().parents[1]
        self.daily_state_path = default_daily_state_path()
        self.daily_transport_result_path = (
            default_daily_transport_result_path()
        )
        agent_probe = (
            self.repo_root / "artifacts" / "agent" / "transport_probe.exe"
        )
        legacy_probe = (
            self.repo_root
            / "artifacts"
            / "driver-test"
            / "transport_probe.exe"
        )
        self.probe_path = agent_probe if agent_probe.exists() else legacy_probe
        self.log_path: Path | None = None
        try:
            log_root = daily_ui_log_root()
            log_root.mkdir(parents=True, exist_ok=True)
            self.log_path = log_root / datetime.now().strftime(
                "ldac-ui-%Y%m%d-%H%M%S.log"
            )
        except OSError:
            self.log_path = None
        self.events: queue.Queue[tuple[str, object]] = queue.Queue()
        self.process: subprocess.Popen[str] | None = None
        self.stop_event: NamedStopEvent | None = None
        self.stop_requested = False
        self.closing = False
        self.daily_mode = False
        self.daily_generation: int | None = None
        self.agent_mode = False
        self.agent_generation: int | None = None
        self.agent_probe_log_offset = 0
        self.next_agent_poll = 0.0
        self.agent_config_path = default_agent_config_path()
        self.agent_state_path = default_agent_state_path()
        self.agent_probe_log_path = default_agent_probe_log_path()

        self.status_var = tk.StringVar(value="未连接")
        self.device_var = tk.StringVar(value="Sony WH-1000XM5")
        self.source_var = tk.StringVar(
            value="输出设备：Native LDAC - WH-1000XM5"
        )
        self.address_var = tk.StringVar(value="")
        self.quality_var = tk.StringVar(value="自动（HQ / SQ / MQ）")
        self.channel_mode_var = tk.StringVar(value="立体声")
        self.sample_rate_var = tk.StringVar(value="48 kHz")
        self.bit_depth_var = tk.StringVar(value="16 位")
        self.actual_quality_var = tk.StringVar(value="—")
        self.ldac_var = tk.StringVar(value="—")
        self.bitrate_caption_var = tk.StringVar(value="kbps  LDAC")
        self.transport_var = tk.StringVar(value="—")
        self.write_var = tk.StringVar(value="—")
        self.packet_var = tk.StringVar(value="—")
        self.endpoint_state_var = tk.StringVar(value="等待连接")
        self.endpoint_buffer_var = tk.StringVar(value="—")
        self.endpoint_volume_var = tk.StringVar(value="—")
        self.endpoint_health_var = tk.StringVar(value="—")
        self.agent_telemetry_var = tk.StringVar(value="")

        self._configure_window()
        self._build_ui()
        self.quality_box.bind(
            "<<ComboboxSelected>>", self._on_quality_selected
        )
        self.channel_mode_box.bind(
            "<<ComboboxSelected>>", self._on_channel_mode_selected
        )
        self.sample_rate_box.bind(
            "<<ComboboxSelected>>", self._on_format_selected
        )
        self.bit_depth_box.bind(
            "<<ComboboxSelected>>", self._on_format_selected
        )
        if self.log_path is not None:
            self._append_log(f"UI log: {self.log_path}")
        self.root.protocol("WM_DELETE_WINDOW", self._on_close)
        self._refresh_agent_state(force=True)
        self.root.after(50, self._poll_events)

    def _configure_window(self) -> None:
        self.root.title(APP_TITLE)
        self.root.geometry("920x760")
        self.root.minsize(780, 660)
        self.root.configure(background=BACKGROUND)
        style = ttk.Style(self.root)
        style.theme_use("clam")
        style.configure(
            "Quality.TCombobox",
            fieldbackground=CARD,
            background=CARD,
            padding=7,
            font=("Microsoft YaHei UI", 10),
        )

    def _build_ui(self) -> None:
        container = tk.Frame(self.root, bg=BACKGROUND, padx=18, pady=16)
        container.pack(fill="both", expand=True)

        header = tk.Frame(container, bg=CARD, padx=18, pady=15)
        header.pack(fill="x")
        left = tk.Frame(header, bg=CARD)
        left.pack(side="left", fill="x", expand=True)
        tk.Label(left, textvariable=self.device_var, bg=CARD, fg=TEXT,
                 font=("Microsoft YaHei UI", 16)).pack(anchor="w")
        status_row = tk.Frame(left, bg=CARD)
        status_row.pack(anchor="w", pady=(5, 0))
        self.status_dot = tk.Canvas(status_row, width=12, height=12, bg=CARD,
                                    highlightthickness=0)
        self.status_dot.pack(side="left", padx=(0, 6))
        self._set_status_dot(MUTED)
        tk.Label(status_row, textvariable=self.status_var, bg=CARD, fg=MUTED,
                 font=("Microsoft YaHei UI", 10)).pack(side="left")
        tk.Label(left, textvariable=self.source_var, bg=CARD, fg=MUTED,
                 font=("Microsoft YaHei UI", 9)).pack(anchor="w", pady=(4, 0))
        self.agent_telemetry_label = tk.Label(
            left,
            textvariable=self.agent_telemetry_var,
            bg=CARD,
            fg=MUTED,
            font=("Microsoft YaHei UI", 8),
            justify="left",
            anchor="w",
            wraplength=620,
        )

        bitrate = tk.Frame(header, bg=CARD)
        bitrate.pack(side="right", padx=(20, 0))
        tk.Label(bitrate, textvariable=self.ldac_var, bg=CARD, fg=TEXT,
                 font=("Segoe UI Semibold", 25)).pack(anchor="e")
        tk.Label(bitrate, textvariable=self.bitrate_caption_var,
                 bg=CARD, fg=MUTED,
                 font=("Segoe UI", 9)).pack(anchor="e")

        controls = tk.Frame(container, bg=CARD, padx=18, pady=13)
        controls.pack(fill="x", pady=(12, 0))
        tk.Label(controls, text="编码质量", bg=CARD, fg=TEXT,
                 font=("Microsoft YaHei UI", 10)).pack(side="left")
        self.quality_box = ttk.Combobox(
            controls,
            textvariable=self.quality_var,
            values=list(QUALITY_OPTIONS),
            state="readonly",
            width=24,
            style="Quality.TCombobox",
        )
        self.quality_box.pack(side="left", padx=(12, 18))
        tk.Label(controls, text="声道", bg=CARD, fg=TEXT,
                 font=("Microsoft YaHei UI", 10)).pack(side="left")
        self.channel_mode_box = ttk.Combobox(
            controls,
            textvariable=self.channel_mode_var,
            values=list(CHANNEL_MODE_OPTIONS),
            state="readonly",
            width=16,
            style="Quality.TCombobox",
        )
        self.channel_mode_box.pack(side="left", padx=(12, 18))

        format_controls = tk.Frame(container, bg=CARD, padx=18, pady=11)
        format_controls.pack(fill="x", pady=(1, 0))
        tk.Label(format_controls, text="采样率", bg=CARD, fg=TEXT,
                 font=("Microsoft YaHei UI", 10)).pack(side="left")
        self.sample_rate_box = ttk.Combobox(
            format_controls,
            textvariable=self.sample_rate_var,
            values=list(SAMPLE_RATE_OPTIONS),
            state="readonly",
            width=11,
            style="Quality.TCombobox",
        )
        self.sample_rate_box.pack(side="left", padx=(12, 24))
        tk.Label(format_controls, text="位深", bg=CARD, fg=TEXT,
                 font=("Microsoft YaHei UI", 10)).pack(side="left")
        self.bit_depth_box = ttk.Combobox(
            format_controls,
            textvariable=self.bit_depth_var,
            values=list(BIT_DEPTH_OPTIONS),
            state="readonly",
            width=9,
            style="Quality.TCombobox",
        )
        self.bit_depth_box.pack(side="left", padx=(12, 18))
        tk.Label(
            format_controls,
            text="修改后会安全关闭当前媒体会话，并通知 Windows 按新格式重开。",
            bg=CARD,
            fg=MUTED,
            font=("Microsoft YaHei UI", 9),
        ).pack(side="left")
        self.start_button = tk.Button(
            controls,
            text="开始 LDAC",
            command=self.start,
            bg=ACCENT,
            fg="white",
            activebackground="#a95800",
            activeforeground="white",
            relief="flat",
            padx=18,
            pady=8,
            font=("Microsoft YaHei UI", 10),
            cursor="hand2",
        )
        self.start_button.pack(side="left")
        self.stop_button = tk.Button(
            controls,
            text="安全停止",
            command=self.stop,
            bg="#e8ebee",
            fg=TEXT,
            activebackground="#d9dde1",
            relief="flat",
            padx=18,
            pady=8,
            font=("Microsoft YaHei UI", 10),
            state="disabled",
        )
        self.stop_button.pack(side="left", padx=(9, 0))

        metrics = tk.Frame(container, bg=BACKGROUND)
        metrics.pack(fill="x", pady=(12, 0))
        self._metric_card(metrics, "当前档位", self.actual_quality_var).pack(side="left", fill="x", expand=True)
        self._metric_card(metrics, "含 RTP 码率", self.transport_var).pack(side="left", fill="x", expand=True, padx=8)
        self._metric_card(metrics, "写入 平均 / 最大", self.write_var).pack(side="left", fill="x", expand=True)
        self._metric_card(metrics, "每秒包数", self.packet_var).pack(side="left", fill="x", expand=True, padx=(8, 0))

        endpoint_metrics = tk.Frame(container, bg=BACKGROUND)
        endpoint_metrics.pack(fill="x", pady=(8, 0))
        self._metric_card(endpoint_metrics, "虚拟端点", self.endpoint_state_var).pack(
            side="left", fill="x", expand=True
        )
        self._metric_card(endpoint_metrics, "PCM 缓冲", self.endpoint_buffer_var).pack(
            side="left", fill="x", expand=True, padx=8
        )
        self._metric_card(endpoint_metrics, "系统音量", self.endpoint_volume_var).pack(
            side="left", fill="x", expand=True
        )
        self._metric_card(endpoint_metrics, "丢弃 / 静音填充", self.endpoint_health_var).pack(
            side="left", fill="x", expand=True, padx=(8, 0)
        )

        chart_card = tk.Frame(container, bg=CARD, padx=12, pady=10)
        chart_card.pack(fill="both", expand=True, pady=(12, 0))
        tk.Label(chart_card, text="实时码率", bg=CARD, fg=TEXT,
                 font=("Microsoft YaHei UI", 11)).pack(anchor="w", padx=6)
        self.chart = BitrateChart(chart_card, height=165)
        self.chart.pack(fill="both", expand=True, pady=(6, 0))

        log_card = tk.Frame(container, bg=CARD, padx=12, pady=10)
        log_card.pack(fill="x", pady=(12, 0))
        tk.Label(log_card, text="诊断日志", bg=CARD, fg=TEXT,
                 font=("Microsoft YaHei UI", 10)).pack(anchor="w", padx=4)
        self.log = tk.Text(
            log_card,
            height=4,
            bg="#f8f9fa",
            fg="#3d454d",
            relief="flat",
            font=("Consolas", 8),
            wrap="none",
            state="disabled",
        )
        self.log.pack(fill="x", pady=(6, 0))

    def _metric_card(self, master: tk.Misc, title: str, variable: tk.StringVar) -> tk.Frame:
        frame = tk.Frame(master, bg=CARD, padx=13, pady=10)
        tk.Label(frame, text=title, bg=CARD, fg=MUTED,
                 font=("Microsoft YaHei UI", 8)).pack(anchor="w")
        tk.Label(frame, textvariable=variable, bg=CARD, fg=TEXT,
                 font=("Segoe UI Semibold", 12)).pack(anchor="w", pady=(3, 0))
        return frame

    def _set_status_dot(self, color: str) -> None:
        self.status_dot.delete("all")
        self.status_dot.create_oval(2, 2, 10, 10, fill=color, outline="")

    def _set_status(self, text: str, color: str = MUTED) -> None:
        self.status_var.set(text)
        self._set_status_dot(color)

    def _set_agent_telemetry(self, telemetry: AgentTelemetry | None) -> None:
        text = "" if telemetry is None else format_agent_telemetry(telemetry)
        self.agent_telemetry_var.set(text)
        if text:
            if not self.agent_telemetry_label.winfo_manager():
                self.agent_telemetry_label.pack(anchor="w", pady=(3, 0))
        elif self.agent_telemetry_label.winfo_manager():
            self.agent_telemetry_label.pack_forget()

    def _write_agent_config(
        self,
        *,
        enabled: bool | None = None,
        quality: str | None = None,
        channel_mode: str | None = None,
        sample_rate: int | None = None,
        bits_per_sample: int | None = None,
    ) -> bool:
        try:
            config = update_config(
                self.agent_config_path,
                enabled=enabled,
                quality=quality,
                channel_mode=channel_mode,
                sample_rate=sample_rate,
                bits_per_sample=bits_per_sample,
            )
        except (OSError, ValueError) as error:
            messagebox.showerror(APP_TITLE, f"无法保存后台配置：\n{error}")
            return False
        self._append_log(
            f"Agent config revision {config.revision}: "
            f"enabled={config.enabled}, quality={config.quality}, "
            f"channel_mode={config.channel_mode}, "
            f"format={config.sample_rate} Hz/{config.bits_per_sample}-bit"
        )
        return True

    def _on_quality_selected(self, _event: object | None = None) -> None:
        if self.daily_mode:
            state = self._load_daily_state_for_ui()
            if state is None:
                return
            quality = QUALITY_OPTIONS[self.quality_var.get()]
            if quality == "auto":
                self.quality_var.set(
                    QUALITY_LABELS.get(
                        state.applied_quality.lower(), QUALITY_LABELS["hq"]
                    )
                )
                self._append_log(
                    "Daily quality AUTO is not available in this phase."
                )
                return
            try:
                response = request_quality(
                    state.config_pipe,
                    quality,
                    max(state.requested_config_revision,
                        state.applied_config_revision) + 1,
                )
            except (OSError, ValueError) as error:
                messagebox.showerror(
                    APP_TITLE, f"无法提交 daily 质量设置：\n{error}"
                )
                return
            self._append_log(
                f"Daily quality request {quality} revision "
                f"{response.requested_revision}: {response.status}"
            )
            self._set_status(
                "质量设置已请求，等待安全会话边界应用…", ACCENT
            )
            return
        if not self.agent_mode:
            return
        quality = QUALITY_OPTIONS[self.quality_var.get()]
        if self._write_agent_config(quality=quality):
            self._set_status("正在应用质量设置…", ACCENT)

    def _on_channel_mode_selected(
        self, _event: object | None = None
    ) -> None:
        if not self.agent_mode:
            return
        channel_mode = CHANNEL_MODE_OPTIONS[self.channel_mode_var.get()]
        if self._write_agent_config(channel_mode=channel_mode):
            self._set_status("正在安全切换声道模式…", ACCENT)

    def _on_format_selected(self, _event: object | None = None) -> None:
        if not self.agent_mode:
            return
        sample_rate = SAMPLE_RATE_OPTIONS[self.sample_rate_var.get()]
        bits_per_sample = BIT_DEPTH_OPTIONS[self.bit_depth_var.get()]
        if self._write_agent_config(
            sample_rate=sample_rate,
            bits_per_sample=bits_per_sample,
        ):
            self._set_status("正在安全切换音频格式…", ACCENT)

    def _reset_agent_probe_tail(self) -> None:
        try:
            size = self.agent_probe_log_path.stat().st_size
        except OSError:
            self.agent_probe_log_offset = 0
            return
        self.agent_probe_log_offset = max(0, size - 64 * 1024)

    def _tail_agent_probe_log(self) -> None:
        try:
            size = self.agent_probe_log_path.stat().st_size
            if size < self.agent_probe_log_offset:
                self.agent_probe_log_offset = 0
            with self.agent_probe_log_path.open("rb") as stream:
                stream.seek(self.agent_probe_log_offset)
                data = stream.read(256 * 1024)
                self.agent_probe_log_offset = stream.tell()
        except OSError:
            return
        if not data:
            return
        encoding = locale.getpreferredencoding(False)
        for line in data.decode(encoding, errors="replace").splitlines():
            self._handle_line(line)

    def _load_daily_state_for_ui(self) -> DailyState | None:
        try:
            return load_daily_state(self.daily_state_path)
        except (OSError, ValueError):
            return None

    def _render_daily_state(self, state: DailyState) -> None:
        transport = None
        try:
            transport = load_daily_transport_result(
                self.daily_transport_result_path
            )
        except (OSError, ValueError):
            pass
        presentation = derive_daily_presentation(state, transport)
        colors = {"ok": GREEN, "warning": ACCENT, "error": RED}
        self._set_status(
            presentation.status, colors.get(presentation.severity, MUTED)
        )
        self.device_var.set(
            "Sony WH-1000XM5  ·  " + presentation.connection
        )
        self.source_var.set(presentation.last_format + " · Daily host")
        self.actual_quality_var.set(presentation.quality)
        self.quality_var.set(
            QUALITY_LABELS.get(
                state.requested_quality.lower(), QUALITY_LABELS["hq"]
            )
        )
        self.transport_var.set(presentation.last_bitrate)
        self.write_var.set(presentation.playback)
        self.packet_var.set(f"worker {state.transport_worker_sequence}")
        self.endpoint_state_var.set(presentation.connection)
        self.endpoint_buffer_var.set(presentation.playback)
        self.endpoint_volume_var.set(presentation.volume)
        self.endpoint_health_var.set(presentation.hfp)
        if presentation.last_bitrate == "—":
            self.ldac_var.set(state.applied_quality)
            self.bitrate_caption_var.set("当前配置质量")
        else:
            self.ldac_var.set(presentation.last_bitrate.split()[0])
            self.bitrate_caption_var.set("kbps  最近会话")
        self.agent_telemetry_var.set(presentation.details)
        if not self.agent_telemetry_label.winfo_manager():
            self.agent_telemetry_label.pack(anchor="w", pady=(3, 0))
        self.start_button.configure(text="后台常驻运行", state="disabled")
        self.stop_button.configure(text="后台独立运行", state="disabled")
        self.quality_box.configure(state="readonly")
        self.quality_box.configure(values=[
            QUALITY_LABELS["hq"],
            QUALITY_LABELS["sq"],
            QUALITY_LABELS["mq"],
        ])
        self.channel_mode_box.configure(state="disabled")
        self.sample_rate_box.configure(state="disabled")
        self.bit_depth_box.configure(state="disabled")

    def _refresh_agent_state(self, force: bool = False) -> None:
        now = time.monotonic()
        if not force and now < self.next_agent_poll:
            return
        self.next_agent_poll = now + 0.5

        daily_state = self._load_daily_state_for_ui()
        daily_running = (
            process_is_running(
                daily_state.host_process_id, "v1_presence_agent.exe"
            )
            if daily_state else False
        )
        should_use_daily = daily_running and self.process is None
        if should_use_daily:
            if not self.daily_mode:
                self.daily_mode = True
                self.daily_generation = None
                self.agent_mode = False
                self.agent_generation = None
                self.chart.clear()
                self._append_log(
                    "Detected the current V1 daily host in read-only mode."
                )
            if daily_state is not None:
                if daily_state.acl_generation != self.daily_generation:
                    self.daily_generation = daily_state.acl_generation
                    self.chart.clear()
                self._render_daily_state(daily_state)
            return
        if self.daily_mode:
            self.daily_mode = False
            self.daily_generation = None
            self.bitrate_caption_var.set("kbps  LDAC")
            self.quality_box.configure(values=list(QUALITY_OPTIONS))
            self._append_log("V1 daily host is no longer running.")

        state = load_agent_state_for_ui(self.agent_state_path)
        running = process_is_running(state.agent_pid) if state else False

        should_use_agent = running and self.process is None
        if should_use_agent and not self.agent_mode:
            self.agent_mode = True
            self.agent_generation = None
            self._reset_agent_probe_tail()
            self._append_log("Detected the installed Native LDAC agent.")
        elif not should_use_agent and self.agent_mode:
            self.agent_mode = False
            self.agent_generation = None
            self._append_log("Installed Native LDAC agent is no longer running.")

        if not self.agent_mode or state is None:
            self._set_agent_telemetry(None)
            self.bitrate_caption_var.set("kbps  LDAC")
            if self.process is None:
                self.start_button.configure(text="开始 LDAC", state="normal")
                self.stop_button.configure(text="安全停止", state="disabled")
                self.quality_box.configure(state="readonly")
                self.channel_mode_box.configure(state="readonly")
                self.sample_rate_box.configure(state="readonly")
                self.bit_depth_box.configure(state="readonly")
            return

        if state.generation != self.agent_generation:
            self.agent_generation = state.generation
            self.chart.clear()
            self._reset_agent_probe_tail()
        self._tail_agent_probe_log()
        self._set_agent_telemetry(state.telemetry)

        try:
            config = load_config(self.agent_config_path)
            self.quality_var.set(
                QUALITY_LABELS.get(config.quality, QUALITY_LABELS["hq"])
            )
            self.channel_mode_var.set(
                CHANNEL_MODE_LABELS.get(
                    config.channel_mode, CHANNEL_MODE_LABELS["stereo"]
                )
            )
            self.sample_rate_var.set(
                SAMPLE_RATE_LABELS.get(
                    config.sample_rate, SAMPLE_RATE_LABELS[48000]
                )
            )
            self.bit_depth_var.set(
                BIT_DEPTH_LABELS.get(
                    config.bits_per_sample, BIT_DEPTH_LABELS[16]
                )
            )
        except (OSError, ValueError):
            config = None

        enabled = state.config_enabled if config is None else config.enabled
        self.start_button.configure(
            text="启用 LDAC", state="disabled" if enabled else "normal"
        )
        self.stop_button.configure(
            text="暂停 LDAC", state="normal" if enabled else "disabled"
        )
        self.quality_box.configure(state="readonly")
        self.channel_mode_box.configure(state="readonly")
        self.sample_rate_box.configure(state="readonly")
        self.bit_depth_box.configure(state="readonly")

        status_map = {
            "starting": ("后台正在启动…", ACCENT),
            "waiting_device": ("等待 XM5 开机连接", MUTED),
            "waiting_transport": ("等待 LDAC 驱动就绪", MUTED),
            "waiting_recovery": ("等待安全恢复连接", MUTED),
            "settling_device": ("XM5 已上线，正在准备…", ACCENT),
            "recovering_transport": ("正在复位异常会话…", ACCENT),
            "connecting": ("正在建立 LDAC…", ACCENT),
            "probe_running": ("后台 LDAC 运行中", GREEN),
            "retry_wait": ("连接失败，正在冷却…", ACCENT),
            "reconfiguring": ("正在安全应用设置…", ACCENT),
            "disabled": ("后台 LDAC 已暂停", MUTED),
            "config_error": ("后台配置无效", RED),
            "stopping": ("后台正在安全停止…", ACCENT),
            "stopped": ("后台 agent 已停止", MUTED),
        }
        text, color = status_map.get(state.state, (state.state, MUTED))
        self._set_status(text, color)

    def start(self) -> None:
        self._refresh_agent_state(force=True)
        if self.daily_mode:
            return
        if self.agent_mode:
            if self._write_agent_config(enabled=True):
                self.start_button.configure(state="disabled")
                self.stop_button.configure(state="normal")
                self._set_status("正在启用后台 LDAC…", ACCENT)
            return
        if self.process is not None:
            return
        if not self.probe_path.exists():
            messagebox.showerror(
                APP_TITLE,
                f"未找到传输程序：\n{self.probe_path}\n\n请先运行 build-agent.ps1。",
            )
            return
        try:
            self.stop_event = NamedStopEvent()
            quality = QUALITY_OPTIONS[self.quality_var.get()]
            channel_mode = CHANNEL_MODE_OPTIONS[self.channel_mode_var.get()]
            sample_rate = SAMPLE_RATE_OPTIONS[self.sample_rate_var.get()]
            bits_per_sample = BIT_DEPTH_OPTIONS[self.bit_depth_var.get()]
            self._append_log(
                f"=== Start {datetime.now().isoformat(timespec='seconds')}, "
                f"quality {quality} ==="
            )
            command = [
                str(self.probe_path),
                "--play-endpoint",
                "--quality",
                quality,
                "--channel-mode",
                channel_mode,
                "--sample-rate",
                str(sample_rate),
                "--bits",
                str(bits_per_sample),
                "--stop-event",
                self.stop_event.name,
            ]
            self.process = subprocess.Popen(
                command,
                cwd=self.repo_root,
                stdout=subprocess.PIPE,
                stderr=subprocess.STDOUT,
                text=True,
                encoding=locale.getpreferredencoding(False),
                errors="replace",
                bufsize=1,
                creationflags=subprocess.CREATE_NO_WINDOW,
            )
        except Exception as error:
            if self.stop_event is not None:
                self.stop_event.close()
            self.stop_event = None
            self.process = None
            messagebox.showerror(APP_TITLE, f"无法启动 LDAC：\n{error}")
            return

        self.stop_requested = False
        self.chart.clear()
        self.ldac_var.set("—")
        self.transport_var.set("—")
        self.write_var.set("—")
        self.packet_var.set("—")
        self.actual_quality_var.set("—")
        self.endpoint_state_var.set("等待连接")
        self.endpoint_buffer_var.set("—")
        self.endpoint_volume_var.set("—")
        self.endpoint_health_var.set("—")
        self._set_status("正在连接…", ACCENT)
        self.start_button.configure(state="disabled")
        self.stop_button.configure(state="normal")
        self.quality_box.configure(state="disabled")
        self.channel_mode_box.configure(state="disabled")
        self.sample_rate_box.configure(state="disabled")
        self.bit_depth_box.configure(state="disabled")
        thread = threading.Thread(target=self._read_process, daemon=True)
        thread.start()

    def _read_process(self) -> None:
        process = self.process
        if process is None or process.stdout is None:
            return
        try:
            for line in process.stdout:
                self.events.put(("line", line.rstrip("\r\n")))
        finally:
            process.stdout.close()
            self.events.put(("exit", process.wait()))

    def stop(self) -> None:
        self._refresh_agent_state(force=True)
        if self.daily_mode:
            return
        if self.agent_mode:
            if self._write_agent_config(enabled=False):
                self.start_button.configure(state="normal")
                self.stop_button.configure(state="disabled")
                self._set_status("正在安全暂停后台 LDAC…", ACCENT)
            return
        if self.process is None or self.stop_requested:
            return
        self.stop_requested = True
        self._set_status("正在安全停止…", ACCENT)
        self.stop_button.configure(state="disabled")
        try:
            if self.stop_event is not None:
                self.stop_event.set()
        except Exception as error:
            messagebox.showerror(APP_TITLE, f"无法发送停止请求：\n{error}")

    def _poll_events(self) -> None:
        try:
            while True:
                kind, payload = self.events.get_nowait()
                if kind == "line":
                    self._handle_line(str(payload))
                elif kind == "exit":
                    self._handle_exit(int(payload))
        except queue.Empty:
            pass
        self._refresh_agent_state()
        if not self.closing or self.process is not None:
            self.root.after(50, self._poll_events)

    def _handle_line(self, line: str) -> None:
        self._append_log(line)
        event = parse_probe_line(line)
        if event is None:
            return
        kind, payload = event
        if kind == "live" and isinstance(payload, LiveMetrics):
            self.ldac_var.set(f"{payload.ldac_kbps:.0f}")
            self.transport_var.set(f"{payload.transport_kbps:.0f} kbps")
            self.actual_quality_var.set(payload.quality)
            self.write_var.set(f"{payload.average_write_ms:.2f} / {payload.max_write_ms:.2f} ms")
            self.packet_var.set(f"{payload.packets} packets")
            self.chart.add(payload.ldac_kbps, payload.transport_kbps)
            self._set_status("已连接", GREEN)
        elif kind == "source" and isinstance(payload, WasapiSource):
            self.source_var.set(
                f"{payload.name} · {payload.sample_rate_hz / 1000:g} kHz · "
                f"{payload.channels} ch · {payload.bits_per_sample}-bit"
            )
        elif kind == "endpoint_source" and isinstance(
            payload, NativeEndpointSource
        ):
            self.source_var.set(
                "输出设备：Native LDAC - WH-1000XM5 · "
                f"{payload.sample_rate_hz / 1000:g} kHz · "
                f"{payload.channels} ch · {payload.bits_per_sample}-bit"
            )
        elif kind == "endpoint" and isinstance(payload, EndpointMetrics):
            state = "正在播放" if payload.active else "等待系统音频"
            self.endpoint_state_var.set(f"{state} · epoch {payload.epoch}")
            self.endpoint_buffer_var.set(
                f"{payload.available_bytes:,} / {payload.capacity_bytes:,} B"
            )
            if payload.volume_control_available and payload.volume_percent is not None:
                suffix = " · 静音" if payload.muted else ""
                self.endpoint_volume_var.set(
                    f"{payload.volume_percent:.0f}%{suffix}"
                )
            else:
                self.endpoint_volume_var.set("不可用")
            self.endpoint_health_var.set(
                f"{payload.dropped_bytes:,} B / "
                f"{payload.silence_fill_frames:,} 帧"
            )
            if payload.active and payload.dropped_bytes > 0:
                self._set_status("已连接 · 播放中有丢弃", ACCENT)
            elif payload.active:
                self._set_status("已连接 · 正在播放", GREEN)
            else:
                self._set_status("已连接 · 等待系统音频", GREEN)
        elif kind == "address":
            address = str(payload)
            self.address_var.set(address)
            self.device_var.set(f"Sony WH-1000XM5  ·  {address}")
        elif kind == "abr" and isinstance(payload, AbrChange):
            self.actual_quality_var.set(payload.quality)
        elif kind == "status":
            text = str(payload)
            color = GREEN if text == "已连接" else ACCENT if "正在" in text else MUTED
            self._set_status(text, color)
        elif kind == "error":
            self._set_status("发生错误", RED)

    def _handle_exit(self, code: int) -> None:
        requested = self.stop_requested
        self._append_log(
            f"=== Exit {datetime.now().isoformat(timespec='seconds')}, "
            f"code {code} ==="
        )
        self.process = None
        if self.stop_event is not None:
            self.stop_event.close()
        self.stop_event = None
        self.stop_requested = False
        self.start_button.configure(state="normal")
        self.stop_button.configure(state="disabled")
        self.quality_box.configure(state="readonly")
        self.channel_mode_box.configure(state="readonly")
        self.sample_rate_box.configure(state="readonly")
        self.bit_depth_box.configure(state="readonly")
        if requested and code in (0, 130):
            self._set_status("已安全停止", MUTED)
        elif code == 0:
            self._set_status("已结束", MUTED)
        else:
            self._set_status(f"异常退出（{code}）", RED)
        if self.closing:
            self.root.destroy()

    def _append_log(self, line: str) -> None:
        if self.log_path is not None:
            try:
                with self.log_path.open("a", encoding="utf-8", newline="\n") as log_file:
                    log_file.write(line + "\n")
            except OSError:
                self.log_path = None
        self.log.configure(state="normal")
        self.log.insert("end", line + "\n")
        line_count = int(self.log.index("end-1c").split(".")[0])
        if line_count > 220:
            self.log.delete("1.0", f"{line_count - 200}.0")
        self.log.see("end")
        self.log.configure(state="disabled")

    def _on_close(self) -> None:
        if self.daily_mode or self.agent_mode:
            self.root.destroy()
            return
        if self.process is None:
            self.root.destroy()
            return
        self.closing = True
        self.stop()


def main() -> None:
    if hasattr(ctypes, "windll"):
        try:
            ctypes.windll.shcore.SetProcessDpiAwareness(2)
        except (AttributeError, OSError):
            pass
    root = tk.Tk()
    LdacControlApp(root)
    root.mainloop()


if __name__ == "__main__":
    main()
