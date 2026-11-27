"""Public Python command API exercises the same native objects."""
import json
import unittest
import iiagent_application_commands as c

class CommandTests(unittest.TestCase):
    def test_schema_scope_and_cli(self):
        request = c.CommandSchemaValidationRequest()
        request.schema_json = json.dumps({"type": "object", "required": ["name"]})
        request.instance_json = '{"name":"New"}'
        self.assertTrue(c.CommandSchemaValidator().execute(request).valid)
        request.instance_json = '{}'
        self.assertFalse(c.CommandSchemaValidator().execute(request).valid)
        output = c.CliCommandAdapter().execute(["--target", "doc", "--command", "rename", "--arguments", '{}'])
        self.assertEqual(output.invocation.target_id, "doc")
        command = c.ApplicationCommandDescriptor(); command.name = "rename"; command.target_id = "doc"
        command.required_scopes = ["edit"]
        output.invocation.principal = "alice"
        grant = c.CommandGrant(); grant.principal = "alice"; grant.target_id = "doc"; grant.command = "rename"; grant.scopes = ["edit"]
        self.assertTrue(c.CommandScopePolicy().execute(command, output.invocation, grant))
        grant.principal = "eve"
        self.assertFalse(c.CommandScopePolicy().execute(command, output.invocation, grant))

    def test_mcp_and_cancel(self):
        descriptor = c.ApplicationCommandDescriptor(); descriptor.name = "rename"; descriptor.target_id = "doc"
        descriptor.input_schema = '{"type":"object"}'
        request = c.McpCommandInput(); request.published = [descriptor]
        request.message = '{"jsonrpc":"2.0","id":1,"method":"tools/call","params":{"name":"rename","arguments":{}}}'
        response = c.McpCommandAdapter().execute(request)
        self.assertEqual(response.invocation.command, "rename")
        self.assertEqual(response.invocation.target_id, "doc")
        flag = c.CommandCancellationHandle()
        self.assertFalse(flag.cancelled)
        self.assertTrue(flag.execute())
        self.assertTrue(flag.cancelled)
        process = c.IsolatedCommandRequest()
        process.environment = {'EXPLICIT_SETTING':'value'}
        self.assertEqual(process.environment, {'EXPLICIT_SETTING':'value'})

if __name__ == '__main__':
    unittest.main()
