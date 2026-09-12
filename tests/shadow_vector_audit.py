import contextlib
import io
from pathlib import Path
import sqlite3
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "scripts"))
from shadow_vector_audit import audit


class VectorAuditTest(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory()
        self.addCleanup(self.directory.cleanup)
        self.work = Path(self.directory.name)
        self.paths = []
        for index in range(2):
            (self.work / f"node-{index}.log").write_text(
                f"\x1b[0m[node-run] DFS vector owner={index:040x} file_id={index:064x} rows=2/2\n"
            )
            for publisher in range(2):
                home = self.work / "bootstrap" / ("server" if index == 0 else f"client{index}") / "data"
                path = home / "dfs" / f"{publisher:040x}" / f"{publisher:064x}"
                path.parent.mkdir(parents=True)
                with sqlite3.connect(path) as database:
                    database.execute("CREATE TABLE Vector(id TEXT PRIMARY KEY, payload BLOB)")
                    database.executemany("INSERT INTO Vector VALUES(?,?)", [("a", b"a\0b"), ("b", b"b")])
                    database.execute("CREATE TABLE ExVectorNodes(id TEXT)")
                    database.executemany("INSERT INTO ExVectorNodes VALUES(?)", [("x",)] * 10)
                self.paths.append(path)

    def check_audit(self):
        with contextlib.redirect_stdout(io.StringIO()):
            audit(self.work, 2, 2, 0, set())

    def test_complete_with_ansi_prefix_and_derived_tables(self):
        self.check_audit()

    def test_missing_publication(self):
        (self.work / "node-1.log").write_text("")
        with self.assertRaisesRegex(ValueError, "expected one vector publication"):
            self.check_audit()

    def test_derived_rows_do_not_hide_missing_vector_rows(self):
        with sqlite3.connect(self.paths[-1]) as database:
            database.execute("DELETE FROM Vector WHERE id='a'")
        with self.assertRaisesRegex(ValueError, "has 1 rows"):
            self.check_audit()

    def test_equal_counts_with_different_content(self):
        with sqlite3.connect(self.paths[-1]) as database:
            database.execute("UPDATE Vector SET payload=? WHERE id='a'", (b"changed",))
        with self.assertRaisesRegex(ValueError, "differs from the other nodes"):
            self.check_audit()


if __name__ == "__main__":
    unittest.main()
