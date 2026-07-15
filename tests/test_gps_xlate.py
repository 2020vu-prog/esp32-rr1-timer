import re
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
MEG1 = 1_000_000
YEAR5 = 3600 * 24 * 360 * 5
TST_XLATE_LO = 50 * MEG1
TST_XLATE_HI = (51 * MEG1) + 30
TST_EDGE = 70_000


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


def xlate_cap64(sample64):
    t0_epoch = YEAR5 + 7
    t1_epoch = YEAR5 + 9
    ig_delta = TST_XLATE_HI - TST_XLATE_LO
    it_delta = t1_epoch - t0_epoch

    sample_offset = sample64 - TST_XLATE_LO
    epoch64us = (sample_offset * it_delta * MEG1) // ig_delta
    return epoch64us + (t0_epoch * MEG1)


class GpsXlateTests(unittest.TestCase):
    def test_translation_matches_old_ghandle_endpoint_cases(self):
        self.assertEqual(xlate_cap64(TST_XLATE_LO), (YEAR5 + 7) * MEG1)
        self.assertEqual(xlate_cap64(TST_XLATE_HI), (YEAR5 + 9) * MEG1)

    def test_translation_is_monotonic_across_old_ghandle_sweep(self):
        prior_us = None
        delta_min = None
        delta_max = None

        for sample64 in range(
            TST_XLATE_LO - TST_EDGE, TST_XLATE_HI + TST_EDGE + 1, 10_000
        ):
            result_us = xlate_cap64(sample64)
            if prior_us is not None:
                delta = result_us - prior_us
                delta_min = delta if delta_min is None else min(delta_min, delta)
                delta_max = delta if delta_max is None else max(delta_max, delta)
                self.assertGreater(delta, 0)
            prior_us = result_us

        self.assertGreaterEqual(delta_min, 19_999)
        self.assertLessEqual(delta_max, 20_000)

    def test_ghandle_diagnostic_is_not_called_at_runtime(self):
        source = read_source("components/rr1_capture/timer_hist.c")
        body = c_function_body(source, "timer_hist_init")

        self.assertNotIn("test_ghandle", body)

    def test_old_ghandle_diagnostic_was_removed(self):
        source = read_source("components/rr1_capture/gps_xlate.c")
        header = read_source("components/rr1_capture/include/gps_xlate.h")

        self.assertNotIn("test_ghandle", source)
        self.assertNotIn("test_ghandle", header)


if __name__ == "__main__":
    unittest.main()
