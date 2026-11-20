import sys
from pathlib import Path

# Run with python3 -I, which leaves the script's folder off sys.path.
sys.path.insert(0, str(Path(__file__).resolve().parent))

from mapkit.cli import tiled_to_area  # noqa: E402

if __name__ == "__main__":
	sys.exit(tiled_to_area())
