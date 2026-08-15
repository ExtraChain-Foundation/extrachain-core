#!/usr/bin/env python3
"""Run the Shadow committee and the real DAG plus ExDFS stand together."""

from __future__ import annotations

import argparse
import json
import re
import shlex
import subprocess
import time
from pathlib import Path


def parse_arguments() -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument("--build-dir", required=True, type=Path)
    parser.add_argument("--dag-dfs-command", required=True)
    parser.add_argument("--duration-seconds", type=int, default=24 * 60 * 60)
    parser.add_argument("--heights-per-cycle", type=int, default=10_000)
    parser.add_argument("--stand-shutdown-seconds", type=int, default=600)
    parser.add_argument("--report", type=Path, default=Path("shadow-soak-report.json"))
    arguments = parser.parse_args()
    if arguments.duration_seconds < 60:
        parser.error("duration-seconds must be at least 60")
    if arguments.heights_per_cycle < 100:
        parser.error("heights-per-cycle must be at least 100")
    return arguments


def require_executable(path: Path) -> Path:
    if not path.is_file():
        raise RuntimeError(f"required executable is absent: {path}")
    return path


def run_checked(command: list[str], timeout: int) -> str:
    result = subprocess.run(command, capture_output=True, text=True, timeout=timeout, check=False)
    if result.returncode != 0:
        raise RuntimeError(
            f"command failed with code {result.returncode}: {' '.join(command)}\n"
            f"{result.stdout}\n{result.stderr}"
        )
    return result.stdout


def main() -> int:
    arguments = parse_arguments()
    benchmark = require_executable(arguments.build_dir / "extrachain-consensus-benchmark")
    network_test = require_executable(arguments.build_dir / "extrachain-consensus-network-tests")
    recovery_test = require_executable(arguments.build_dir / "extrachain-shadow-recovery-tests")
    dag_dfs_command = shlex.split(arguments.dag_dfs_command)
    if not dag_dfs_command:
        raise RuntimeError("dag-dfs-command is empty")

    run_checked([str(network_test)], 180)
    run_checked([str(recovery_test)], 180)
    arguments.report.parent.mkdir(parents=True, exist_ok=True)
    dag_log_path = arguments.report.with_suffix(".dag-dfs.log")
    dag_log = dag_log_path.open("w", encoding="utf-8")
    dag_dfs = subprocess.Popen(dag_dfs_command, stdout=dag_log, stderr=subprocess.STDOUT, text=True)
    started = time.monotonic()
    deadline = started + arguments.duration_seconds
    cycles = 0
    heights = 0
    rates: list[float] = []
    failure = ""
    try:
        while time.monotonic() < deadline:
            if dag_dfs.poll() is not None:
                if time.monotonic() - started < arguments.duration_seconds * 0.95:
                    failure = f"DAG plus ExDFS stand exited early with code {dag_dfs.returncode}"
                elif dag_dfs.returncode != 0:
                    failure = f"DAG plus ExDFS stand failed with code {dag_dfs.returncode}"
                break
            output = run_checked(
                [str(benchmark), str(arguments.heights_per_cycle), "7"],
                600,
            )
            match = re.search(
                r"heights_s=([0-9.]+).*finalized=(\d+).*mobile_light=(\d+).*valid=yes",
                output,
            )
            if match is None or int(match.group(2)) != int(match.group(3)):
                failure = f"committee benchmark returned an invalid result: {output.strip()}"
                break
            rates.append(float(match.group(1)))
            heights += arguments.heights_per_cycle
            cycles += 1
    except (RuntimeError, subprocess.TimeoutExpired) as error:
        failure = str(error)
    finally:
        if not failure and dag_dfs.poll() is None:
            try:
                dag_dfs.wait(timeout=arguments.stand_shutdown_seconds)
            except subprocess.TimeoutExpired:
                failure = "DAG plus ExDFS stand did not finish its audits after the load window"
        if not failure and dag_dfs.poll() is not None and dag_dfs.returncode != 0:
            failure = f"DAG plus ExDFS stand failed with code {dag_dfs.returncode}"
        if dag_dfs.poll() is None:
            dag_dfs.terminate()
            try:
                dag_dfs.wait(timeout=45)
            except subprocess.TimeoutExpired:
                dag_dfs.kill()
                dag_dfs.wait(timeout=10)
        dag_log.close()
        with dag_log_path.open("rb") as log_input:
            log_input.seek(0, 2)
            log_input.seek(max(0, log_input.tell() - 32_768))
            dag_output_tail = log_input.read().decode("utf-8", errors="replace")

    elapsed = time.monotonic() - started
    if not failure and elapsed + 1 < arguments.duration_seconds * 0.95:
        failure = "soak ended before the requested duration"
    report = {
        "status": "failed" if failure else "passed",
        "failure": failure,
        "duration_seconds": elapsed,
        "committee_size": 7,
        "committee_cycles": cycles,
        "committee_heights": heights,
        "minimum_heights_per_second": min(rates) if rates else 0.0,
        "average_heights_per_second": sum(rates) / len(rates) if rates else 0.0,
        "dag_dfs_exit_code": dag_dfs.returncode,
        "dag_dfs_log": str(dag_log_path),
        "dag_dfs_output_tail": dag_output_tail,
    }
    arguments.report.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(report, indent=2))
    return 1 if failure else 0


if __name__ == "__main__":
    raise SystemExit(main())
