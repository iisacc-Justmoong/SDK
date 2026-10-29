import gzip
import subprocess
import sys

result = subprocess.run([sys.argv[1]], check=True, text=True, capture_output=True)
compressed = bytes.fromhex(result.stdout.strip())
if gzip.decompress(compressed) != bytes([0, 1, 2, 3, 0, 255]) + b"gzip":
    raise SystemExit("The standard gzip decoder did not recover the original bytes")
