# Broadcast protocol v1 — S0 frozen contract

2026-11-03. The sender is a JACK-only Node v4 module. The Rust relay is independent of JACK/Qt/WinJACKNexus. 本文冻结协议，不表示网络或浏览器功能已经验证。

## Interfaces and trust boundary

- Shared base URL: `https://host:9443/bc`. One process shares one publisher control connection and UDP socket across node instances.
- Publishing control: WebSocket Upgrade at `/bc/publish?publisher=<id>`, with `Authorization: Bearer <token>` validated **before** upgrade. WSS in production; WS/HTTP allowed only with explicit loopback development mode. No continuous audio on this connection.
- Audio publishing: UDP at the negotiated host/port. Default bind port 9444. Long-lived Bearer tokens are never in UDP packets.
- Anonymous listening: `GET /bc/listen?publisher=<id>&stream=<id>&channel=all&format=opus`. No cookies, login, JS, separate subscription call, WebSocket or UDP listener required.
- `channel` defaults to `all`; individual values are M/L/R/C/LFE/Ls/Rs/Lrb/Rrb, case-sensitive and valid only for the stream layout. `format=wav` and `format=opus` are required capabilities; default opus. Reject missing publisher/stream, duplicate, unknown or invalid query parameters (400). IDs are 1–64 ASCII letters/digits/hyphens/underscores; GUI uses UUIDs.
- Unknown stream 404; retained offline stream 503; invalid token 401 with WWW-Authenticate; occupied publisher identity 409; capacity exceeded 429. These precede Upgrade/200. JSON machine-code diagnostics are permitted only for errors, never as a successful audio body.
- Source IP is not identity. All valid publishers possessing the configured static token share the right to publish under an unoccupied publisher namespace. No account/tenant isolation is claimed. Public listeners may hear any known stream address by design.

## Control messages

Server immediately sends `{"type":"hello","version":1,"session":"<32 hex>","key":"<64 hex>","udpPort":9444,"maxDatagram":1200}`. Session ID is 128 random bits; key is 256 random bits. Server binds UDP to the configured interface, client uses the control host and returned UDP port. Key/session never appear in logs or project files.

Publisher sends `register` with requestId (u32), streamId, handle (nonzero u32 unique per session), epoch (positive u32), displayName, category (array of display strings), layout (`mono`,`stereo`,`5.1`,`7.1`), pcmRate (JACK integer sample rate, 8000–192000), opusHeads (object keyed by `all` and speaker role, base64 standard OpusHead packets). Server validates every header channel count, mapping and pre-skip before accepting. PCM and Opus are both available; do not advertise a format that is not being supplied.

Ack: `{"type":"ack","requestId":1}`; failure: `{"type":"error","requestId":1,"code":"invalid_registration"}`. Display updates may use register with the same handle, stream and epoch without resetting media. Layout/rate/encoder reset requires an increasing epoch and new headers; close existing HTTP responses and let clients reconnect to the same URL. `unregister` includes handle and requestId; removes the stream. Unknown request types are errors, not publishing rights for listeners.

One current control session owns a publisher namespace. Disconnect invalidates UDP keys, stops media, closes that session's listeners and retains bounded offline stream records for 404/503 distinction (up to 4096 total records; expire offline records after 10 minutes). Cleanup checks session ID so an old task cannot remove a newly connected publisher. Ping/Pong every 10 seconds, close on 30 seconds without control activity. Normal reconnect backoff 1/2/4/8/16/30 seconds; 401 stops retry until credentials change. No automatic publish after application startup: user starts the global publisher.

## UDP wire format

All numeric header fields are little-endian. Total application datagram size at most **1200** bytes; fixed header **64** bytes, payload up to **1104** bytes, final **32** bytes HMAC-SHA256 over header+payload using this session key. Tag comparison is constant-time. Media is public audio, authenticated but not encrypted by this format; control TLS does not encrypt UDP. This does not promise confidential broadcasting.

| Offset | Bytes | Field |
| ---: | ---: | --- |
| 0 | 4 | ASCII WJBC |
| 4 | 1 | version 1 |
| 5 | 1 | codec: 1 PCM signed16 LE interleaved; 2 Opus packet |
| 6 | 1 | variant: 0 full layout, 1..8 canonical channel index+1 |
| 7 | 1 | flags, zero |
| 8 | 16 | binary session ID |
| 24 | 4 | handle |
| 28 | 4 | format epoch |
| 32 | 8 | per-session monotonically increasing datagram sequence |
| 40 | 8 | first sample position on the codec clock |
| 48 | 2 | samples per channel in this complete audio block |
| 50 | 2 | zero-based fragment index |
| 52 | 2 | fragment count (1..32) |
| 54 | 2 | reserved, zero |
| 56 | 4 | this fragment payload bytes |
| 60 | 4 | reserved, zero |

Fragment complete block bytes consecutively in chunks of 1104, last may be smaller. Never split by IP fragmentation. Reassembly key is session/handle/epoch/codec/variant/position; fragments must agree on duration/count and a slot cannot change bytes. Each fragment has a distinct datagram sequence. Count and total block bytes are bounded; a 7.1 PCM 48 kHz 20 ms block is 15360 bytes, requiring 14 datagrams. PCM variant must be 0; server derives mono listening from PCM channel bytes. Opus variants include all and each speaker; Mono all/M may alias the same encoded packets and headers.

Authenticate before allocating assembly state. Reject unknown handle, stale epoch, invalid count/length, payload above cap, altered tags and replay. Maintain a 16384-sequence sliding anti-replay bitmap per session; accepts out-of-order within window once, rejects duplicates and older sequences. Do not enforce strictly ordered reception.

Per stream/codec/variant assemblies bounded to 100 ms and 8 in-flight blocks. Reorder on sample position; 40 ms target playout delay, hard 100 ms age cap. Missing fragment makes that whole encoded block unavailable; discard incomplete/expired blocks. For PCM insert at most 100 ms of zero samples for a missing interval; longer discontinuity closes affected HTTP responses and restarts at live position. For Opus never zero compressed bytes or create synthetic encoded silence: omit the lost packet and retain its sample interval in Ogg granule positions per RFC 7845 section 4.1. Decoder concealment occurs in the receiving decoder. Complete surviving Opus packets never change.

Send at media cadence, not bursts of stale queued data. JACK callback only copies into preallocated bounded storage; network thread discards blocks older than 100 ms. One slow HTTP listener cannot block UDP reception or other streams. Each HTTP response has at most 100 ms of queued audio; overflow or a 2-second stalled body consumer closes that response. Track UDP replay/late/loss separately from local queue drops. Persistent congestion produces a visible overload and stops the affected publishing session rather than unlimited queueing/sending. No retransmission, FEC, multicast or NAT relay service in v1. This bounded deployment policy follows [UDP usage guidelines](https://www.rfc-editor.org/info/rfc8085/); it is not a congestion-controlled arbitrary-throughput transport.

## Audio and channel identities

Canonical JACK/PCM order is M; L,R; L,R,C,LFE,Ls,Rs; L,R,C,LFE,Ls,Rs,Lrb,Rrb. Source clock is JACK. PCM signed16 little-endian conversion happens only at sender, with finite float clipping to [-1,32767/32768], scale 32768 and nearest-integer quantization; all channels use the same gain before conversion. Mono/full PCM is not separately uploaded per speaker.

Opus uses 48 kHz, 20 ms (960 samples), application audio, nominal mono 96 kbps / stereo 160 kbps / 5.1 384 kbps / 7.1 512 kbps, plus 96 kbps for each mono variant. Network encoding/re-sampling is off the JACK thread. 48 kHz input bypasses resampling; other supported JACK clocks require a stateful bandlimited sender resampler, with unchanged local monitor clock. Do not silently substitute a linear interpolator for audio-quality resampling. A worker failure must leave JACK/local monitoring alive and display a publishing error.

Use libopus multistream surround mapping family 1 for 5.1/7.1, family 0 for 1/2 channels. Opus/Vorbis input order differs from JACK: 5.1 is L,C,R,Ls,Rs,LFE; 7.1 is L,C,R,Ls,Rs,Lrb,Rrb,LFE. Sender reorders explicitly and transfers OpusHead mapping/pre-skip through control. Individual speaker Opus encoders take the original corresponding channel, not a downmix. Server never splits coupled Opus multistream bytes into a fake mono stream.

## Standard HTTP live audio

Execution amendment, 2026-11-04: 用户要求不使用 Computer Use；浏览器控件、点击播放及界面观察验收已跳过（用户指令），不算 PASS、不阻塞阶段。保留标准 audio/* 与原生播放的产品契约；自动 HTTP/容器/解码/声道验证继续，不能冒称验证了浏览器界面。

**浏览器直接打开带参数 URL 必须出现原生音频播放控件，点击能够持续收听**。No application HTML, JS decoder, plugin or special client prerequisite. Autoplay without user gesture is not required.

| Request | Successful Content-Type | Body |
| --- | --- | --- |
| format=wav | audio/wav | Standard RIFF/WAVE PCM16, streaming data |
| format=opus | audio/ogg; codecs=opus | Standard Ogg Opus, no UDP/private headers |

Return 200, Cache-Control no-store, Content-Disposition inline, X-Content-Type-Options nosniff. Never use attachment or application/octet-stream/private MIME. Omit Content-Length for indefinite live response; HTTP/1.1 chunking is handled by the HTTP stack, HTTP/2/3 use their normal body framing. Do not send Transfer-Encoding chunked on HTTP/2. Byte range seeking is not a retained recording: ignore Range and start current live with 200/Accept-Ranges none. HEAD may expose headers without creating a media subscriber.

WAV: mono/stereo fmt tag 1, 44-byte header. 5.1/7.1 use 40-byte WAVEFORMATEXTENSIBLE fmt, valid bits 16, PCM GUID, channel masks 0x60f / 0x63f. Use 0xffffffff RIFF/data streaming length sentinels; no fake finite HTTP length. WAV byte order follows ascending speaker mask bits: 7.1 HTTP order L,R,C,LFE,Lrb,Rrb,Ls,Rs, so relay performs only byte selection/reordering. Single-speaker URL outputs one-channel WAV; preserve original speaker identity in metadata headers. Validate playback beyond short-file startup and independently exercise indefinite-size headers. Do not claim a short file proves multi-hour behavior or browser portability; incompatible clients can select Opus, but WAV is still a required, separately verified capability.

Ogg: relay sends each new listener its own BOS OpusHead/OpusTags and serial/sequence/CRC, then complete Opus packets with rebased 48 kHz granules. New listeners begin at current live packets, not old buffered history. Respect supplied pre-skip and decoder preroll; send bounded available preroll where needed rather than fabricating encoder state. Handle missing packets according to RFC 7845 section 4.1. Format change terminates response; no unverified chained-stream workaround. Ogg encapsulation and WAV header/byte routing are transport work, not audio transcoding.

Preserve original layout in full-stream responses. Browser downmix on a stereo output device is not evidence of physical 7.1 output. Browser matrix: current installed Windows Edge/Chrome/Firefox, record actual versions and availability; Safari 18.4+ is a cross-platform compatibility target, manual/pending when no Apple environment is available. Playback tests require real browser navigation, native controls, play/time advancement and pause/resume to live; general player check uses installed ffmpeg/ffprobe and a common HTTP player when available. Do not mark unavailable environments PASS. No custom playback webpage is a substitute.

## Node and UI behavior frozen for implementation

M affects broadcast and monitor. S only controls this instance's local monitor selection; it never mutes another broadcast. Monitor output for a track is `Listen && (!anySolo || solo)` applied after gain/M. Listen and S start off on new/restore/recreate, but survive closing/reopening GUI. Each stream retains same-layout individual JACK output ports, writes zeros while Listen is off, no downmix/global monitor bus. Audio meter uses maximum absolute sample across channels after gain/M, before Listen/S; it is peak dBFS, not LUFS.

Reuse existing shared DragTreeWidget for hierarchy display, disable its outward drag for this window, use shared ActionButton/ChoiceField to move categories/streams transactionally; no new tree control or private styling is necessary. Shared ConsoleKnob, FxLevelMeterWidget single vertical column, ToggleField and existing containers/themes supply controls. Refresh uses ApplicationSettings/bindRefreshTimer at global FPS.

Global persistent keys live under QSettings UserScope WinJACKNexus/Application, `broadcast/` subgroup: serverBaseUrl, publisherId and credentialTarget only. Token is stored with Windows Credential Manager (CredReadW/CredWriteW); project JSON never overrides global server or credentials. Module-local singleton owns the connection/refcount. Per-instance state schema v1 stores categories, stream UUIDs/stable port IDs, layout/gain/M and client name. Restore reserves IDs against current process live registry; collisions with another live node mean copy and allocate fresh stream IDs, same node restore retains IDs, destroyed/recreated instance releases/reserves original IDs. Remote namespace collision rejects publishing rather than silently stealing identity.

These seams are the plan-authorized RED/GREEN boundaries: protocol fixture/wire; actual standalone relay control+UDP+HTTP; CLI standard decoding; exported JACK-only DLL discovery/state/offline audio core; actual visible Windows GUI. Do not test private helpers instead of these public behaviors.

## Sources

- [Ogg Opus specification and multichannel mapping](https://www.rfc-editor.org/info/rfc7845/)
- [WAVEFORMATEXTENSIBLE speaker masks/order](https://learn.microsoft.com/en-us/windows/win32/api/mmreg/ns-mmreg-waveformatextensible)
- [Safari 18.4 Ogg support](https://webkit.org/blog/16574/webkit-features-in-safari-18-4/)
- [Bearer token header use](https://www.rfc-editor.org/info/rfc6750/)
- [Axum streaming response bodies](https://docs.rs/axum/latest/axum/body/struct.Body.html)

S0 establishes fixtures and fields only. Browser/socket implementation evidence belongs to S1/S2/S5; JACK hardware checks remain separate. Manual review/installation/listening acceptance: 已跳过（用户指令）, not PASS.
