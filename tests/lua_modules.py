"""Load the shipped service and management modules into isolated Lua fixtures."""
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def install_modules(lua):
    for directory, namespace in (
        ("server/openwrt/ocserv/files/lua", "ocserv."),
        ("server/openwrt/luci-app-ocserv-easy/luasrc/model/ocserv_easy", "luci.model.ocserv_easy."),
    ):
        for path in (ROOT / directory).glob("*.lua"):
            lua.execute('package.preload[...] = assert(loadstring(select(2,...)))',
                        namespace + path.stem, path.read_text(encoding="utf-8"))
