"""Exercise native schema validation against the bundled official required cases."""
import json
from pathlib import Path
import sys
import iiagent_application_commands as c

root = Path(sys.argv[1])
dialects = {
    'draft3':'http://json-schema.org/draft-03/schema#',
    'draft4':'http://json-schema.org/draft-04/schema#',
    'draft6':'http://json-schema.org/draft-06/schema#',
    'draft7':'http://json-schema.org/draft-07/schema#',
    'draft2019-09':'https://json-schema.org/draft/2019-09/schema',
    'draft2020-12':'https://json-schema.org/draft/2020-12/schema',
}
resources = []
for path in (root/'remotes').rglob('*.json'):
    try:
        value = json.loads(path.read_text())
    except (UnicodeError, ValueError):
        continue
    rel = path.relative_to(root/'remotes').as_posix()
    for host in ('http://localhost:1234/', 'https://localhost:1234/', 'http://localhost:4242/'):
        entry = c.CommandSchemaResource(); entry.uri = host + rel; entry.schema_json = json.dumps(value, ensure_ascii=False)
        resources.append(entry)
validator = c.CommandSchemaValidator()
count = 0
failures = []
for draft, dialect in dialects.items():
    for path in sorted((root/'tests'/draft).glob('*.json')):
        for group in json.loads(path.read_text()):
            request = c.CommandSchemaValidationRequest()
            request.default_dialect = dialect
            request.schema_json = json.dumps(group['schema'], ensure_ascii=False)
            if 'localhost:' in request.schema_json:
                request.resources = resources
            for case in group['tests']:
                request.instance_json = json.dumps(case['data'], ensure_ascii=False)
                result = validator.execute(request)
                count += 1
                if result.valid != case['valid'] or result.status not in (c.Status.OK, c.Status.VALIDATION_FAILED):
                    failures.append((draft, path.name, group['description'], case['description'], result.diagnostic))
for failure in failures[:30]: print('FAIL', *failure, sep=' | ', file=sys.stderr)
print(f'Official required cases: {count-len(failures)}/{count} passed')
if failures: raise SystemExit(1)
