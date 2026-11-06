# Broadcast relay

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
server binary, this README, project license, Cargo.lock and source-commit/target
metadata. Build/test logs are uploaded even on failure. No Qt, JACK, receiver or
audio-device dependencies are installed. Linux packages use Ubuntu 24.04's
native GNU/glibc environment; they are not static musl packages. Platform build
results are established by the corresponding workflow run, not by local YAML
validation.

GitHub requires a manually dispatched workflow to exist on the repository's
default branch before it can be run from the Actions UI. Once merged there,
select the desired source branch in **Run workflow**.

## Run

Put a strong publishing token in a protected UTF-8 file. Never put the token in
URLs or command-line arguments. Configure your normal TLS certificate and key:

```powershell
& build/bc-target/release/bc-server.exe --bind 0.0.0.0:9443 --udp-bind 0.0.0.0:9444 --publish-token-file C:/Secure/bc.token --tls-cert C:/Secure/server.pem --tls-key C:/Secure/server-key.pem
```

Allow TCP 9443 and inbound UDP 9444 on the server firewall. The publisher opens
its own outgoing control and UDP connections. HTTP listeners need only TCP.
There is no UDP destination specified by anonymous HTTP listeners.

For **explicit local development only**, use both loopback binds and
`--insecure-loopback`. Cleartext non-loopback binds are rejected. The one JSON
startup line reports the actual bound addresses (including ephemeral port 0).
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
