"""Verify recovery when canonical sections commit before their derived index."""
import pathlib
import sqlite3
import subprocess
import sys
import tempfile

binary = pathlib.Path(sys.argv[1]).resolve()
with tempfile.TemporaryDirectory(prefix="extrachain-index-restart-") as home:
    for phase in ("crash", "recover", "clean"):
        subprocess.run([str(binary), phase, home], check=True, timeout=15)
    for suffix in ("", "-wal", "-shm"):
        (pathlib.Path(home) / ("dag/cache/ChainIndex.db" + suffix)).unlink(missing_ok=True)
    for phase in ("recover", "clean", "invalidate", "incomplete"):
        subprocess.run([str(binary), phase, home], check=True, timeout=15)
    with sqlite3.connect(pathlib.Path(home) / "dag/cache/ChainIndex.db") as connection:
        connection.execute("UPDATE index_meta SET value='2' WHERE key='derived_index_version'")
        connection.execute("DELETE FROM index_meta WHERE key='clean_shutdown'")
    subprocess.run([str(binary), "incomplete", home], check=True, timeout=15)
