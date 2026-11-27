"""Run official optional high-precision cases without Python float rounding."""
import decimal
import json
from pathlib import Path
import sys
import iiagent_application_commands as c

root = Path(sys.argv[1]) / 'tests'
dialects = {
    'draft3':'http://json-schema.org/draft-03/schema#',
    'draft4':'http://json-schema.org/draft-04/schema#',
    'draft6':'http://json-schema.org/draft-06/schema#',
    'draft7':'http://json-schema.org/draft-07/schema#',
    'draft2019-09':'https://json-schema.org/draft/2019-09/schema',
    'draft2020-12':'https://json-schema.org/draft/2020-12/schema',
}

def encode(value):
    """Preserve each Decimal's source value as a JSON number, including exponent."""
    if isinstance(value, decimal.Decimal): return str(value)
    if isinstance(value, list): return '[' + ','.join(map(encode, value)) + ']'
    if isinstance(value, dict):
        return '{' + ','.join(json.dumps(key, ensure_ascii=False) + ':' + encode(item)
                              for key, item in value.items()) + '}'
    return json.dumps(value, ensure_ascii=False)

validator = c.CommandSchemaValidator()
total = 0
failures = []
for draft, dialect in dialects.items():
    for filename in ('bignum.json', 'float-overflow.json'):
        path = root / draft / 'optional' / filename
        if not path.exists(): continue
        for group in json.loads(path.read_text(), parse_float=decimal.Decimal):
            request = c.CommandSchemaValidationRequest()
            request.default_dialect = dialect
            request.schema_json = encode(group['schema'])
            for case in group['tests']:
                request.instance_json = encode(case['data'])
                result = validator.execute(request)
                total += 1
                if result.valid != case['valid'] or result.status not in (c.Status.OK, c.Status.VALIDATION_FAILED):
                    failures.append((draft, filename, group['description'], case['description'], result.diagnostic))
for failure in failures: print('FAIL', *failure, sep=' | ', file=sys.stderr)
print(f'Official optional precision cases: {total-len(failures)}/{total} passed')
if failures: raise SystemExit(1)
