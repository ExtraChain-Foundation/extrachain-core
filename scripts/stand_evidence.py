"""Bounded status, immutable inputs, and deterministic stand evidence."""
import hashlib
import json
import os
from pathlib import Path
import re
import time


TERMINAL = {'PASS', 'FAIL', 'INTERRUPTED', 'INFRA_ERROR'}


def write_json(path, value):
    path = Path(path)
    temporary = path.with_name(path.name + '.tmp')
    with temporary.open('w') as stream:
        json.dump(value, stream, sort_keys=True, indent=2, allow_nan=False)
        stream.write('\n')
        stream.flush()
        os.fsync(stream.fileno())
    temporary.replace(path)


def digest(path, pulse=lambda: None):
    path = Path(path)
    result = hashlib.sha256()
    if path.is_dir():
        for entry in sorted(path.rglob('*')):
            if entry.is_symlink():
                raise ValueError(f'Directory inputs must not contain symlinks: {entry}')
            result.update(entry.relative_to(path).as_posix().encode() + b'\0')
            result.update(b'file\0' if entry.is_file() else b'dir\0')
            if entry.is_file():
                result.update(digest(entry, pulse).encode())
            pulse()
    else:
        with path.open('rb') as stream:
            for block in iter(lambda: stream.read(1024 * 1024), b''):
                result.update(block)
                pulse()
    return result.hexdigest()


def fingerprint(value):
    return hashlib.sha256(json.dumps(value, sort_keys=True, separators=(',', ':'),
                                     allow_nan=False).encode()).hexdigest()


def tail(path, limit=2048):
    try:
        with Path(path).open('rb') as stream:
            stream.seek(max(0, os.fstat(stream.fileno()).st_size - limit))
            return stream.read(limit).decode(errors='replace')
    except FileNotFoundError:
        return ''


class Lines:
    """Read complete appended records without loading an entire journal."""
    def __init__(self, path):
        self.path = Path(path)
        self.offset = 0
        self.identity = None
        self.pending = b''

    def read(self, budget=1024 * 1024):
        if not self.path.exists():
            return []
        with self.path.open('rb') as stream:
            stat = os.fstat(stream.fileno())
            identity = (stat.st_dev, stat.st_ino)
            if self.identity not in (None, identity) or stat.st_size < self.offset:
                raise ValueError(f'Evidence was replaced or truncated: {self.path}')
            self.identity = identity
            stream.seek(self.offset)
            data = stream.read(budget)
            self.offset += len(data)
        data = self.pending + data
        complete, separator, self.pending = data.rpartition(b'\n')
        if not separator:
            self.pending = data
            complete = b''
        if len(self.pending) > 1024 * 1024:
            raise ValueError(f'Evidence record exceeds 1 MiB: {self.path}')
        return complete.decode(errors='replace').splitlines()


class Errors:
    """Keep the first error and bounded groups while scanning new output once."""
    def __init__(self):
        self.first = None
        self.groups = {}
        self.overflow = 0

    def feed(self, line):
        if not re.search(r'\b(?:ERROR|FAIL|FATAL|Traceback|AssertionError|RuntimeError)\b', line):
            return
        example = line[:400]
        if self.first is None:
            self.first = example
        key = re.sub(r'0x[0-9a-fA-F]+|\b\d+\b', '#', example)
        if key in self.groups:
            self.groups[key]['count'] += 1
        elif len(self.groups) < 8:
            self.groups[key] = dict(first=example, count=1)
        else:
            self.overflow += 1

    def summary(self):
        return dict(first=self.first, groups=list(self.groups.values()), overflow=self.overflow,
                    scope='stage output; diagnostic hints, not a verdict')


class Progress:
    def __init__(self):
        self.last = None
        self.started_ms = None
        self.configuration = None
        self.waves = {}
        self.failed = None
        self.checkpoint = False
        self.auditing = False
        self.peak_rss_bytes = 0
        self.peak_fds = 0
        self.peak_log_bytes = 0
        self.sampled_nodes = 0
        self.latest_resource_s = None

    def event(self, entry):
        if not isinstance(entry, dict):
            raise ValueError('An event must be an object')
        kind = entry.get('event')
        if kind is None:
            return
        if self.last and self.last.get('event') in ('PASS', 'FAIL', 'INTERRUPTED'):
            raise ValueError('Event follows a terminal event')
        self.last = entry
        if kind == 'configuration':
            if self.configuration is not None:
                raise ValueError('Duplicate configuration')
            self.configuration = entry
        elif kind == 'endurance-start':
            if self.started_ms is not None:
                raise ValueError('Workload restarted within one run')
            self.started_ms = entry['monotonic_ms']
        elif kind == 'wave-converged':
            index = entry['wave']
            if index not in (len(self.waves), len(self.waves) - 1):
                raise ValueError('Missing or out-of-order wave')
            self.waves[index] = entry
        elif kind in ('FAIL', 'INTERRUPTED'):
            self.failed = entry
        elif kind == 'shutdown-checkpoint':
            self.checkpoint = True
        elif kind == 'offline-audit-start':
            self.auditing = True

    def resource(self, entry):
        nodes = entry['nodes']
        self.sampled_nodes = len(nodes)
        self.latest_resource_s = entry['monotonic_s']
        for node in nodes:
            self.peak_rss_bytes = max(self.peak_rss_bytes, node['rss_bytes'])
            self.peak_fds = max(self.peak_fds, node['fds'])
            self.peak_log_bytes = max(self.peak_log_bytes, node['log_bytes'])

    def summary(self):
        last_wave = self.waves[max(self.waves)] if self.waves else {}
        return dict(last_event=(self.last or {}).get('event'), waves=len(self.waves),
                    receipts=last_wave.get('receipts', 0), file_copies=last_wave.get('file_copies', 0),
                    nodes=last_wave.get('nodes', 0), sampled_nodes=self.sampled_nodes,
                    workload_elapsed_s=round(time.monotonic() - self.started_ms / 1000, 1)
                    if self.started_ms is not None else 0,
                    resource_age_s=round(time.monotonic() - self.latest_resource_s, 1)
                    if self.latest_resource_s is not None else None,
                    peak_rss_bytes=self.peak_rss_bytes, peak_fds=self.peak_fds,
                    peak_log_bytes=self.peak_log_bytes)

    def verdict(self, expected):
        last = self.last or {}
        if last.get('event') == 'INTERRUPTED':
            return 'INTERRUPTED'
        if self.failed:
            return 'FAIL'
        if last.get('event') != 'PASS':
            return 'INFRA_ERROR'
        configuration = self.configuration or {}
        if any(configuration.get(key) != value for key, value in expected.items()):
            return 'FAIL'
        waves = expected['duration_s'] // expected['interval_s']
        receipts = waves * expected['senders'] * expected['per_sender']
        if (self.started_ms is None or not self.checkpoint or not self.auditing
                or set(self.waves) != set(range(waves))
                or last.get('elapsed_s', 0) < expected['duration_s']
                or last.get('monotonic_ms', 0) - self.started_ms < expected['duration_s'] * 1000
                or last.get('waves') != waves or last.get('receipts') != receipts
                or last.get('file_copies') != waves * 56):
            return 'FAIL'
        for index, wave in self.waves.items():
            nodes = wave.get('nodes')
            if (nodes not in (7, 8) or wave.get('receipts') != (index + 1) * expected['senders'] * expected['per_sender']
                    or wave.get('file_copies') != (index + 1) * 7 * nodes):
                return 'FAIL'
        return 'PASS' if self.waves[waves - 1]['nodes'] == 8 else 'FAIL'


def load_progress(log):
    result = Progress()
    with Path(log).open() as stream:
        for line in stream:
            if line.startswith('{'):
                result.event(json.loads(line))
    return result


def confined(directory, relative):
    path = (Path(directory) / relative).resolve()
    if not path.is_relative_to(Path(directory).resolve()):
        raise ValueError('Evidence path escapes its stage')
    return path


def verify_stage(stage, directory, progress, pulse=lambda: None, started=None):
    evidence = {'output.log': digest(directory / 'output.log', pulse)}
    if stage['kind'] == 'endurance':
        verdict = progress.verdict(stage['expect'])
        if verdict != 'PASS':
            raise ValueError(f'Endurance evidence is {verdict}')
        if started is not None and (progress.started_ms < started * 1000 - 1
                or time.monotonic() - started < stage['expect']['duration_s']
                or progress.last['monotonic_ms'] > time.monotonic() * 1000 + 1):
            raise ValueError('Endurance evidence is not from this continuous stage')
        work = directory / 'work'
        receipts = json.loads((work / 'durable-wave-receipts.json').read_text())
        files = json.loads((work / 'durable-wave-files.json').read_text())
        waves = stage['expect']['duration_s'] // stage['expect']['interval_s']
        count = waves * stage['expect']['senders'] * stage['expect']['per_sender']
        nodes = receipts.get('nodes', [])
        if (receipts.get('complete') is not True or receipts.get('expected') != count
                or receipts.get('submitted') != count or len(nodes) != 8
                or {n['index'] for n in nodes} != set(range(8))
                or any(n.get('finalized') != count or n.get('same_receipts') is not True
                       or n.get('error') is not None for n in nodes)
                or files.get('complete') is not True or files.get('copies') != waves * 56
                or files.get('expected_copies') != waves * 56):
            raise ValueError('Incomplete durable receipts or files')
        paths = [work / f'audit-{n}.log' for n in range(7)] + [work / 'observer-audit.log']
        for path in paths:
            if not tail(path).rstrip().endswith('=== AUDIT PASS (failures=0) ==='):
                raise ValueError(f'Missing stopped-data audit: {path.name}')
        paths += [work / 'durable-wave-receipts.json', work / 'durable-wave-files.json',
                  work / 'barrier/shutdown-checkpoint.json']
        for path in paths:
            evidence[str(path.relative_to(directory))] = digest(path, pulse)
    else:
        for check in stage['checks']:
            path = confined(directory, check['path'])
            if check['type'] == 'text':
                found = False
                with path.open(errors='replace') as stream:
                    for line in stream:
                        found = found or re.search(check['pattern'], line) is not None
                        pulse()
                if not found:
                    raise ValueError(f'Missing result in {check["path"]}')
            elif check['type'] == 'json':
                actual = json.loads(path.read_text())
                for key, value in check['equals'].items():
                    if actual.get(key) != value:
                        raise ValueError(f'Unexpected {key} in {check["path"]}')
            else:
                raise ValueError('Unknown evidence check')
            evidence[check['path']] = digest(path, pulse)
    return evidence


def diagnostic(run, state, reason, stage=None, limit=18000):
    directory = run / stage if stage else run
    result = dict(state=state, reason=str(reason)[:1000], stage=stage, files=[], excerpts=[])
    errors = directory / 'errors.json'
    if errors.exists():
        result['errors'] = json.loads(errors.read_text())
    while len(json.dumps(result).encode()) > limit - 2000:
        groups = result.get('errors', {}).get('groups', [])
        first = result.get('errors', {}).get('first')
        if groups:
            groups.pop()
        elif first:
            result['errors']['first'] = first[:len(first) // 2]
        else:
            result['reason'] = result['reason'][:len(result['reason']) // 2]
    candidates = [directory / 'output.log', run / 'controller.log']
    candidates += sorted((directory / 'work').glob('*audit*.log'))
    candidates += sorted((directory / 'work').glob('node-*.log'))
    for path in candidates:
        if not path.is_file():
            continue
        item = dict(path=str(path.relative_to(run)), bytes=path.stat().st_size)
        if len(json.dumps(result).encode()) + len(json.dumps(item).encode()) > limit - 500:
            break
        result['files'].append(item)
        excerpt = dict(path=str(path.relative_to(run)), text=tail(path, 1500))
        if len(json.dumps(result).encode()) + len(json.dumps(excerpt).encode()) < limit - 2000:
            result['excerpts'].append(excerpt)
    return result
