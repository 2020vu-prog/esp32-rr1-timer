import re
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


def read_source(relative_path):
    return (ROOT / relative_path).read_text()


def c_function_body(source, function_name):
    match = re.search(rf"\b{re.escape(function_name)}\s*\([^)]*\)\s*\{{", source)
    if not match:
        raise AssertionError(f"Could not find function {function_name}")

    start = match.end()
    depth = 1
    pos = start
    while pos < len(source) and depth:
        if source[pos] == "{":
            depth += 1
        elif source[pos] == "}":
            depth -= 1
        pos += 1

    if depth:
        raise AssertionError(f"Could not parse function {function_name}")
    return source[start : pos - 1]


class CredentialRetryTests(unittest.TestCase):
    def test_credential_fetch_retries_until_complete_credentials(self):
        source = read_source("components/rr1_capture/get_mqtt_creds.c")
        body = c_function_body(source, "https_request_creds")

        self.assertIn("while (true)", body)
        self.assertIn("free_creds();", body)
        self.assertIn("https_request_discover(host_name);", body)
        self.assertIn("https_request_auth(host_name);", body)
        self.assertIn("have_mqtt_creds()", body)
        self.assertIn("return;", body)
        self.assertNotIn("return false;", body)

    def test_mqtt_start_is_gated_on_credential_success(self):
        source = read_source("main/umain.c")

        self.assertIn("https_request_creds();", source)
        self.assertIn("mqtt_app_start();", source)


if __name__ == "__main__":
    unittest.main()
