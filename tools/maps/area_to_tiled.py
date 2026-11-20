import sys
from pathlib import Path

# Run with python3 -I, which leaves the script's folder off sys.path.
sys.path.insert(0, str(Path(__file__).resolve().parent))

from mapkit.cli import area_to_tiled  # noqa: E402

if __name__ == "__main__":
	sys.exit(area_to_tiled())
