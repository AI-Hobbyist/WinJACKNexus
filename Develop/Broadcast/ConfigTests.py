"""Real server configuration, CLI precedence and optional TLS startup."""

import http.client
import json
from pathlib import Path
import queue
import ssl
import subprocess
import tempfile
import threading
import unittest

from websockets.sync.client import connect

import RelayTests as relay


class ConfigTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.directory = Path(self.temporary.name)
        (self.directory / "publish.token").write_text(
            "test-publish-secret", encoding="utf-8"
        )

    def start(self, settings, overrides=(), directory=None):
        directory = directory or self.directory
        path = directory / "server.json"
        path.write_text(json.dumps(settings), encoding="utf-8")
        process = subprocess.Popen(
            [str(relay.SERVER), "--config", str(path), *overrides],
            cwd=relay.ROOT,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            text=True,
        )
        output = queue.Queue()

        def capture():
            for line in process.stdout:
                output.put(line)

        reader = threading.Thread(target=capture, daemon=True)
        reader.start()

        def cleanup():
            if process.poll() is None:
                process.terminate()
            process.wait(timeout=5)
            reader.join(timeout=5)
            process.stdout.close()

        self.addCleanup(cleanup)
        return json.loads(output.get(timeout=8))

    def settings(self):
        return {
            "bind": "127.0.0.1:0",
            "udp_bind": "127.0.0.1:0",
            "publish_token_file": "publish.token",
        }

    def check_http_ws(self, ready):
        connection = http.client.HTTPConnection(ready["http"], timeout=3)
        self.addCleanup(connection.close)
        connection.request("GET", "/bc/listen?publisher=station&stream=missing")
        self.assertEqual(connection.getresponse().status, 404)
        with connect(
            f"ws://{ready['http']}/bc/publish?publisher=config-test",
            additional_headers={"Authorization": "Bearer test-publish-secret"},
        ) as socket:
            self.assertEqual(json.loads(socket.recv())["version"], 1)

    def test_config_relative_paths_work_from_another_directory_without_tls(self):
        ready = self.start(self.settings())
        self.assertNotIn("tls", ready)
        self.check_http_ws(ready)

    def test_cli_addresses_override_file_independent_of_argument_order(self):
        settings = self.settings()
        settings["bind"] = "192.0.2.1:1"
        settings["udp_bind"] = "192.0.2.1:2"
        ready = self.start(
            settings, ["--bind", "127.0.0.1:0", "--udp-bind", "127.0.0.1:0"]
        )
        self.check_http_ws(ready)

    def test_plain_http_can_bind_public_interfaces_for_a_reverse_proxy(self):
        settings = self.settings()
        settings.update(bind="0.0.0.0:0", udp_bind="0.0.0.0:0")
        ready = self.start(settings)
        self.assertTrue(ready["http"].startswith("0.0.0.0:"))
        ready["http"] = "127.0.0.1:" + ready["http"].rsplit(":", 1)[1]
        self.check_http_ws(ready)

    def test_partial_tls_and_unknown_fields_fail_before_listening(self):
        for extra in [{"tls_cert": "cert.pem"}, {"typo": True}]:
            with self.subTest(extra=extra):
                path = self.directory / "invalid.json"
                path.write_text(
                    json.dumps({**self.settings(), **extra}), encoding="utf-8"
                )
                result = subprocess.run(
                    [str(relay.SERVER), "--config", str(path)],
                    stdout=subprocess.PIPE,
                    stderr=subprocess.PIPE,
                    text=True,
                    timeout=5,
                )
                self.assertNotEqual(result.returncode, 0)
                self.assertEqual(result.stdout, "")
                self.assertNotIn("test-publish-secret", result.stderr)

    def test_pair_of_tls_files_enables_verified_https_and_wss(self):
        fixture = relay.RelayTests("runTest")
        fixture.start_server(tls=True)
        self.addCleanup(fixture.doCleanups)
        fixture.stop_server()
        directory = Path(fixture.temporary.name)
        ready = self.start(
            {**self.settings(), "tls_cert": "test.pem", "tls_key": "test-key.pem"},
            directory=directory,
        )
        self.assertTrue(ready["tls"])
        context = ssl.create_default_context(cafile=str(directory / "test.pem"))
        connection = http.client.HTTPSConnection(
            ready["http"], context=context, timeout=3
        )
        self.addCleanup(connection.close)
        connection.request("GET", "/bc/listen?publisher=station&stream=missing")
        self.assertEqual(connection.getresponse().status, 404)
        with connect(
            f"wss://{ready['http']}/bc/publish?publisher=config-test",
            ssl=context,
            additional_headers={"Authorization": "Bearer test-publish-secret"},
        ) as socket:
            self.assertEqual(json.loads(socket.recv())["version"], 1)


if __name__ == "__main__":
    unittest.main()
