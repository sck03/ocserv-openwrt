"""Resolve native build inputs from the same manifest used to verify downloads."""
import argparse
from build_common import source_directory

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("component", choices=("openconnect", "gnutls", "gmp", "nettle", "stoken", "libxml2", "zlib", "openssl", "lz4"))
parser.add_argument("--mobile", action="store_true", help="Use the Android/Apple source manifest")
parser.add_argument("--version", action="store_true", help="Print only the version for native build arguments")
args = parser.parse_args()
directory = source_directory(args.component, mobile=args.mobile)
print(directory.removeprefix(args.component + "-") if args.version else directory)
