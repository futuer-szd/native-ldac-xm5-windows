"""Parse the stable, line-oriented output from transport_probe.exe."""

from __future__ import annotations

from dataclasses import dataclass
import re
from typing import Any


@dataclass(frozen=True)
class LiveMetrics:
    ldac_kbps: float
    transport_kbps: float
    packets: int
    window_seconds: float
    quality: str
    average_write_ms: float
    max_write_ms: float
    max_pace_lag_ms: float
    capture_late_packets: int
    slow_write_packets: int
    elapsed_seconds: float


@dataclass(frozen=True)
class WasapiSource:
    name: str
    sample_rate_hz: int
    channels: int
    bits_per_sample: int


@dataclass(frozen=True)
class NativeEndpointSource:
    interface_path: str
    sample_rate_hz: int
    channels: int
    bits_per_sample: int


@dataclass(frozen=True)
class EndpointMetrics:
    epoch: int
    active: bool
    available_bytes: int
    capacity_bytes: int
    dropped_bytes: int
    silence_fill_frames: int
    volume_control_available: bool
    volume_percent: float | None
    muted: bool


@dataclass(frozen=True)
class AbrChange:
    previous_quality: str
    quality: str
    reason: str


_LIVE_RE = re.compile(
    r"^Live:\s+"
    r"(?P<ldac>[0-9.]+) kbps LDAC,\s+"
    r"(?P<transport>[0-9.]+) kbps with RTP,\s+"
    r"(?P<packets>\d+) packets/(?P<window>[0-9.]+) s,\s+"
    r"(?P<quality>HQ|SQ|MQ);\s+"
    r"write avg/max (?P<write_avg>[0-9.]+)/(?P<write_max>[0-9.]+) ms,\s+"
    r"(?:pace )?lag max (?P<lag>[0-9.]+) ms,\s+"
    r"(?:capture-late/slow-write|late/slow) "
    r"(?P<late>\d+)/(?P<slow>\d+),\s+"
    r"elapsed (?P<elapsed>[0-9.]+) s\."
)
_WASAPI_RE = re.compile(
    r"^WASAPI source: (?P<name>.+), (?P<rate>\d+) Hz, "
    r"(?P<channels>\d+) channel\(s\), (?P<bits>\d+)-bit mix format\."
)
_NATIVE_ENDPOINT_RE = re.compile(
    r"^Native endpoint source: (?P<path>.+), (?P<rate>\d+) Hz, "
    r"(?P<channels>\d+) channel\(s\), (?P<bits>\d+)-bit PCM\."
)
_ENDPOINT_METRICS_RE = re.compile(
    r"^Source: epoch (?P<epoch>\d+), (?P<state>active|idle), "
    r"buffer (?P<available>\d+)/(?P<capacity>\d+) bytes, "
    r"driver dropped (?P<dropped>\d+) bytes, "
    r"silence fill (?P<silence>\d+) frames, "
    r"(?:volume (?P<volume>[0-9.]+)%(?P<muted> \(muted\))?"
    r"|(?P<unavailable>volume control unavailable))\."
)
_ABR_RE = re.compile(
    r"^ABR: (?P<previous>HQ|SQ|MQ) -> (?P<quality>HQ|SQ|MQ) "
    r"\((?P<reason>[^)]+)\)\."
)
_ADDRESS_RE = re.compile(r"^Remote Bluetooth address:\s+(?P<address>[0-9A-F]+)$")


def parse_probe_line(line: str) -> tuple[str, Any] | None:
    """Return an event name and payload for a recognized probe output line."""

    text = line.strip()
    match = _LIVE_RE.match(text)
    if match:
        values = match.groupdict()
        return (
            "live",
            LiveMetrics(
                ldac_kbps=float(values["ldac"]),
                transport_kbps=float(values["transport"]),
                packets=int(values["packets"]),
                window_seconds=float(values["window"]),
                quality=values["quality"],
                average_write_ms=float(values["write_avg"]),
                max_write_ms=float(values["write_max"]),
                max_pace_lag_ms=float(values["lag"]),
                capture_late_packets=int(values["late"]),
                slow_write_packets=int(values["slow"]),
                elapsed_seconds=float(values["elapsed"]),
            ),
        )

    match = _WASAPI_RE.match(text)
    if match:
        values = match.groupdict()
        return (
            "source",
            WasapiSource(
                name=values["name"],
                sample_rate_hz=int(values["rate"]),
                channels=int(values["channels"]),
                bits_per_sample=int(values["bits"]),
            ),
        )

    match = _NATIVE_ENDPOINT_RE.match(text)
    if match:
        values = match.groupdict()
        return (
            "endpoint_source",
            NativeEndpointSource(
                interface_path=values["path"],
                sample_rate_hz=int(values["rate"]),
                channels=int(values["channels"]),
                bits_per_sample=int(values["bits"]),
            ),
        )

    match = _ENDPOINT_METRICS_RE.match(text)
    if match:
        values = match.groupdict()
        volume = values["volume"]
        return (
            "endpoint",
            EndpointMetrics(
                epoch=int(values["epoch"]),
                active=values["state"] == "active",
                available_bytes=int(values["available"]),
                capacity_bytes=int(values["capacity"]),
                dropped_bytes=int(values["dropped"]),
                silence_fill_frames=int(values["silence"]),
                volume_control_available=values["unavailable"] is None,
                volume_percent=float(volume) if volume is not None else None,
                muted=values["muted"] is not None,
            ),
        )

    match = _ABR_RE.match(text)
    if match:
        values = match.groupdict()
        return (
            "abr",
            AbrChange(
                previous_quality=values["previous"],
                quality=values["quality"],
                reason=values["reason"],
            ),
        )

    match = _ADDRESS_RE.match(text)
    if match:
        return "address", match.group("address")
    if text.startswith("Signaling connected:"):
        return "status", "正在协商 LDAC"
    if text.startswith("XM5 accepted START"):
        return "status", "已连接"
    if text.startswith("Gracefully stopping"):
        return "status", "正在安全停止"
    if text.startswith("XM5 accepted CLOSE"):
        return "status", "已安全断开"
    if text.startswith("Signaling channel closed"):
        return "status", "已停止"
    if " failed " in f" {text.lower()} " or text.lower().startswith("failed"):
        return "error", text
    return None
