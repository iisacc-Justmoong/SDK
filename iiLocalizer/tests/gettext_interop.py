"""Compare supported PO lookups with the installed GNU gettext tooling."""

import gettext
import io
import os
from pathlib import Path
import subprocess

from iilocalizer import Catalog


# Use the same source-controlled PO for both readers; no catalog is written to disk.
source = Path(os.environ['IILOCALIZER_TEST_PO']).read_bytes()
compiled = subprocess.run(
    [os.environ['IILOCALIZER_MSGFMT'], '--check-format', '-o', '-', '-'],
    input=source, stdout=subprocess.PIPE, stderr=subprocess.PIPE, check=True,
)
reference = gettext.GNUTranslations(io.BytesIO(compiled.stdout))
with Catalog.from_po(source.decode('utf-8')) as catalog:
    for message, context in (
        ('Open', None), ('Open', ''), ('Open', 'menu'),
        ('Unreviewed', None), ('Unreviewed too', None),
        ('Obsolete', None), ('Untranslated', None), ('Escapes', None),
    ):
        expected = reference.gettext(message) if context is None else reference.pgettext(context, message)
        actual = catalog.translate(message, context=context)
        if actual != expected:
            raise AssertionError((message, context, actual, expected))
