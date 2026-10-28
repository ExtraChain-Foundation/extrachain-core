import contextlib
import io
from pathlib import Path
import sqlite3
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "scripts"))
from shadow_history_audit import audit


class HistoryAuditTest(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory()
        self.addCleanup(self.directory.cleanup)
        self.work = Path(self.directory.name)
        self.paths = []
        for index in range(2):
            (self.work / f"node-{index}.log").write_text(
                f"[node-run] DFS history owner={index:040x} file_id={index:064x} rows=2 hash={'a' * 64}\n"
            )
            for publisher in range(2):
                home = self.work / "bootstrap" / ("server" if index == 0 else f"client{index}") / "data"
                path = home / "dfs" / f"{publisher:040x}" / f"{publisher:064x}"
                path.parent.mkdir(parents=True)
                with sqlite3.connect(path) as database:
                    database.execute("CREATE TABLE HistoryItems(id INTEGER PRIMARY KEY, payload TEXT)")
                    database.executemany("INSERT INTO HistoryItems VALUES(?,?)", [(1, "updated"), (3, "original")])
                    database.execute("CREATE TABLE historical_chain(id INTEGER PRIMARY KEY, hash TEXT)")
                    database.executemany("INSERT INTO historical_chain VALUES(?,?)", [(i, "a" * 64) for i in range(6)])
                self.paths.append(path)

    def check_audit(self):
        with contextlib.redirect_stdout(io.StringIO()):
            audit(self.work, 2, 3, set())

    def test_complete(self):
        self.check_audit()

    def test_bad_copies(self):
        mutations = [
            "DELETE FROM HistoryItems WHERE id=3",
            "UPDATE HistoryItems SET payload='changed' WHERE id=3",
            "UPDATE HistoryItems SET payload='old' WHERE id=1",
            "UPDATE HistoryItems SET id=2 WHERE id=3",
            "DELETE FROM historical_chain WHERE id=5",
            "UPDATE historical_chain SET hash='wrong' WHERE id=5",
        ]
        for mutation in mutations:
            with self.subTest(mutation=mutation):
                path = self.paths[-1]
                backup = path.read_bytes()
                try:
                    with sqlite3.connect(path) as database:
                        database.execute(mutation)
                    with self.assertRaises(ValueError):
                        self.check_audit()
                finally:
                    path.write_bytes(backup)

    def test_missing_publication(self):
        (self.work / "node-1.log").write_text("")
        with self.assertRaises(ValueError):
            self.check_audit()


if __name__ == "__main__":
    unittest.main()
