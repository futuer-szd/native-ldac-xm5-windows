from __future__ import annotations

from pathlib import Path
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from probe_protocol import (
    AbrChange,
    EndpointMetrics,
    LiveMetrics,
    NativeEndpointSource,
    WasapiSource,
    parse_probe_line,
)


class ProbeProtocolTests(unittest.TestCase):
    def test_live_metrics(self) -> None:
        event = parse_probe_line(
            "Live: 987.3 kbps LDAC, 1006.7 kbps with RTP, "
            "188 packets/1.01 s, HQ; write avg/max 1.80/9.26 ms, "
            "pace lag max 13.96 ms, capture-late/slow-write 122/9, "
            "elapsed 1.0 s."
        )
        self.assertIsNotNone(event)
        kind, payload = event
        self.assertEqual(kind, "live")
        self.assertIsInstance(payload, LiveMetrics)
        self.assertEqual(payload.quality, "HQ")
        self.assertAlmostEqual(payload.ldac_kbps, 987.3)
        self.assertEqual(payload.slow_write_packets, 9)

    def test_wasapi_source(self) -> None:
        event = parse_probe_line(
            "WASAPI source: 1 - R27Q71 (AMD High Definition Audio Device), "
            "48000 Hz, 2 channel(s), 32-bit mix format."
        )
        self.assertIsNotNone(event)
        kind, payload = event
        self.assertEqual(kind, "source")
        self.assertIsInstance(payload, WasapiSource)
        self.assertEqual(payload.sample_rate_hz, 48000)
        self.assertEqual(payload.channels, 2)

    def test_native_endpoint_source(self) -> None:
        event = parse_probe_line(
            r"Native endpoint source: \\?\root#media#0001#wavespeaker, "
            "48000 Hz, 2 channel(s), 16-bit PCM."
        )
        self.assertIsNotNone(event)
        kind, payload = event
        self.assertEqual(kind, "endpoint_source")
        self.assertIsInstance(payload, NativeEndpointSource)
        self.assertEqual(payload.sample_rate_hz, 48000)
        self.assertEqual(payload.bits_per_sample, 16)

    def test_active_endpoint_metrics(self) -> None:
        event = parse_probe_line(
            "Source: epoch 5, active, buffer 0/48000 bytes, "
            "driver dropped 0 bytes, silence fill 0 frames, volume 30%."
        )
        self.assertIsNotNone(event)
        kind, payload = event
        self.assertEqual(kind, "endpoint")
        self.assertIsInstance(payload, EndpointMetrics)
        self.assertTrue(payload.active)
        self.assertEqual(payload.dropped_bytes, 0)
        self.assertEqual(payload.volume_percent, 30.0)
        self.assertFalse(payload.muted)

    def test_idle_muted_endpoint_metrics(self) -> None:
        event = parse_probe_line(
            "Source: epoch 6, idle, buffer 0/48000 bytes, "
            "driver dropped 0 bytes, silence fill 48128 frames, "
            "volume 0% (muted)."
        )
        self.assertIsNotNone(event)
        kind, payload = event
        self.assertEqual(kind, "endpoint")
        self.assertIsInstance(payload, EndpointMetrics)
        self.assertFalse(payload.active)
        self.assertTrue(payload.muted)
        self.assertEqual(payload.silence_fill_frames, 48128)

    def test_abr_change(self) -> None:
        event = parse_probe_line("ABR: HQ -> SQ (transport congestion).")
        self.assertIsNotNone(event)
        kind, payload = event
        self.assertEqual(kind, "abr")
        self.assertIsInstance(payload, AbrChange)
        self.assertEqual(payload.previous_quality, "HQ")
        self.assertEqual(payload.quality, "SQ")

    def test_status_and_address(self) -> None:
        self.assertEqual(
            parse_probe_line("Remote Bluetooth address: 001122334455"),
            ("address", "001122334455"),
        )
        self.assertEqual(
            parse_probe_line("XM5 accepted START; the LDAC Media transport is ready."),
            ("status", "已连接"),
        )


if __name__ == "__main__":
    unittest.main()
