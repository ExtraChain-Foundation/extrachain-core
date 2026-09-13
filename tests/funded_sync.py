import json
from pathlib import Path
import re
import shutil
import socket
import subprocess
import sys
import tempfile
import time


def free_port():
    with socket.socket() as listener:
        listener.bind(("127.0.0.1", 0))
        return listener.getsockname()[1]


def stop(process):
    if process.poll() is None:
        process.terminate()
        try:
            process.wait(timeout=3)
        except subprocess.TimeoutExpired:
            process.kill()
            process.wait(timeout=3)


def main():
    generator, node = [str(Path(value).resolve()) for value in sys.argv[1:3]]
    timeout = int(sys.argv[3]) if len(sys.argv) > 3 else 30
    if not 1 <= timeout <= 120:
        raise ValueError("Timeout must be in 1..120 seconds")
    work = Path(tempfile.mkdtemp(prefix="extrachain-funded-sync-"))
    processes = []
    success = False
    try:
        seed = work / "server" / "data"
        with (work / "generate.log").open("w") as output:
            subprocess.run([generator, "25000", str(seed)], stdout=output, stderr=output,
                           check=True, timeout=timeout)
        target = int(json.loads((seed / "dag" / "range").read_text())["last"])
        server_port = free_port()
        client_port = free_port()
        while client_port == server_port:
            client_port = free_port()
        with (work / "server.log").open("w") as output:
            server = subprocess.Popen([node, "serve", "data", str(server_port), "65536"],
                                      cwd=seed.parent, stdout=output, stderr=output)
        processes.append(server)
        deadline = time.monotonic() + timeout / 2
        publication = None
        while time.monotonic() < deadline and server.poll() is None:
            publication = re.search(r"DFS payload owner=(\w+) file_id=(\w+) size=65536",
                                    (work / "server.log").read_text())
            if publication:
                break
            time.sleep(0.1)
        if publication is None:
            raise RuntimeError("Server did not publish its test file")
        client_home = work / "client"
        client_home.mkdir()
        with (work / "client.log").open("w") as output:
            client = subprocess.Popen([node, "join", "data", "127.0.0.1", str(target),
                                       str(client_port), str(server_port), publication[1],
                                       "combined-network.bin", "65536"],
                                      cwd=client_home, stdout=output, stderr=output)
        processes.append(client)
        if client.wait(timeout=timeout) != 0:
            raise RuntimeError("Funded history synchronization failed")
        destination = client_home / "data"
        assert int(json.loads((destination / "dag" / "range").read_text())["last"]) == target
        relative = Path("dfs") / publication[1] / publication[2]
        assert (destination / relative).read_bytes() == (seed / relative).read_bytes()
        assert len(list((destination / "dag" / "packs").glob("*.pack"))) == 2
        print(f"Synced {target + 1} sections, two packs, and matching DFS payload")
        success = True
    finally:
        for process in reversed(processes):
            stop(process)
        if success:
            shutil.rmtree(work)
        else:
            print(f"Failure evidence: {work}", file=sys.stderr)
            for name in ("generate.log", "server.log", "client.log"):
                path = work / name
                if path.exists():
                    print(path.read_text()[-3000:], file=sys.stderr)


if __name__ == "__main__":
    main()
