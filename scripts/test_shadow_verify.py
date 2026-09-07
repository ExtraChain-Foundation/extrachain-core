import os
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

from shadow_verify import node_dirs


class MembershipTest(unittest.TestCase):
    def test_includes_optional_observer(self):
        with tempfile.TemporaryDirectory() as temporary, patch.dict(os.environ, {'EXC_VERIFY_SKIP': ''}):
            root = Path(temporary)
            (root / 'bootstrap/server/data').mkdir(parents=True)
            for index in range(1, 7):
                (root / f'bootstrap/client{index}/data').mkdir(parents=True)
            self.assertEqual(len(node_dirs(temporary)), 7)
            (root / 'bootstrap/client7/data').mkdir(parents=True)
            self.assertEqual([name for name, _ in node_dirs(temporary)],
                             [f'node-{index}' for index in range(8)])


if __name__ == '__main__':
    unittest.main()
