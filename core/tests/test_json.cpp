#include "rules/json.h"
#include "test.h"

using namespace rules;

TEST(jsonParsesNestedValues) {
	const Result<JsonValue> parsed = parseJson(R"({
		"name": "Test \"Fighter\"\n",
		"numbers": [0, -12, 3.5, 2e2],
		"flags": {"on": true, "off": false, "none": null},
		"glyph": "é😀"
	})");
	CHECK(parsed.ok());
	if (!parsed.ok()) {
		return;
	}
	const JsonValue& root = *parsed.value;
	CHECK_EQ(root.find("name")->string, std::string("Test \"Fighter\"\n"));
	const JsonValue& numbers = *root.find("numbers");
	CHECK_EQ(numbers.items.size(), std::size_t(4));
	CHECK_EQ(numbers.items[1].number, -12.0);
	CHECK_EQ(numbers.items[2].number, 3.5);
	CHECK_EQ(numbers.items[3].number, 200.0);
	CHECK(root.find("flags")->find("on")->boolean);
	CHECK(root.find("flags")->find("none")->is(JsonValue::Type::Null));
	CHECK_EQ(root.find("glyph")->string, std::string("\xC3\xA9\xF0\x9F\x98\x80"));
	CHECK_EQ(root.find("flags")->line, 4);
	CHECK(root.find("missing") == nullptr);
}

TEST(jsonReportsErrorsWithLineNumbers) {
	struct Case {
		const char* text;
		const char* expected;
	};
	const Case cases[] = {
		{"{\n\t\"a\": 1,\n\t\"b\": tru\n}", "line 3: invalid literal"},
		{"{\n\t\"a\": [1, 2,]\n}", "line 2: unexpected character ']'"},
		{"{\"a\": 1, \"a\": 2}", "line 1: duplicate key 'a'"},
		{"{\n\"a\": \"open\n\"}", "line 2: control character in string"},
		{"[1, 2", "line 1: expected ',' or ']'"},
		{"{\"a\": 01}", "line 1: expected ',' or '}'"},
		{"{} []", "line 1: unexpected content after the top-level value"},
		{"", "line 1: unexpected end of input"},
	};
	for (const Case& c : cases) {
		const Result<JsonValue> parsed = parseJson(c.text);
		CHECK(!parsed.ok());
		if (parsed.error.find(c.expected) == std::string::npos) {
			::test::fail(__FILE__, __LINE__, "expected '" + std::string(c.expected) + "' in '" + parsed.error + "'");
		}
	}
}
