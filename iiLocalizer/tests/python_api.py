"""Verify the Python API uses the same native PO and substitution contract."""
import os
import pathlib
import unittest

from iilocalizer import Catalog, LabelUsage, LocalizationError, select_label_usages


class LocalizerTest(unittest.TestCase):
    """Compare Python-visible results and errors with the native fixture."""

    def test_translation_and_status(self):
        """Read a real catalog, reorder values and distinguish source fallback."""
        source = pathlib.Path(os.environ['IILOCALIZER_TEST_PO']).read_text(encoding='utf-8')
        with Catalog.from_po(source) as catalog:
            self.assertEqual(catalog.language, 'ko')
            self.assertEqual(catalog.translate('Hello, %1', ['지민']), '안녕하세요, 지민님')
            self.assertEqual(catalog.translate('First %1, second %2', ['A', 'B']), '두 번째 B, 첫 번째 A')
            self.assertEqual(catalog.translate('Open', context='menu'), '열기')
            self.assertEqual(catalog.translate('Open', context=''), '빈 문맥')
            self.assertEqual(catalog.translate('Open'), '개방')
            self.assertEqual(catalog.translate_info('Malformed %1', ['A']).origin, 'source_invalid_translation')
            self.assertEqual(catalog.translate_info('Unreviewed').origin, 'source_missing')
            with self.assertRaises(LocalizationError):
                catalog.translate('Missing %2', ['A'])
        with self.assertRaises(RuntimeError):
            catalog.translate('Open')

    def test_invalid_catalog(self):
        """A malformed or unsupported PO must not create a usable handle."""
        with self.assertRaises(LocalizationError):
            Catalog.from_po('msgid "a"\nmsgid_plural "b"\nmsgstr[0] "c"\n')
        with self.assertRaises(LocalizationError):
            Catalog.from_po('msgid "a"\nmsgstr "\\q"\n')
        with self.assertRaises(LocalizationError):
            Catalog.from_po('msgid "a"\nmsgstr "\x00"\n')

    def test_argument_limits(self):
        """Reject oversized or malformed values before any output is published."""
        with Catalog.from_po('') as catalog:
            with self.assertRaises(LocalizationError):
                catalog.translate('Hello, %1', ['x' * 65537])
            with self.assertRaises(LocalizationError):
                catalog.translate('Hello, %1', ['\x00'])
            with self.assertRaises(TypeError):
                catalog.translate('Hello, %1', 'not a list')

    def test_usage_selection(self):
        """Omitting a location selects all matching registered labels only."""
        uses = [
            LabelUsage('main/toolbar/toolbarGreeting', 'Hello, %1', 'toolbar.greeting'),
            LabelUsage('main/panel/panelGreeting', 'Hello, %1', 'panel.greeting'),
            LabelUsage('main/panel/other', 'Other', 'footer.other'),
        ]
        self.assertEqual(select_label_usages(uses, 'Hello, %1'), [0, 1])
        self.assertEqual(select_label_usages(uses, 'Hello, %1', 'main/panel/panelGreeting'), [1])
        self.assertEqual(select_label_usages(uses, 'Hello, %1', 'missing/greeting'), [])
        self.assertEqual(select_label_usages(uses, 'Hello, %1', 'main/panel/other'), [])
        self.assertEqual(select_label_usages(uses, 'Hello, %1', 'main/panel'), [])
        self.assertEqual(select_label_usages([], 'Hello, %1'), [])
        with self.assertRaises(LocalizationError):
            select_label_usages(uses, 'Hello, %1', '')
        with self.assertRaises(LocalizationError):
            select_label_usages(uses + [LabelUsage('main/toolbar/toolbarGreeting', 'Other', 'unique')], 'Hello, %1')
        with self.assertRaises(LocalizationError):
            select_label_usages(uses + [LabelUsage('unique/location', 'Other', 'toolbar.greeting')], 'Hello, %1')
        with self.assertRaises(LocalizationError):
            select_label_usages([LabelUsage('x' * 65537, 'Hello, %1', 'unique')], 'Hello, %1')


if __name__ == '__main__':
    unittest.main()
