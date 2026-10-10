#!/usr/bin/env python3
"""Run frozen validation jobs under systemd, without a model polling loop."""
import argparse
from datetime import datetime, timezone
import getpass
import grp
import json
import os
from pathlib import Path
import platform
import re
import shlex
import shutil
import socket
import subprocess
import sys
import time

from stand_evidence import (TERMINAL, Errors, Lines, Progress, confined, diagnostic, digest,
                            fingerprint, load_progress, tail, verify_stage, write_json)


HERE = Path(__file__).resolve().parent
NAME = re.compile(r'[a-z0-9][a-z0-9-]{0,47}\Z')


def now():
    return datetime.now(timezone.utc).isoformat()


def notify(message):
    address = os.environ.get('NOTIFY_SOCKET')
    if address:
        if address.startswith('@'):
            address = '\0' + address[1:]
        with socket.socket(socket.AF_UNIX, socket.SOCK_DGRAM) as stream:
            stream.sendto(message.encode(), address)


def git(source, *args):
    return subprocess.check_output(['git', '-C', source, *args], text=True).strip()


def validate_manifest(value):
    if value.get('version') != 1 or not NAME.fullmatch(value.get('name', '')):
        raise ValueError('Invalid manifest version or name')
    if value.get('purpose') not in ('validation', 'acceptance'):
        raise ValueError('Declare validation or acceptance purpose')
    if not Path(value['root']).is_absolute() or not Path(value['source']['path']).is_absolute():
        raise ValueError('Source and run roots must be absolute')
    stages = value['stages']
    if not 1 <= len(stages) <= 32:
        raise ValueError('Expected 1..32 stages')
    names = [s['name'] for s in stages]
    if len(set(names)) != len(names) or any(not NAME.fullmatch(n) for n in names):
        raise ValueError('Stage names must be unique and bounded')
    if not set(value['required_stages']) <= set(names) or not value['required_stages']:
        raise ValueError('Required stages are missing')
    if not value.get('inputs') or not 0 < value['min_free_bytes']:
        raise ValueError('Frozen inputs and a disk limit are required')
    for stage in stages:
        if stage['kind'] not in ('command', 'endurance') or not 1 <= stage['timeout_s'] <= 86400:
            raise ValueError('Invalid stage kind or deadline')
        if (not isinstance(stage['command'], list) or not stage['command']
                or not all(isinstance(s, str) and '\0' not in s for s in stage['command'])):
            raise ValueError('Commands must be argument arrays')
        if stage['kind'] == 'endurance':
            e = stage['expect']
            if (not 60 <= e['interval_s'] <= 3600 or e['duration_s'] % e['interval_s']
                    or not 4 <= e['duration_s'] // e['interval_s'] <= 256
                    or not 1 <= e['senders'] <= 6 or not 1 <= e['per_sender'] <= 64
                    or not 1 <= e['file_bytes'] <= 16777216):
                raise ValueError('Invalid endurance expectations')
            if stage.get('cacheable') or stage.get('reuse_from'):
                raise ValueError('Endurance and final checks cannot be reused')
        elif not stage.get('checks'):
            raise ValueError('A successful exit alone does not establish a result')
        for check in stage.get('checks', []):
            confined('/stage', check['path'])
            if check['type'] == 'text':
                re.compile(check['pattern'])
            elif check['type'] == 'json':
                if not check.get('equals'):
                    raise ValueError('JSON checks need explicit expected fields')
            else:
                raise ValueError('Unknown result check')
    if value['purpose'] == 'acceptance':
        required = {'preview', 'extended', 'backoff', 'large', 'legacy', 'history',
                    'console', 'release-linux', 'release-macos', 'sanitizers-macos',
                    'endurance', 'final-short'}
        if not required <= set(value['required_stages']):
            raise ValueError('Acceptance must include every prerequisite')
        long = stages[names.index('endurance')]
        short = stages[names.index('final-short')]
        if (long['kind'] != 'endurance' or short['kind'] != 'endurance'
                or long['expect']['duration_s'] != 21600 or long['expect']['interval_s'] != 300
                or long['expect']['senders'] != 6 or long['expect']['per_sender'] != 48
                or short['expect']['duration_s'] < 600
                or names.index('final-short') != len(names) - 1
                or names.index('endurance') != len(names) - 2):
            raise ValueError('Acceptance duration, workload, or stage order differs')


def prepare(template, destination):
    value = json.loads(template.read_text())
    validate_manifest(value)
    if destination.exists():
        raise ValueError('Use a new manifest path')
    source = value['source']
    if git(source['path'], 'status', '--porcelain'):
        raise ValueError('Source must be clean before freezing')
    source['revision'] = git(source['path'], 'rev-parse', 'HEAD')
    source['tree'] = git(source['path'], 'rev-parse', 'HEAD^{tree}')
    value['platform'] = dict(system=platform.system(), machine=platform.machine(),
                             release=platform.release(), python=sys.version)
    value['controller'] = {p.name: digest(p) for p in (HERE / 'stand.py', HERE / 'stand_evidence.py')}
    for entry in value['inputs']:
        path = Path(entry['path']).absolute()
        entry.update(path=str(path), resolved_path=str(path.resolve(strict=True)), sha256=digest(path))
    environment = dict(HOME=str(Path.home()), USER=getpass.getuser(),
                       PATH='/usr/sbin:/usr/bin:/sbin:/bin', LANG='C.UTF-8', LC_ALL='C.UTF-8')
    environment.update(value.get('environment', {}))
    if any(k.startswith('EXC_') or k.startswith('EXTRACHAIN_') for k in environment):
        raise ValueError('Set workload environment in individual stages, not globally')
    value['environment'] = environment
    value['prepared_at'] = now()
    value['fingerprint'] = fingerprint({k: v for k, v in value.items() if k not in ('prepared_at', 'fingerprint')})
    write_json(destination, value)
    return dict(manifest=str(destination), fingerprint=value['fingerprint'])


def verify_inputs(value, pulse=lambda: None):
    validate_manifest(value)
    if fingerprint({k: v for k, v in value.items() if k not in ('prepared_at', 'fingerprint')}) != value['fingerprint']:
        raise ValueError('Manifest changed after preparation')
    source = value['source']
    if (git(source['path'], 'rev-parse', 'HEAD') != source['revision']
            or git(source['path'], 'rev-parse', 'HEAD^{tree}') != source['tree']
            or git(source['path'], 'status', '--porcelain')):
        raise ValueError('Frozen source changed')
    actual = dict(system=platform.system(), machine=platform.machine(),
                  release=platform.release(), python=sys.version)
    if actual != value['platform']:
        raise ValueError('Execution platform differs from prepared inputs')
    for name, expected in value['controller'].items():
        if digest(HERE / name, pulse) != expected:
            raise ValueError('Controller changed')
    for entry in value['inputs']:
        if str(Path(entry['path']).resolve(strict=True)) != entry['resolved_path']:
            raise ValueError(f'Input link changed: {entry["path"]}')
        if digest(entry['path'], pulse) != entry['sha256']:
            raise ValueError(f'Input changed: {entry["path"]}')


def stage_key(value, stage):
    return fingerprint(dict(source=value['source'], inputs=value['inputs'],
                            environment=value['environment'], platform=value['platform'],
                            controller=value['controller'],
                            stage={k: v for k, v in stage.items() if k != 'reuse_from'}))


def reusable(value, stage, pulse=lambda: None):
    if not stage.get('reuse_from'):
        return None
    if not stage.get('cacheable') or stage['kind'] != 'command':
        raise ValueError('This stage cannot reuse a result')
    directory = Path(stage['reuse_from']).resolve()
    if status_view(directory.parent)['state'] != 'PASS':
        raise ValueError('Cached run did not finish successfully')
    result = json.loads((directory / 'result.json').read_text())
    if result['state'] != 'PASS' or result['key'] != stage_key(value, stage):
        raise ValueError('Cached result has different inputs or is incomplete')
    if not result.get('evidence'):
        raise ValueError('Cached result has no evidence')
    for name, expected in result['evidence'].items():
        if digest(confined(directory, name), pulse) != expected:
            raise ValueError('Cached evidence changed')
    return result


def append_event(run, event, **fields):
    with (run / 'events.jsonl').open('a') as stream:
        stream.write(json.dumps(dict(time=now(), event=event, **fields), allow_nan=False) + '\n')


def render_report(run):
    manifest = json.loads((run / 'manifest.json').read_text())
    status = json.loads((run / 'status.json').read_text())
    lines = [f"Run {run.name}: {status['state']}", '',
             f"Purpose: {manifest['purpose']}. Source: {manifest['source']['revision']}.",
             f"Reason: {status.get('reason', 'None')}", '', '| Stage | Result | Seconds |',
             '| --- | --- | --- |']
    for stage in manifest['stages']:
        path = run / stage['name'] / 'result.json'
        result = json.loads(path.read_text()) if path.exists() else {'state': 'NOT_RUN'}
        lines.append(f"| {stage['name']} | {result['state']} | {result.get('elapsed_s', '')} |")
    lines += ['', 'PASS requires every declared stage and its evidence. Interrupted runs do not count '
              'toward continuous acceptance.', '', 'Full events, stage logs, and result hashes remain '
              'in this run directory.']
    report = '\n'.join(lines) + '\n'
    (run / 'report.md').write_text(report)
    (run / 'tracker-update.md').write_text(report)
    (run / 'pr-update.md').write_text(report)


def finish(run, state, reason, stage=None):
    status = json.loads((run / 'status.json').read_text())
    if status['state'] in TERMINAL:
        return
    status.update(state=state, reason=str(reason)[:1000], finished_at=now(), heartbeat=time.time())
    write_json(run / 'status.json', status)
    if state != 'PASS':
        write_json(run / 'diagnostic.json', diagnostic(run, state, reason, stage))
    append_event(run, 'finished', state=state, reason=status['reason'])
    render_report(run)


def finalize(run):
    status = json.loads((run / 'status.json').read_text())
    service = os.environ.get('SERVICE_RESULT', 'unknown')
    write_json(run / 'service-result.json', dict(result=service, exit_code=os.environ.get('EXIT_CODE'),
                                               exit_status=os.environ.get('EXIT_STATUS'), finished_at=now()))
    if status['state'] == 'FINALIZING' and service == 'success' and not (run / 'stop-request.json').exists():
        finish(run, 'PASS', 'All required stages and evidence passed; service stopped')
    elif status['state'] not in TERMINAL:
        interrupted = (run / 'stop-request.json').exists()
        finish(run, 'INTERRUPTED' if interrupted else 'INFRA_ERROR',
               'User requested stop' if interrupted else f'Controller terminated: {service}', status.get('stage'))
    elif status['state'] == 'PASS' and service != 'success':
        status['state'] = 'FINALIZING'
        write_json(run / 'status.json', status)
        finish(run, 'INFRA_ERROR', f'Service cleanup failed: {service}', status.get('stage'))


def run_worker(run):
    value = json.loads((run / 'manifest.json').read_text())
    status = json.loads((run / 'status.json').read_text())
    current = None
    errors = None
    last_pulse = 0

    def pulse(force=False):
        nonlocal last_pulse
        if not force and time.monotonic() - last_pulse < 5:
            return
        last_pulse = time.monotonic()
        status.update(heartbeat=time.time(), updated_at=now(), disk_free_bytes=shutil.disk_usage(run).free)
        if errors is not None:
            write_json(run / current / 'errors.json', errors.summary())
        write_json(run / 'status.json', status)
        notify('WATCHDOG=1\nSTATUS=' + str(status.get('stage', 'preflight')))
        if status['disk_free_bytes'] < value.get('runtime_min_free_bytes', 10 * 1024 ** 3):
            raise OSError('Free disk space is below the runtime limit')

    notify('READY=1')
    try:
        status['state'] = 'PREFLIGHT'
        pulse(True)
        verify_inputs(value, pulse)
        if shutil.disk_usage(run).free < value['min_free_bytes']:
            raise OSError('Insufficient free space for this profile')
        for stage in value['stages']:
            current = stage['name']
            errors = None
            directory = run / current
            directory.mkdir()
            status.update(state='RUNNING', stage=current, progress={})
            pulse(True)
            append_event(run, 'stage-start', stage=current)
            key = stage_key(value, stage)
            cached = reusable(value, stage, pulse)
            if cached:
                shutil.copy2(Path(stage['reuse_from']) / 'result.json', directory / 'reused-result.json')
                write_json(directory / 'result.json', dict(state='PASS', key=key, elapsed_s=0,
                                                          reused_from=stage['reuse_from'], evidence={}))
                append_event(run, 'stage-finished', stage=current, state='PASS', reused=True)
                continue
            replace = lambda s: s.replace('{run}', str(run)).replace('{stage}', str(directory))
            command = [replace(s) for s in stage['command']]
            environment = dict(value['environment'])
            environment.update({k: replace(v) for k, v in stage.get('environment', {}).items()})
            started = time.monotonic()
            progress = Progress()
            errors = Errors()
            output = Lines(directory / 'output.log')
            resources = Lines(directory / 'work/resources.jsonl')

            def read_new():
                for line in output.read():
                    errors.feed(line)
                    if line.startswith('{'):
                        try:
                            event = json.loads(line)
                        except ValueError:
                            if stage['kind'] == 'endurance':
                                raise
                            continue
                        if isinstance(event, dict) and stage['kind'] == 'endurance':
                            progress.event(event)
                for line in resources.read():
                    progress.resource(json.loads(line))
                status['progress'] = progress.summary()
                status['stage_elapsed_s'] = round(time.monotonic() - started, 1)

            with (directory / 'output.log').open('xb') as log:
                process = subprocess.Popen(command, cwd=replace(stage.get('cwd', value['source']['path'])),
                                           env=environment, stdout=log, stderr=subprocess.STDOUT,
                                           stdin=subprocess.DEVNULL)
                status['child_pid'] = process.pid
                while process.poll() is None:
                    read_new()
                    pulse()
                    if time.monotonic() - started > stage['timeout_s']:
                        raise TimeoutError(f'Stage {current} exceeded its deadline')
                    time.sleep(0.5)
            while output.offset < output.path.stat().st_size:
                read_new()
                pulse()
            read_new()
            result = dict(key=key, returncode=process.returncode,
                          elapsed_s=round(time.monotonic() - started, 3))
            write_json(directory / 'errors.json', errors.summary())
            if process.returncode != 0:
                result.update(state='FAIL', evidence={'output.log': digest(output.path, pulse)})
                write_json(directory / 'result.json', result)
                finish(run, 'FAIL', (progress.failed or {}).get('error', f'{current} exited {process.returncode}'), current)
                return 1
            if stage['kind'] == 'endurance' and (output.pending or resources.pending):
                raise ValueError('Incomplete final evidence record')
            evidence = verify_stage(stage, directory, progress, pulse, started)
            result.update(state='PASS', evidence=evidence)
            write_json(directory / 'result.json', result)
            append_event(run, 'stage-finished', stage=current, state='PASS')
            pulse(True)
        verify_inputs(value, pulse)
        status['state'] = 'FINALIZING'
        pulse(True)
        return 0
    except (OSError, ValueError, KeyError, TimeoutError, subprocess.SubprocessError) as error:
        if errors is not None:
            write_json(run / current / 'errors.json', errors.summary())
        if current and (run / current).is_dir() and not (run / current / 'result.json').exists():
            write_json(run / current / 'result.json', dict(state='INFRA_ERROR', reason=str(error)[:1000]))
        finish(run, 'INFRA_ERROR', error, current)
        return 2


def start(manifest_path):
    value = json.loads(manifest_path.read_text())
    validate_manifest(value)
    if platform.system() != 'Linux':
        raise ValueError('Run this command on the Linux stand host')
    verify_inputs(value)
    root = Path(value['root']).resolve()
    root.mkdir(parents=True, exist_ok=True)
    run = root / (value['name'] + '-' + datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%S')
                  + '-' + os.urandom(3).hex())
    run.mkdir(mode=0o700)
    write_json(run / 'manifest.json', value)
    unit = 'extrachain-stand-' + run.name
    write_json(run / 'status.json', dict(state='QUEUED', run_id=run.name, unit=unit,
                                       started_at=now(), heartbeat=time.time(), stage=None))
    command = ['sudo', '-n', 'systemd-run', '--quiet', '--unit=' + unit,
               '--uid=' + getpass.getuser(), '--gid=' + grp.getgrgid(os.getgid()).gr_name,
               '--property=Type=notify', '--property=NotifyAccess=main',
               '--property=WatchdogSec=30', '--property=TimeoutStartSec=30',
               '--property=LimitCORE=0',
               '--property=TimeoutStopSec=45', '--property=KillMode=control-group',
               '--property=RuntimeMaxSec=' + str(sum(s['timeout_s'] for s in value['stages']) + 600),
               '--property=WorkingDirectory=' + str(run),
               '--property=StandardOutput=append:' + str(run / 'controller.log'),
               '--property=StandardError=inherit',
               '--property=ExecStopPost=' + shlex.join([sys.executable, str(HERE / 'stand.py'), '_finalize', str(run)]),
               sys.executable, str(HERE / 'stand.py'), '_run', str(run)]
    result = subprocess.run(command, capture_output=True, text=True)
    if result.returncode:
        finish(run, 'INFRA_ERROR', result.stderr)
        raise ValueError(f'Failed to start {run}: {result.stderr.strip()}')
    return dict(run_id=run.name, directory=str(run), state='QUEUED', unit=unit,
                report=str(run / 'report.md'), events=str(run / 'events.jsonl'))


def status_view(run):
    status = json.loads((run / 'status.json').read_text())
    status['heartbeat_age_s'] = round(time.time() - status['heartbeat'], 1)
    if status['state'] == 'PASS' and not (run / 'service-result.json').exists():
        status['state'] = 'FINALIZING'
    if status['state'] not in TERMINAL and status['heartbeat_age_s'] > 35:
        status['stale'] = True
    return status


def stop(run):
    status = status_view(run)
    if status['state'] in TERMINAL and (run / 'service-result.json').exists():
        return status
    write_json(run / 'stop-request.json', dict(requested_at=now(), reason='User requested stop'))
    subprocess.run(['sudo', '-n', 'systemctl', 'stop', status['unit']], check=True)
    return status_view(run)


def archive(run, remove):
    status = status_view(run)
    if status['state'] not in TERMINAL:
        raise ValueError('Only terminal jobs can be archived')
    active = subprocess.run(['systemctl', 'is-active', status['unit']], capture_output=True, text=True)
    if active.stdout.strip() not in ('inactive', 'failed', 'unknown'):
        raise ValueError('The service has not stopped')
    destination = run.with_suffix('.tar.zst')
    if destination.exists():
        raise ValueError('Archive already exists')
    subprocess.run(['tar', '--sparse', '-I', 'zstd -T2 -6', '-cf', str(destination),
                    '-C', str(run.parent), run.name], check=True)
    subprocess.run(['zstd', '-q', '-t', str(destination)], check=True)
    subprocess.run(['tar', '-I', 'zstd', '--compare', '-f', str(destination), '-C', str(run.parent)], check=True)
    record = dict(archive=str(destination), sha256=digest(destination), state=status['state'],
                  verification=['zstd integrity', 'full tar comparison'], source_removed=False)
    metadata = run.with_suffix('.archive.json')
    write_json(metadata, record)
    if remove:
        shutil.rmtree(run)
        record['source_removed'] = True
        write_json(metadata, record)
    return record


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest='command', required=True)
    prepare_parser = sub.add_parser('prepare')
    prepare_parser.add_argument('template', type=Path)
    prepare_parser.add_argument('--output', type=Path, required=True)
    start_parser = sub.add_parser('start')
    start_parser.add_argument('manifest', type=Path)
    for name in ('status', 'stop', 'report', 'diagnose', 'archive', '_run', '_finalize'):
        child = sub.add_parser(name)
        child.add_argument('run', type=Path)
        if name == 'diagnose':
            child.add_argument('--file')
            child.add_argument('--offset', type=int, default=0)
            child.add_argument('--limit', type=int, default=8192)
        if name == 'archive':
            child.add_argument('--remove-source', action='store_true')
    replay = sub.add_parser('replay')
    replay.add_argument('log', type=Path)
    replay.add_argument('--expect', type=Path, required=True)
    args = parser.parse_args()
    try:
        if args.command == 'prepare':
            result = prepare(args.template, args.output)
        elif args.command == 'start':
            result = start(args.manifest)
        elif args.command == 'replay':
            progress = load_progress(args.log)
            result = dict(state=progress.verdict(json.loads(args.expect.read_text())),
                          last_event=(progress.last or {}).get('event'), waves=len(progress.waves),
                          scope='event replay only; no acceptance or durable audit claim')
        else:
            run = args.run.resolve(strict=True)
            if args.command == '_run':
                return run_worker(run)
            if args.command == '_finalize':
                finalize(run)
                return 0
            if args.command == 'status':
                result = status_view(run)
            elif args.command == 'stop':
                result = stop(run)
            elif args.command == 'archive':
                result = archive(run, args.remove_source)
            elif args.command == 'report':
                print((run / 'report.md').read_text(), end='')
                return 0
            elif args.file:
                if not 0 <= args.offset or not 1 <= args.limit <= 16384:
                    raise ValueError('Invalid bounded read')
                path = confined(run, args.file)
                with path.open('rb') as stream:
                    stream.seek(args.offset)
                    data = stream.read(args.limit)
                result = dict(path=args.file, offset=args.offset, next_offset=args.offset + len(data),
                              text=data.decode(errors='replace'))
            else:
                result = json.loads((run / 'diagnostic.json').read_text())
        print(json.dumps(result, sort_keys=True, allow_nan=False))
        return 0
    except (OSError, ValueError, KeyError, subprocess.SubprocessError) as error:
        print(json.dumps(dict(error=str(error)[:1500])), file=sys.stderr)
        return 2


if __name__ == '__main__':
    raise SystemExit(main())
