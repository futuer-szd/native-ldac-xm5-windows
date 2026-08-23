# NativeLdacAvrcpObserver

This is an isolated, XM5-only AVRCP observe-sideband candidate. It binds only
to the Sony WH-1000XM5 `BTHENUM` service PDO for UUID `0x110E`. After the
physical ACL is connected under Microsoft AVRCP and a capability-only AVDTP
channel is holding the profile connection, it performs exactly one outbound
AVCTP control OPEN on PSM `0x0017`, registers for AVRCP absolute-volume
notification, and records supported PASS THROUGH commands. It never retries
the AVCTP OPEN.

The public device interface is read-only. It exposes version, status, and a
bounded event dequeue operation. It has no IOCTL for Core Audio volume writes,
media-key injection, player control, arbitrary AVRCP traffic, AVDTP, or audio
packets. The current `LdacNative` A2DP driver and `ROOT\MEDIA\0001` endpoint are
not referenced by this package.

The package is a candidate only. Building or staging it does not authorize
binding the live XM5 AVRCP PDO. The first binding/restart requires a separate,
explicitly confirmed gate with exact rollback evidence.
