#!/usr/bin/env python3
"""Check the content and presence of every file published in each workload wave."""
import functools
import hashlib
import json
from pathlib import Path
import re
import stat

from shadow_artifacts import homes


@functools.lru_cache(maxsize=1792)
def expected_digest(publisher, wave, size):
    pattern = bytes((offset * (131 + publisher) + 17 + publisher * 7 + wave) & 255
                    for offset in range(256))
    digest = hashlib.sha256()
    chunk = pattern * 256
    for _ in range(size // len(chunk)):
        digest.update(chunk)
    digest.update(chunk[:size % len(chunk)])
    return digest.digest()


def audit_waves(work, waves, nodes=7, size=1048576, cache=None, publishers=7):
    if not (1 <= waves <= 256 and 1 <= publishers <= nodes <= 8 and 0 < size <= 16777216):
        raise ValueError('Invalid wave, member, publisher or file size bound')
    cache = {} if cache is None else cache
    directories = homes(Path(work), nodes)
    failures, seen, live_keys = [], set(), set()
    copies = 0
    for wave in range(waves):
        for publisher in range(publishers):
            marker = Path(work) / 'barrier' / f'published-wave-{wave}-{publisher}.json'
            try:
                if marker.is_symlink() or marker.stat().st_size > 1024:
                    raise ValueError('Invalid publication record')
                record = json.loads(marker.read_text())
                if (not isinstance(record, dict) or set(record) != {'owner', 'file_id', 'wave', 'size'}
                        or not all(isinstance(value, str) for value in record.values())
                        or record['wave'] != str(wave) or record['size'] != str(size)
                        or re.fullmatch('[0-9a-f]{40}', record['owner']) is None
                        or re.fullmatch('[0-9a-f]{1,64}', record['file_id']) is None):
                    raise ValueError('Invalid publication fields')
                identity = record['owner'], record['file_id']
                if identity in seen:
                    raise ValueError('Publication reused an existing file')
                seen.add(identity)
            except (OSError, ValueError) as error:
                failures.append(f'Wave {wave}, publisher {publisher}: {error}')
                continue
            expected = expected_digest(publisher, wave, size)
            for member, directory in enumerate(directories):
                path = directory / 'dfs' / record['owner'] / record['file_id']
                try:
                    info = path.lstat()
                    if (not stat.S_ISREG(info.st_mode) or info.st_size != size
                            or path.parent.is_symlink()):
                        raise ValueError('Invalid payload type or size')
                    key = (path, info.st_dev, info.st_ino, info.st_size, info.st_mtime_ns, info.st_ctime_ns)
                    digest = cache.get(key)
                    if digest is None:
                        hasher = hashlib.sha256()
                        with path.open('rb') as stream:
                            for chunk in iter(lambda: stream.read(65536), b''):
                                hasher.update(chunk)
                        digest = hasher.digest()
                    if digest != expected:
                        raise ValueError('Content mismatch')
                    cache[key] = digest
                    live_keys.add(key)
                    copies += 1
                except (OSError, ValueError) as error:
                    failures.append(f'Wave {wave}, member {member}, publisher {publisher}: {error}')
    for key in cache.keys() - live_keys:
        del cache[key]
    return dict(complete=not failures and copies == waves * publishers * nodes,
                copies=copies, expected_copies=waves * publishers * nodes, waves=waves,
                bytes_per_file=size, failures=failures)
