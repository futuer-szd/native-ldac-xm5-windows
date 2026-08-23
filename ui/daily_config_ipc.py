"""Bounded client for the current V1 daily quality configuration pipe."""

from __future__ import annotations

import ctypes
from ctypes import wintypes
from dataclasses import dataclass
import re
import struct


MAGIC = 0x31434C4E
VERSION = 1
REQUEST_TYPE = 1
RESPONSE_TYPE = 2
REQUEST = struct.Struct("<IHHIQII")
RESPONSE = struct.Struct("<IHHIIQQII")
QUALITY_CODES = {"hq": 0, "sq": 1, "mq": 2}
STATUS_NAMES = {
    0: "accepted",
    1: "rejected",
    2: "invalid",
    3: "stale_revision",
    4: "error",
}
PIPE_NAME_PATTERN = re.compile(
    r"^NativeLdac\.V1\.Config\.[A-Za-z0-9._-]{1,64}$"
)


@dataclass(frozen=True)
class DailyConfigResponse:
    status: str
    requested_revision: int
    applied_revision: int
    error: int


def request_quality(
    pipe_name: str,
    quality: str,
    revision: int,
    timeout_ms: int = 2000,
) -> DailyConfigResponse:
    if PIPE_NAME_PATTERN.fullmatch(pipe_name) is None:
        raise ValueError("daily config pipe is invalid")
    if quality not in QUALITY_CODES or revision <= 0:
        raise ValueError("daily quality request is invalid")
    if not hasattr(ctypes, "WinDLL"):
        raise OSError("daily configuration requires Windows")
    request = REQUEST.pack(
        MAGIC,
        VERSION,
        REQUEST_TYPE,
        REQUEST.size,
        revision,
        QUALITY_CODES[quality],
        0,
    )
    response = ctypes.create_string_buffer(RESPONSE.size)
    read = wintypes.DWORD()
    kernel32 = ctypes.WinDLL("kernel32", use_last_error=True)
    kernel32.CallNamedPipeW.argtypes = (
        wintypes.LPCWSTR,
        wintypes.LPVOID,
        wintypes.DWORD,
        wintypes.LPVOID,
        wintypes.DWORD,
        ctypes.POINTER(wintypes.DWORD),
        wintypes.DWORD,
    )
    kernel32.CallNamedPipeW.restype = wintypes.BOOL
    path = rf"\\.\pipe\{pipe_name}"
    if not kernel32.CallNamedPipeW(
        path,
        request,
        len(request),
        response,
        RESPONSE.size,
        ctypes.byref(read),
        timeout_ms,
    ):
        raise ctypes.WinError(ctypes.get_last_error())
    if read.value != RESPONSE.size:
        raise OSError("daily config response size is invalid")
    (
        magic,
        version,
        message_type,
        message_bytes,
        status,
        requested_revision,
        applied_revision,
        error,
        reserved,
    ) = RESPONSE.unpack(response.raw)
    if (
        magic != MAGIC
        or version != VERSION
        or message_type != RESPONSE_TYPE
        or message_bytes != RESPONSE.size
        or status not in STATUS_NAMES
        or reserved != 0
    ):
        raise OSError("daily config response is invalid")
    return DailyConfigResponse(
        status=STATUS_NAMES[status],
        requested_revision=requested_revision,
        applied_revision=applied_revision,
        error=error,
    )
