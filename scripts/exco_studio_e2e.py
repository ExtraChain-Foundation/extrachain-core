#!/usr/bin/env python3
"""Run live Contract Studio checks against an isolated Shadow network."""
import argparse
import base64
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import time

import msgpack

DOCUMENTS = ('activation-manifest', 'governance-policy', 'recovery-policy',
             'shadow-config', 'trust-anchor', 'validator-set')


class Stand:
    def __init__(self, args):
        self.args = args
        self.root = Path(__file__).resolve().parents[1]
        self.work = args.work.resolve()
        self.work.mkdir(parents=True, exist_ok=False)
        self.processes, self.streams = [], []
        self.log_paths = {}
        self.sequence = 0
        self.next_sample = 0
        self.environment = dict(os.environ, EXCO_CONTROL=str(self.work / 'control'),
                                EXC_SHADOW_COMMITTEE_SIZE='7', EXC_DFS_MODE='full')
        self.result = dict(status='RUNNING', languages={}, events=[])
        self.save()

    def save(self):
        (self.work / 'result.json').write_text(json.dumps(self.result, indent=2) + '\n')

    def event(self, name, **values):
        item = dict(event=name, monotonic_s=time.monotonic(), **values)
        self.result['events'].append(item)
        self.save()
        print(json.dumps(item), flush=True)

    def start(self, command, log, cwd=None, environment=None):
        stream = (self.work / log).open('ab')
        self.streams.append(stream)
        process = subprocess.Popen(list(map(str, command)), cwd=cwd,
                                   env=environment or self.environment,
                                   stdout=stream, stderr=subprocess.STDOUT)
        self.processes.append(process)
        self.log_paths[process.pid] = self.work / log
        return process

    def checked(self, command, log, environment=None):
        process = self.start(command, log, environment=environment)
        if process.wait(timeout=600) != 0:
            raise RuntimeError(f'Command failed: {log}')

    def sample(self):
        now = time.monotonic()
        if now < self.next_sample:
            return
        self.next_sample = now + 5
        nodes = []
        for process in self.processes:
            if process.poll() is not None:
                continue
            try:
                directory = Path('/proc') / str(process.pid)
                values = dict(line.split(':', 1) for line in (directory / 'status').read_text().splitlines())
                rss = int(values.get('VmRSS', '0 kB').split()[0]) * 1024
                fds = sum(1 for _ in (directory / 'fd').iterdir())
                if rss > 4 * 1024 ** 3 or fds > 4096:
                    raise RuntimeError(f'Process {process.pid} exceeded the resource limit')
                nodes.append(dict(pid=process.pid, rss_bytes=rss, fds=fds, threads=int(values['Threads']),
                                  log_bytes=self.log_paths[process.pid].stat().st_size))
            except FileNotFoundError:
                continue
        free = shutil.disk_usage(self.work).free
        if free < 10 * 1024 ** 3:
            raise RuntimeError('Free disk space fell below 10 GiB')
        with (self.work / 'resources.jsonl').open('a') as stream:
            stream.write(json.dumps(dict(monotonic_s=now, disk_free_bytes=free, nodes=nodes)) + '\n')

    def rpc(self, directory, operation, timeout=45, **values):
        directory.mkdir(parents=True, exist_ok=True)
        self.sequence += 1
        request = dict(sequence=self.sequence, operation=operation, **values)
        temporary = directory / 'request.tmp'
        temporary.write_text(json.dumps(request))
        temporary.rename(directory / 'request.json')
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            self.sample()
            path = directory / 'response.json'
            if path.exists():
                response = json.loads(path.read_text())
                if response.get('sequence') == self.sequence:
                    return response
            time.sleep(.1)
        raise TimeoutError(f'{directory.name}: {operation}')

    def node(self, index, operation, **values):
        return self.rpc(self.work / 'control' / str(index), operation, **values)

    def ui(self, operation, **values):
        response = self.rpc(self.bridge, operation, **values)
        if not response.get('ok'):
            raise RuntimeError(f'Studio {operation}: {response.get("error", response.get("status"))}')
        return response

    def wait(self, predicate, label, timeout=300):
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            self.sample()
            value = predicate()
            if value:
                return value
            time.sleep(.5)
        raise TimeoutError(label)

    def bootstrap(self):
        environment = dict(self.environment, EXTRACHAIN_TEST_BUILD=str(self.args.build),
                           EXTRACHAIN_TEST_WORK=str(self.work / 'bootstrap'),
                           EXTRACHAIN_TEST_DFS_BYTES='1024')
        self.checked(['bash', self.root / 'tests/multi_console_sync.sh', self.args.seed, '7', '27000'],
                     'bootstrap.log', environment)
        self.homes = [self.work / 'bootstrap' / ('server' if i == 0 else f'client{i}') / 'data'
                      for i in range(8)]
        for index, home in enumerate(self.homes):
            self.checked([self.args.build / 'extrachain-shadow-bundle', '--prepare', home,
                          'seed' if index == 0 else 'joiner'],
                         'prepare.log')
        self.checked([self.args.build / 'extrachain-shadow-bundle', self.homes[0], *self.homes[:7]],
                     'bundle.log')
        (self.homes[7] / 'consensus').mkdir(exist_ok=True)
        for document in DOCUMENTS:
            shutil.copy2(self.homes[0] / 'consensus' / f'{document}.msgpack',
                         self.homes[7] / 'consensus' / f'{document}.msgpack')
        self.barrier = self.work / 'barrier'
        self.barrier.mkdir()
        self.nodes = [self.start_node(index) for index in range(8)]
        self.wait(lambda: all((self.barrier / f'node-{i}').exists() for i in range(8)),
                  'committee start barrier', 90)
        (self.barrier / 'go').touch()
        self.wait(lambda: all((self.barrier / f'loaded-{i}').exists() for i in range(8)),
                  'committee startup')
        self.statuses = [self.node(i, 'status') for i in range(8)]
        assert all(x['ok'] for x in self.statuses), self.statuses
        self.wait(lambda: self.node(0, 'status').get('finalized', 0) >= 1, 'initial Shadow finality')
        self.event('network-ready', network=self.statuses[0]['network'])

    def start_node(self, index, resume=False):
        environment = dict(self.environment, EXC_BIND_IP=f'127.0.0.{index + 1}')
        if resume:
            environment['EXC_SHADOW_RESUME_WAVE'] = '0'
        return self.start([self.args.build / 'extrachain-node-run', 'committee', self.homes[index],
                           'seed' if index == 0 else 'joiner', str(index), str(17593 + index),
                           '17593', '8', '1' if index == 0 and not resume else '0', '18000', self.barrier, '1', '1'],
                          f'node-{index}.log', environment=environment)

    def publish(self):
        self.manifests = {}
        for language in ('assemblyscript', 'rust'):
            path = self.args.packages / f'{language}-0.3.0.tar.xz'
            package = self.node(0, 'publish-package', timeout=180, language=language,
                                path=str(path), version='0.3.0')
            assert package['ok'], package
            manifest = dict(schema=3, release_sequence=1, channel='stable', language=language,
                            version='0.3.0', compiler_version='0.28.20' if language == 'assemblyscript' else '1.97.0',
                            runtime_version='24.10.0' if language == 'assemblyscript' else '1.97.0',
                            rust_version='1.97.0', sdk_version='0.3.0', components_version='0.3.0',
                            catalog_version='0.3.0', template_version='0.3.0', contract_abi='4',
                            core_min='0.25.0', core_max='0.26.0', created=int(time.time()),
                            packages=[package['package']], revoked_versions=[])
            published = self.node(0, 'publish-manifest', manifest=json.dumps(manifest))
            assert published['ok'], published
            self.manifests[language] = manifest
            self.event('published', language=language, package=package['package'])
            (self.work / f'manifest-{language}.json').write_text(json.dumps(manifest, indent=2))
        next_snapshot = 0
        def manifests_ready():
            nonlocal next_snapshot
            available = all(self.node(1, 'manifest', language=language).get('ok')
                            for language in self.manifests)
            if not available and time.monotonic() >= next_snapshot:
                next_snapshot = time.monotonic() + 10
                states = [self.node(index, 'status') for index in (0, 1)]
                with (self.work / 'network-diagnostics.jsonl').open('a') as stream:
                    stream.write(json.dumps(dict(monotonic_s=time.monotonic(), states=states)) + '\n')
            return available
        self.wait(manifests_ready, 'toolchain manifest replication')
        for language, manifest in self.manifests.items():
            expected = manifest['packages'][0]
            received = self.wait(lambda: (value if (value := self.node(
                1, 'package', package=json.dumps(expected))).get('ok') else False),
                f'{language} package replication', 600)
            assert received['hash'] == expected['hash'] and received['size'] == expected['size'], received
            self.event('package-verified', language=language, hash=received['hash'], size=received['size'])

    def launch_ui(self, language, restart=False):
        self.bridge = self.work / f'ui-{language}'
        self.bridge.mkdir(exist_ok=True)
        data = self.bridge / 'xdg' / 'ExtraChain Foundation' / 'ExtraChain Client'
        data.mkdir(parents=True, exist_ok=True)
        if not restart:
            settings = json.loads((self.homes[1] / '.settings').read_text())
            settings.update(first_node='127.0.0.1', node_identifier=None, node_nonce=None, dag_mode=1)
            (data / '.settings').write_text(json.dumps(settings))
            (data / 'consensus').mkdir()
            for document in DOCUMENTS:
                shutil.copy2(self.homes[0] / 'consensus' / f'{document}.msgpack',
                             data / 'consensus' / f'{document}.msgpack')
        environment = dict(self.environment, EXCO_UI_BRIDGE=str(self.bridge), EXCO_UI_BIND='127.0.0.9',
                           XDG_DATA_HOME=str(self.bridge / 'xdg'), XDG_CONFIG_HOME=str(self.bridge / 'config'),
                           QT_QPA_PLATFORM='offscreen', QT_QUICK_BACKEND='software', QSG_RHI_BACKEND='software')
        self.client = self.start([self.args.client, '--viewport=1600x1100'], f'ui-{language}.log',
                                 environment=environment)
        self.wait(lambda: self.ui('snapshot')['core_ready'], 'Studio Core startup', 90)
        self.ui('type', name='login', value=f'exco-{language}')
        self.ui('type', name='password', value='exco-test-password')
        self.ui('click', text='Sign in' if restart else 'Create profile')
        self.wait(lambda: self.ui('snapshot')['authenticated'], 'Studio authentication', 60)
        self.wait(lambda: self.ui('snapshot')['connections'] > 0, 'Studio network connection', 90)
        self.ui('navigate')
        self.ui('click', text='Develop')
        if language == 'rust':
            self.ui('select', name='language', index=1)
        self.ui('screenshot')
        self.event('studio-ready', language=language, restart=restart)

    def idle(self, label, expected, timeout=300):
        def ready():
            state = self.ui('snapshot')
            if not state['busy']:
                if expected not in state['status']:
                    raise RuntimeError(f'{label}: {state["status"]}')
                return state
        return self.wait(ready, label, timeout)

    def operate(self, operation, contract='', method='', arguments=None, module=''):
        self.ui('click', text='Operate')
        self.ui('select', name='operation', index=operation)
        if operation != 2:
            self.ui('type', name='contractId', value=contract)
        else:
            self.ui('type', name='contractKind', value='studio-token')
        if operation < 2:
            self.ui('type', name='method', value=method)
        if module:
            self.ui('type', name='modulePath', value=module)
        self.ui('type', name='arguments', value=json.dumps(arguments if arguments is not None else []))
        self.ui('click', text=('Run query', 'Submit call', 'Deploy contract', 'Submit upgrade')[operation])
        if operation != 0:
            self.ui('click', text='OK')
        return self.ui('snapshot')

    def consistent(self, contract, revision=None, timeout=180):
        def check():
            states = [self.node(i, 'inspect', contract=contract) for i in range(8)]
            if not all(x.get('ok') for x in states):
                return False
            fields = ('module_hash', 'version', 'state_hash', 'revision', 'transaction_hash', 'state')
            if any(tuple(s[k] for k in fields) != tuple(states[0][k] for k in fields) for s in states):
                return False
            if revision is not None and states[0]['revision'] < revision:
                return False
            return states[0]
        return self.wait(check, f'contract {contract} convergence', timeout)

    def balances(self, state, receiver, owner_balance, receiver_balance, locked):
        encoded = base64.b64decode(state['state'])
        value = msgpack.unpackb(encoded, raw=False)
        ledger = value[6]
        balances = {key: int(amount) for key, amount in ledger[0]}
        locks = {key: int(amount) for key, amount in ledger[2]}
        assert balances.get(state['owner'], 0) == owner_balance, balances
        assert balances.get(receiver, 0) == receiver_balance, balances
        assert locks.get(state['owner'], 0) == locked, locks
        assert int(ledger[3]) == 1000 and sum(balances.values()) == 1000, ledger

    def language(self, language):
        self.launch_ui(language)
        if language == 'assemblyscript':
            self.checked(['tc', 'qdisc', 'add', 'dev', 'lo', 'root', 'netem', 'loss', '100%'],
                         'pending-install-fault.log')
            try:
                self.ui('click', text='Enable tools')
                self.wait(lambda: self.ui('snapshot')['busy'], 'pending toolchain download', 15)
                self.ui('quit')
                assert self.client.wait(timeout=15) == 0, 'Studio failed to close during download'
                self.event('pending-installation-close', status='PASS')
            finally:
                self.checked(['tc', 'qdisc', 'del', 'dev', 'lo', 'root'], 'pending-install-restore.log')
            self.launch_ui(language, restart=True)
        self.ui('click', text='Update')
        self.idle('first installation requires enable', 'enabled first', 90)
        self.ui('click', text='Enable tools')
        self.idle('toolchain installation', 'are ready', 600)
        self.ui('click', text='Fungible token')
        self.ui('type', name='projectName', value=f'exco_{language}')
        self.ui('click', text='Freeze last unit')
        self.ui('click', text='Generate Rust' if language == 'rust' else 'Generate AssemblyScript')
        generated = self.ui('snapshot')['source']
        assert generated, 'Source generation returned no source'
        self.ui('click', text='Build WebAssembly')
        built = self.idle('contract build', 'build completed', 300)
        first_module = str(self.work / f'{language}-v1.wasm')
        shutil.copy2(built['module'], first_module)
        self.event('built', language=language, module_sha256=hashlib.sha256(Path(first_module).read_bytes()).hexdigest())
        self.ui('screenshot')
        submitted = self.operate(2, arguments=[1000, []])
        assert 'submitted' in submitted['status'], submitted
        def deployed():
            self.ui('refresh')
            state = self.ui('snapshot')
            return state['catalog'][0]['contractId'] if state['catalog'] else False
        contract = self.wait(deployed, 'Studio confirmed contract catalog')
        initial = self.consistent(contract)
        self.event('deployed', language=language, contract=contract, state=initial)
        receiver = self.statuses[1]['wallet']
        self.balances(initial, receiver, 1000, 0, 0)
        response = self.operate(1, contract, 'transfer', [receiver, 100])
        assert 'submitted' in response['status'], response
        after_call = self.consistent(contract, initial['revision'] + 1)
        self.balances(after_call, receiver, 900, 100, 0)
        self.ui('click', text='Develop')
        self.ui('click', text='Freeze last unit')
        self.ui('click', text='Generate Rust' if language == 'rust' else 'Generate AssemblyScript')
        self.ui('click', text='Build WebAssembly')
        built = self.idle('upgrade build', 'build completed', 300)
        assert Path(built['module']).exists(), built
        shutil.copy2(built['module'], self.work / f'{language}-v2.wasm')
        response = self.operate(3, contract, arguments=[], module=built['module'])
        assert 'submitted' in response['status'], response
        upgraded = self.consistent(contract, after_call['revision'] + 1)
        assert upgraded['version'] == 2 and upgraded['module_hash'] != initial['module_hash'], upgraded
        response = self.operate(1, contract, 'transfer', [receiver, 899])
        assert 'submitted' in response['status'], response
        final = self.consistent(contract, upgraded['revision'] + 1)
        self.balances(final, receiver, 1, 999, 1)
        response = self.operate(1, contract, 'transfer', [receiver, 1])
        assert 'submitted' not in response['status'], response
        assert self.consistent(contract)['state_hash'] == final['state_hash']
        self.result['languages'][language] = dict(status='LIFECYCLE_PASS', contract=contract,
                                                 first_module=first_module, final=final)
        self.save()
        self.ui('screenshot')
        self.ui('quit')
        self.client.wait(timeout=60)
        self.launch_ui(language, restart=True)
        response = self.operate(0, contract, 'balance_of', [receiver])
        assert '999' in response['query_result'], response
        response = self.operate(1, contract, 'approve', [receiver, 0])
        assert 'submitted' in response['status'], response
        record = self.consistent(contract, final['revision'] + 1)
        self.result['languages'][language]['final'] = record
        self.ui('quit')
        self.client.wait(timeout=60)
        self.result['languages'][language]['status'] = 'PASS'
        self.event('language-pass', language=language)

    def recovery(self):
        for index, process in enumerate(self.nodes):
            process.terminate()
        for process in self.nodes:
            assert process.wait(timeout=45) == 0, 'Full node failed during clean shutdown'
        for index, home in enumerate(self.homes):
            self.checked([self.args.build / 'extrachain-dag-audit', home,
                          'seed' if index == 0 else 'joiner'], f'audit-{index}.log')
        self.nodes = [self.start_node(index, resume=True) for index in range(8)]
        for language, record in self.result['languages'].items():
            restored = self.consistent(record['contract'])
            assert restored['state_hash'] == record['final']['state_hash'], restored
            assert restored['revision'] == record['final']['revision'], restored
            self.event('normal-restart-verified', language=language, state=restored)
        self.nodes[7].terminate()
        assert self.nodes[7].wait(timeout=45) == 0
        original = self.homes[7]
        preserved = self.work / 'recovery-original'
        original.rename(preserved)
        shutil.copytree(preserved, original)
        heads = original / 'contract-heads'
        assert heads.is_dir() and any(heads.iterdir()), 'No derived heads exist to test replay'
        heads.rename(self.work / 'recovery-saved-heads')
        self.nodes[7] = self.start_node(7, resume=True)
        for language, record in self.result['languages'].items():
            restored = self.consistent(record['contract'])
            assert restored['state_hash'] == record['final']['state_hash'], restored
            assert restored['revision'] == record['final']['revision'], restored
            assert restored['checkpoint_revision'] < restored['revision'], restored
            self.event('checkpoint-replay-verified', language=language, state=restored)
        self.result['recovery'] = 'PASS'
        self.save()

    def negative_packages(self):
        for language, manifest in self.manifests.items():
            denied = self.node(1, 'publish-package-current', language=language,
                               path=str(self.args.packages / f'{language}-0.3.0.tar.xz'), version='0.3.0')
            assert not denied['ok'] and 'network address' in denied['error'], denied
            damaged = dict(manifest['packages'][0], hash='0' * 64)
            denied = self.node(1, 'package', package=json.dumps(damaged))
            assert not denied['ok'] and 'match' in denied['error'], denied
            invalid = dict(manifest, contract_abi='999')
            denied = self.node(0, 'publish-manifest', manifest=json.dumps(invalid))
            assert not denied['ok'], denied
            denied = self.node(0, 'publish-manifest', manifest=json.dumps(manifest))
            assert not denied['ok'] and 'increase' in denied['error'], denied
            self.event('negative-package-checks', language=language, status='PASS')
        self.result['negative_packages'] = 'PASS'
        self.save()

    def negative_updates(self):
        for language, record in self.result['languages'].items():
            other = 'rust' if language == 'assemblyscript' else 'assemblyscript'
            contract = record['contract']
            denied = self.node(1, 'upgrade', contract=contract,
                               path=str(self.work / f'{language}-v1.wasm'), arguments='[]')
            assert not denied['ok'], denied
            self.launch_ui(language, restart=True)
            response = self.operate(3, contract, arguments=[], module=str(self.work / f'{other}-v2.wasm'))
            assert 'submitted' not in response['status'], response
            self.event('language-change-rejected', language=language, detail=response['status'])
            response = self.operate(3, contract, arguments=['invalid'],
                                    module=str(self.work / f'{language}-v1.wasm'))
            assert 'submitted' not in response['status'], response
            current = self.consistent(contract)
            assert current['state_hash'] == record['final']['state_hash'], current
            assert current['revision'] == record['final']['revision'], current
            self.event('failed-migration-rejected', language=language, detail=response['status'])
            manifest = dict(self.manifests[language])
            active_files = list((self.bridge / 'xdg').rglob('installation.json'))
            assert len(active_files) == 1, active_files
            active = active_files[0]
            original_installation = active.read_bytes()
            for sequence, changes, expected in [
                (2, dict(core_min='99.0.0', core_max='99.0.0'), 'Core'),
                (3, dict(revoked_versions=['0.3.0']), 'revoked')]:
                candidate = dict(manifest, release_sequence=sequence, **changes)
                published = self.node(0, 'publish-manifest', manifest=json.dumps(candidate))
                assert published['ok'], published
                self.wait(lambda: expected.lower() in self.node(1, 'manifest', language=language)
                          .get('error', '').lower(), f'{language} negative manifest replication')
                self.ui('click', text='Develop')
                self.ui('click', text='Update')
                self.idle(f'{language} rejected toolchain update', expected, 90)
                assert active.read_bytes() == original_installation, 'A rejected update changed the active release'
            restored = dict(manifest, release_sequence=4)
            published = self.node(0, 'publish-manifest', manifest=json.dumps(restored))
            assert published['ok'], published
            self.wait(lambda: self.node(1, 'manifest', language=language).get('manifest', {})
                      .get('release_sequence') == 4, f'{language} restored manifest')
            self.ui('click', text='Update')
            if language == 'rust':
                self.wait(lambda: any(active.parent.glob('install-*')), 'installation staging directory', 60)
                self.client.kill()
                self.client.wait(timeout=30)
                assert active.read_bytes() == original_installation, 'Interrupted update replaced the active release'
                self.event('installation-interrupted', language=language, active_release_preserved=True)
                self.launch_ui(language, restart=True)
                self.ui('click', text='Update')
            self.idle('valid toolchain update', 'are current', 600)
            self.ui('quit')
            assert self.client.wait(timeout=60) == 0
            self.event('negative-update-checks', language=language, status='PASS')
        self.result['negative_updates'] = 'PASS'
        self.save()

    def close(self):
        for process in reversed(self.processes):
            if process.poll() is None:
                process.terminate()
        deadline = time.monotonic() + 30
        for process in reversed(self.processes):
            if process.poll() is None:
                try:
                    process.wait(timeout=max(.1, deadline - time.monotonic()))
                except subprocess.TimeoutExpired:
                    process.kill()
                    process.wait()
        for stream in self.streams:
            stream.close()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('work', 'build', 'seed', 'packages', 'client'):
        parser.add_argument(f'--{name}', type=Path, required=True)
    args = parser.parse_args()
    stand = Stand(args)
    try:
        stand.bootstrap()
        stand.publish()
        for language in ('assemblyscript', 'rust'):
            stand.language(language)
        stand.negative_packages()
        stand.negative_updates()
        stand.recovery()
        stand.result['status'] = 'PASS'
    except Exception as error:
        stand.result.update(status='FAIL', error=str(error))
        stand.event('failure', error=str(error))
        raise
    finally:
        stand.close()
        stand.save()


if __name__ == '__main__':
    main()
