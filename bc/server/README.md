# Remote audio server — current broadcast relay

This is the independent server for WinJACKNexus remote audio features. Broadcast
publishing and relay are its first implemented capabilities; other remote audio
functions are planned for future extensions and are not yet available. The
current executable/package name remains `bc-server`.

Independent Rust executable. No WinJACKNexus, Qt, JACK, audio device, decoder or
encoder is required on the relay host. The relay accepts authenticated UDP media
and serves anonymous standard WAV / Ogg Opus over HTTP(S).

## Build

Use the repository's existing build tree and Release configuration. From the
repository root, execute in foreground PowerShell:

```powershell
& cargo build --manifest-path bc/Cargo.toml --package bc-server --release --locked --target-dir build/bc-target 2>&1 | Tee-Object -FilePath build.log
$buildExitCode = $LASTEXITCODE
if ($buildExitCode -ne 0) { Get-Content build.log; exit $buildExitCode }
```

The existing CMake tree also exposes the explicit `wjn_bc_server` target. It is
not part of the default C++ target set. Output: `build/bc-target/release/bc-server.exe`
on Windows. The crate is portable Rust; non-Windows builds have not been verified.

## GitHub Actions

Run **Build standalone broadcast server** from the Actions tab
(`.github/workflows/broadcast-server.yml`, manual `workflow_dispatch`). It builds
and tests only `bc-server`, independently of the desktop Release workflow:

| Platform | Rust target | Download |
| --- | --- | --- |
| Windows x64 | `x86_64-pc-windows-msvc` | `bc-server-windows-x64.zip` |
| Linux x64 | `x86_64-unknown-linux-gnu` | `bc-server-linux-x64.tar.gz` |
| Linux ARM64 | `aarch64-unknown-linux-gnu` | `bc-server-linux-arm64.tar.gz` |

Each native runner uploads an archive and its `.sha256` file, including the
server binary, this README, example JSON/NGINX configurations, project license,
Cargo.lock and source-commit/target
metadata. Build/test logs are uploaded even on failure. No Qt, JACK, receiver or
audio-device dependencies are installed. Linux packages use Ubuntu 24.04's
native GNU/glibc environment; they are not static musl packages. Platform build
results are established by the corresponding workflow run, not by local YAML
validation.

GitHub requires a manually dispatched workflow to exist on the repository's
default branch before it can be run from the Actions UI. Once merged there,
select the desired source branch in **Run workflow**.

## Run

Copy `server.example.json` to `server.json`, put a strong publishing token in
`publish.token` beside it, then run:

```powershell
& ./bc-server.exe --config ./server.json
```

On Linux use `./bc-server --config ./server.json`. No Python, Qt or JACK is
needed on the server. Never put the token in URLs or command-line arguments.

All existing startup settings can be supplied in JSON:

```json
{
  "bind": "127.0.0.1:9443",
  "udp_bind": "0.0.0.0:9444",
  "publish_token_file": "publish.token",
  "tls_cert": null,
  "tls_key": null,
  "insecure_loopback": false
}
```

File paths in JSON are relative to the configuration file's directory, so the
working directory does not affect certificate/token discovery. Explicit CLI
options override JSON regardless of their order; CLI file paths remain relative
to the working directory. Unknown JSON fields, duplicate CLI options and missing
option values are errors. Unspecified addresses retain the CLI defaults
`0.0.0.0:9443` and `0.0.0.0:9444`; `publish_token_file` remains required.

TLS is optional. Omit both certificate fields (or use null) for HTTP/WS behind
NGINX. Configure both `tls_cert` and `tls_key` for direct HTTPS/WSS; specifying
only one is an error, and invalid certificate files do not fall back to HTTP.
For direct TLS the existing command line still works:

```powershell
& build/bc-target/release/bc-server.exe --bind 0.0.0.0:9443 --udp-bind 0.0.0.0:9444 --publish-token-file C:/Secure/bc.token --tls-cert C:/Secure/server.pem --tls-key C:/Secure/server-key.pem
```

The packaged `nginx.example.conf` terminates HTTPS/WSS at NGINX and forwards
HTTP/WS to `127.0.0.1:9443`, preserving `/bc/`, WebSocket Upgrade and streaming
responses without proxy buffering. Adapt its domain and NGINX certificate paths
inside your existing NGINX http configuration. The publisher and listeners use
the public NGINX HTTPS address, e.g. `https://stream.example.com/bc`.
References: [NGINX WebSocket proxying](https://nginx.org/en/docs/http/websocket.html)
and [proxy buffering](https://nginx.org/en/docs/http/ngx_http_proxy_module.html#proxy_buffering).

UDP 9444 must independently reach the relay on the same public host used for
control. The HTTP reverse proxy does not carry UDP. If NGINX is on another host,
separately forward that UDP port to the relay. With same-host NGINX, expose TCP
443 and UDP 9444; the example keeps backend TCP 9443 on loopback. For direct TLS,
expose TCP 9443 and UDP 9444. UDP payloads use session/HMAC authentication and
integrity checks but are not TLS-encrypted. HTTP listeners need only TCP and do
not select an arbitrary UDP destination.

The legacy `--insecure-loopback` development flag still requires both binds to
be loopback. It also accepts explicit `true`/`false` to override a JSON flag.
Ordinary certificate-free HTTP/WS mode does not require this development flag.
The one JSON startup line reports the actual bound addresses (including port 0).
Credentials/session keys are never included in that line.

## Listen

Open the same URL in a browser or a standard compatible HTTP audio player:

```text
https://host:9443/bc/listen?publisher=station&stream=news&channel=all&format=opus
https://host:9443/bc/listen?publisher=station&stream=news&channel=L&format=wav
```

No listener token. The publisher must register and supply both formats and its
encoded Opus speaker variants. `format` defaults to opus; `channel` defaults to
all. Invalid/duplicate query options are errors. Successful responses contain
`audio/wav` or `audio/ogg; codecs=opus`, not JSON or private frames. They are live
streams: no fixed HTTP length, retained recording or seek support.

Full layouts preserve channels; the playback device/browser may downmix at its
output. Multichannel WAV uses WAVEFORMATEXTENSIBLE. Ogg headers are supplied per
listener; no audio decoding/transcoding happens on the relay.

The current S1 evidence is real UDP/HTTP byte transport, Ogg CRC/timestamp tests,
four-layout routing, control/TLS and slow-listener isolation. Actual browser
native-control/live playback checks belong to S2/S5 and are not yet claimed PASS.

## Verification

Run the public-process tests in foreground PowerShell through Tee-Object and
check the exit code as above. Existing Python:
`D:/ProgramData/miniconda3/python.exe Develop/Broadcast/RelayTests.py`.
Test-only packages: websockets (sync API), cryptography (temporary local TLS
certificate). The test certificate is not installed in a machine trust store.
FFmpeg was used to generate the checked-in deterministic tone fixtures; it is
not a relay runtime dependency. Fixture metadata records the generator settings.

The tests start and stop only their own relay processes. They require no JACK
server and never stop running WinJACKNexus instances.

Protocol: [Protocol-v1.md](../../Docs/Broadcast/Protocol-v1.md).
