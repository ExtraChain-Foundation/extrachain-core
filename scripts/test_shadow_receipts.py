import base64
from contextlib import closing
import sqlite3
import tempfile
import unittest
from pathlib import Path

import msgpack
from shadow_receipts import audit, decode_store_payload


class ReceiptAuditTest(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory()
        self.addCleanup(self.directory.cleanup)
        self.work = Path(self.directory.name)
        self.intent = 'a' * 64
        self.submissions, self.cursors = {}, {}
        (self.work / 'node-0.log').write_text(f'[node-run] intent hash={self.intent} submitted_at_ms=1000\n')
        self.databases = []
        for index in range(7):
            path = self.work / 'bootstrap' / ('server' if index == 0 else f'client{index}') / 'data' / 'consensus'
            path.mkdir(parents=True)
            database = path / 'intent-pool.sqlite'
            with closing(sqlite3.connect(database)) as connection, connection:
                connection.execute('CREATE TABLE consensus_intent_receipts (hash TEXT PRIMARY KEY, payload TEXT)')
            self.databases.append(database)
            self.receipt(index, 2, 7)

    def receipt(self, index, status, height):
        payload = base64.b64encode(msgpack.packb([self.intent, status, height, 25140, 0, 0])).decode().rstrip('=')
        with closing(sqlite3.connect(self.databases[index])) as connection, connection:
            connection.execute('INSERT OR REPLACE INTO consensus_intent_receipts VALUES (?, ?)', (self.intent, payload))

    def result(self):
        return audit(self.work, 1, self.submissions, self.cursors, 7)

    def test_all_nodes_must_finalize_the_same_receipt(self):
        self.assertTrue(self.result()['complete'])
        self.receipt(6, 0, 7)
        self.assertFalse(self.result()['complete'])
        self.receipt(6, 2, 8)
        self.assertFalse(self.result()['complete'])
        self.receipt(6, 2, 7)
        self.assertTrue(self.result()['complete'])

    def test_missing_database_does_not_pass(self):
        self.databases[6].unlink()
        self.assertFalse(self.result()['complete'])

    def test_urlsafe_store_payload_and_invalid_alphabet(self):
        receipt = [self.intent, 2, 255, 65535, 0, 0]
        payload = base64.urlsafe_b64encode(msgpack.packb(receipt)).decode().rstrip('=')
        self.assertTrue('-' in payload or '_' in payload)
        self.assertEqual(decode_store_payload(payload), receipt)
        for database in self.databases:
            with closing(sqlite3.connect(database)) as connection, connection:
                connection.execute('UPDATE consensus_intent_receipts SET payload = ?', (payload,))
        self.assertTrue(self.result()['complete'])
        with closing(sqlite3.connect(self.databases[6])) as connection, connection:
            connection.execute('UPDATE consensus_intent_receipts SET payload = ?', (payload + '!',))
        self.assertFalse(self.result()['complete'])

    def test_incomplete_submission_line_is_retained(self):
        self.submissions.clear()
        (self.work / 'node-0.log').write_text(f'[node-run] intent hash={self.intent} submitted_at_ms=')
        self.assertFalse(self.result()['complete'])
        with (self.work / 'node-0.log').open('a') as stream:
            stream.write('1000\n')
        self.assertTrue(self.result()['complete'])


if __name__ == '__main__':
    unittest.main()
