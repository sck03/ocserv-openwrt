"""Real JSON fixtures with LuCI jsonc's null-to-nil conversion semantics."""
import json

from lupa.lua51 import lua_type


def install(lua):
    def to_python(value):
        if lua_type(value) != "table":
            return value
        keys = list(value.keys())
        if keys and set(keys) == set(range(1, len(keys) + 1)):
            return [to_python(value[i]) for i in range(1, len(keys) + 1)]
        return {key: to_python(value[key]) for key in keys}

    def to_lua(value):
        if isinstance(value, dict):
            return lua.table_from({key: to_lua(item) for key, item in value.items()})
        if isinstance(value, list):
            return lua.table_from([to_lua(item) for item in value])
        return value

    lua.globals().TEST_JSON_ENCODE = lambda value: json.dumps(to_python(value))
    lua.globals().TEST_JSON_DECODE = lambda raw: to_lua(json.loads(raw))
