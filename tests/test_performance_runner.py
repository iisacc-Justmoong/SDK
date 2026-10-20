import importlib.util
from pathlib import Path
import unittest

spec=importlib.util.spec_from_file_location("runner",Path(__file__).resolve().parents[1]/"tools/run_performance_benchmarks.py")
runner=importlib.util.module_from_spec(spec);spec.loader.exec_module(runner)


class RunnerTests(unittest.TestCase):
    def test_preserves_samples_and_uses_median(self):
        report=runner.summarize({"before":[runner.parse_output(f"case {n} 42") for n in (10,100,20)],
                                 "after":[runner.parse_output(f"case {n} 42") for n in (2,3,1)]})
        self.assertEqual(report["case"]["before"]["median_ms"],20)
        self.assertEqual(report["case"]["speedup"],10)
        self.assertEqual(report["case"]["before"]["samples_ms"],[10,100,20])

    def test_changed_results_or_missing_cases_cannot_produce_speedups(self):
        with self.assertRaises(ValueError): runner.summarize({"before":[runner.parse_output("a 10 42")],"after":[runner.parse_output("a 1 43")]})
        with self.assertRaises(ValueError): runner.summarize({"before":[runner.parse_output("a 10 42")],"after":[runner.parse_output("b 1 42")]})
        with self.assertRaises(ValueError): runner.summarize({"after":[runner.parse_output("a 1 1"),runner.parse_output("a 2 2")]})

    def test_invalid_measurements_are_rejected(self):
        for output in ("", "a nan 1", "a -1 1", "a 1 1\na 2 1", "invalid"):
            with self.assertRaises(ValueError): runner.parse_output(output)


if __name__=="__main__": unittest.main()
