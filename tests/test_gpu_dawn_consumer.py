import importlib.util
import sys
import unittest
from pathlib import Path
from unittest import mock

SCRIPT = Path(__file__).resolve().parents[1] / "tools/fetch_gpu_dawn.py"
spec = importlib.util.spec_from_file_location("gpu_dawn_consumer", SCRIPT)
consumer = importlib.util.module_from_spec(spec)
spec.loader.exec_module(consumer)


class ConsumerWiringTest(unittest.TestCase):
    def test_passes_provider_to_gpu_tool(self):
        storage = mock.MagicMock()
        loader = mock.MagicMock()
        module_spec = mock.MagicMock(loader=loader)
        fetch = mock.MagicMock()
        with mock.patch.dict(sys.modules, {"ii_file_provider": storage}), \
             mock.patch.object(consumer.importlib.util, "spec_from_file_location", return_value=module_spec), \
             mock.patch.object(consumer.importlib.util, "module_from_spec", return_value=fetch):
            consumer.main()
        loader.exec_module.assert_called_once_with(fetch)
        fetch.main.assert_called_once_with(storage)


if __name__ == "__main__":
    unittest.main()
