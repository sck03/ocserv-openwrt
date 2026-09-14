"""Print the source directory from the pinned manifest for a client component."""
import argparse
from build_common import source_directory

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("component", choices=("openconnect", "openssl", "libxml2", "zlib"))
args = parser.parse_args()
print(source_directory(args.component))
