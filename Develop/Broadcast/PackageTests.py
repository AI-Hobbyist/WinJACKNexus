"""Check packaged Schannel deployment independently of the SDK plugin path."""

import json
import os
import shutil
import subprocess
import unittest
import zipfile

import RelayTests as relay


class PackageTests(unittest.TestCase):
    setUp = relay.RelayTests.setUp
    start_server = relay.RelayTests.start_server
    stop_server = relay.RelayTests.stop_server

    def test_overlay_schannel_rejects_untrusted_and_accepts_valid_certificate(self):
        self.stop_server()
        self.start_server(tls="ca-signed")
        directory = relay.Path(self.temporary.name) / "deployed"
        directory.mkdir()
        with zipfile.ZipFile(
            relay.ROOT / "build/Broadcast-Client-Overlay-20261107.zip"
        ) as archive:
            for name in [
                "Qt6Network.dll",
                "Qt6WebSockets.dll",
                "tls/qschannelbackend.dll",
            ]:
                archive.extract(name, directory)
        shutil.copy2(
            relay.ROOT / "build/release/bin/wjn_bc_session_probe.exe", directory
        )
        # Core is an existing-install prerequisite, not a new overlay payload.
        shutil.copy2(relay.ROOT / "build/release/bin/Qt6Core.dll", directory)
        (directory / "qt.conf").write_text(
            "[Paths]\nPrefix=.\nPlugins=.\n", encoding="utf-8"
        )
        environment = os.environ.copy()
        environment.pop("QT_PLUGIN_PATH", None)
        environment["PATH"] = os.pathsep.join(
            [str(directory), os.environ["SystemRoot"] + "/System32"]
        )
        arguments = [
            str(directory / "wjn_bc_session_probe.exe"),
            f"https://{self.address}/bc",
            "station",
        ]
        for trusted in [False, True]:
            with self.subTest(trusted=trusted):
                result = subprocess.run(
                    arguments
                    + (
                        [str(relay.Path(self.temporary.name) / "ca.pem")]
                        if trusted
                        else []
                    ),
                    cwd=directory,
                    env=environment,
                    input="test-publish-secret\n",
                    text=True,
                    stdout=subprocess.PIPE,
                    stderr=subprocess.PIPE,
                    timeout=10,
                )
                report = json.loads(result.stdout)
                self.assertEqual(report["tlsBackend"], "schannel")
                self.assertEqual(
                    result.returncode,
                    0 if trusted else 2,
                    result.stdout + result.stderr,
                )
                self.assertEqual(report["state"], "online" if trusted else "error")
                self.assertNotIn("test-publish-secret", result.stdout + result.stderr)


if __name__ == "__main__":
    unittest.main()
