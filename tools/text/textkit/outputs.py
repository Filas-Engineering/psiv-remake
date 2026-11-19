import shutil
import subprocess
from pathlib import Path


class OutputError(Exception):
	pass


def repo_root():
	for parent in Path(__file__).resolve().parents:
		if (parent / ".git").exists():
			return parent
	return None


def _ignored(root, path):
	if shutil.which("git"):
		result = subprocess.run(
			["git", "-C", str(root), "check-ignore", "-q", "--no-index", "--", str(path)],
			capture_output=True,
		)
		if result.returncode in (0, 1):
			return result.returncode == 0
	return (root / "source-material") in path.parents


def check(path, force=False, inputs=()):
	"""Refuse an output path that would overwrite a file or land in git's view.

	Script text must never be committed, so anything written inside the
	checkout has to be in a git-ignored folder.
	"""
	path = Path(path).resolve()
	if any(path == Path(i).resolve() for i in inputs):
		raise OutputError(f"{path} is also an input; write to a new path")
	check_location(path)
	if path.exists():
		if not force:
			raise OutputError(f"{path} already exists (use --force to replace it)")
		if path.is_dir():
			raise OutputError(f"{path} is a directory")
		path.unlink()
	if not path.parent.is_dir():
		raise OutputError(f"{path.parent} does not exist")
	return path


def check_location(path):
	path = Path(path).resolve()
	root = repo_root()
	if root is not None and (root == path or root in path.parents) and not _ignored(root, path):
		raise OutputError(f"{path} is inside the repository and not git-ignored; write it to an ignored folder")
	return path
