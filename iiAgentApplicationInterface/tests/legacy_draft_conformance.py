"""Behavioral cases for Draft 0-2, which the official suite does not cover."""
import json
import unittest
import iiagent_application_commands as c

class LegacyDrafts(unittest.TestCase):
    def validate(self, draft, schema, instance):
        request = c.CommandSchemaValidationRequest()
        request.default_dialect = f'http://json-schema.org/draft-0{draft}/schema#'
        request.schema_json = json.dumps(schema, ensure_ascii=False)
        request.instance_json = json.dumps(instance, ensure_ascii=False)
        return c.CommandSchemaValidator().execute(request)

    def test_shared_property_and_type_contracts(self):
        for draft in range(3):
            cases = [
                ({'properties':{'required':{'type':'string'},'optional':{'type':'integer','optional':True}}}, {}, False),
                ({'properties':{'required':{'type':'string'},'optional':{'type':'integer','optional':True}}}, {'required':'x'}, True),
                ({'properties':{'required':{'type':'string'},'optional':{'type':'integer','optional':True}}}, {'required':'x','optional':'bad'}, False),
                ({'type':['string','integer']}, 3, True),
                ({'type':['string','integer']}, False, False),
                ({'type':'any'}, None, True),
                ({'disallow':'string'}, 'x', False),
                ({'extends':[{'type':'number'},{'minimum':3}]}, 2, False),
                ({'extends':[{'type':'number'},{'minimum':3}]}, 3, True),
                ({'items':[{'type':'integer'}], 'additionalProperties':False}, [1, 2], False),
                ({'items':[{'type':'integer'}], 'additionalProperties':False}, [1], True),
                ({'minimum':3, 'minimumCanEqual':False}, 3, False),
                ({'maximum':3, 'maximumCanEqual':False}, 3, False),
                ({'properties':{'state':{'optional':True},'town':{'requires':'state','optional':True}}}, {'town':'x'}, False),
                ({'properties':{'state':{'optional':True},'town':{'requires':'state','optional':True}}}, {'town':'x','state':'y'}, True),
            ]
            for schema, instance, expected in cases:
                with self.subTest(draft=draft, schema=schema, instance=instance):
                    result = self.validate(draft, schema, instance)
                    self.assertEqual(result.valid, expected, result.diagnostic)
                    self.assertIn(result.status, (c.Status.OK, c.Status.VALIDATION_FAILED))

    def test_version_specific_keywords(self):
        for draft in (0, 1):
            with self.subTest(draft=draft):
                self.assertFalse(self.validate(draft, {'maxDecimal':2}, 1.234).valid)
                self.assertTrue(self.validate(draft, {'maxDecimal':2}, 1.23).valid)
                self.assertTrue(self.validate(draft, {'uniqueItems':True}, [1,1]).valid)
        self.assertFalse(self.validate(2, {'uniqueItems':True}, [1,1]).valid)
        self.assertFalse(self.validate(2, {'divisibleBy':0.2}, 0.3).valid)
        self.assertTrue(self.validate(2, {'divisibleBy':0.2}, 0.4).valid)

    def test_large_max_decimal_in_root_and_referenced_draft(self):
        for draft in (0, 1):
            schema = {'$schema': f'http://json-schema.org/draft-0{draft}/schema#',
                      'maxDecimal': 400}
            for referenced in (False, True):
                request = c.CommandSchemaValidationRequest()
                request.instance_json = '1'
                if referenced:
                    request.schema_json = '{"$ref":"https://example.invalid/old"}'
                    resource = c.CommandSchemaResource()
                    resource.uri = 'https://example.invalid/old'
                    resource.schema_json = json.dumps(schema)
                    request.resources = [resource]
                else:
                    request.schema_json = json.dumps(schema)
                with self.subTest(draft=draft, referenced=referenced):
                    self.assertTrue(c.CommandSchemaValidator().execute(request).valid)
                    request.instance_json = '1e-401'
                    self.assertEqual(c.CommandSchemaValidator().execute(request).status,
                                     c.Status.VALIDATION_FAILED)

    def test_referenced_draft_five_uses_draft_four_semantics(self):
        request = c.CommandSchemaValidationRequest()
        request.schema_json = '{"$ref":"https://example.invalid/draft-five"}'
        resource = c.CommandSchemaResource()
        resource.uri = 'https://example.invalid/draft-five'
        resource.schema_json = ('{"$schema":"http://json-schema.org/draft-05/schema#",'
                                '"type":"number","minimum":3,"exclusiveMinimum":true}')
        request.resources = [resource]
        for instance, valid in (('3', False), ('4', True), ('3.0', False), ('3.5', True)):
            with self.subTest(instance=instance):
                request.instance_json = instance
                result = c.CommandSchemaValidator().execute(request)
                self.assertEqual(result.valid, valid, result.diagnostic)
                self.assertIn(result.status, (c.Status.OK, c.Status.VALIDATION_FAILED))

if __name__ == '__main__': unittest.main()
