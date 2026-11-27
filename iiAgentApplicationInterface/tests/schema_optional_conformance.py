"""Run optional official cases; exact numeric files use their own lossless runner."""
import collections, json, pathlib, sys
import iiagent_application_commands as c
root = pathlib.Path(sys.argv[1])
dialects = {'draft3':'http://json-schema.org/draft-03/schema#', 'draft4':'http://json-schema.org/draft-04/schema#', 'draft6':'http://json-schema.org/draft-06/schema#', 'draft7':'http://json-schema.org/draft-07/schema#', 'draft2019-09':'https://json-schema.org/draft/2019-09/schema', 'draft2020-12':'https://json-schema.org/draft/2020-12/schema'}
resources = []
for path in (root/'remotes').rglob('*.json'):
    try: value = json.loads(path.read_text())
    except (UnicodeError, ValueError): continue
    for host in ('http://localhost:1234/', 'https://localhost:1234/', 'http://localhost:4242/'):
        entry = c.CommandSchemaResource(); entry.uri = host + path.relative_to(root/'remotes').as_posix(); entry.schema_json = json.dumps(value, ensure_ascii=False); resources.append(entry)
validator = c.CommandSchemaValidator(); total = 0; failures = collections.defaultdict(list)
for draft, dialect in dialects.items():
    for path in sorted((root/'tests'/draft/'optional').rglob('*.json')):
        if path.name in ('bignum.json', 'float-overflow.json'): continue
        for group in json.loads(path.read_text()):
            request = c.CommandSchemaValidationRequest(); request.default_dialect = dialect; request.schema_json = json.dumps(group['schema'], ensure_ascii=False); request.assert_formats = True
            if 'localhost:' in request.schema_json: request.resources = resources
            for case in group['tests']:
                request.instance_json = json.dumps(case['data'], ensure_ascii=False)
                result = validator.execute(request); total += 1
                if result.valid != case['valid'] or result.status not in (c.Status.OK, c.Status.VALIDATION_FAILED):
                    failures[str(path.relative_to(root/'tests'))].append((group['description'], case['description'], result.diagnostic))
count = sum(map(len, failures.values()))
print(f'Official optional cases: {total-count}/{total} passed')
for path, values in failures.items(): print(path, len(values), values[0])
if failures: raise SystemExit(1)
