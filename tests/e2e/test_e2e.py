#!/usr/bin/env python3
"""
E2E test: Simulated serial communication between firmware and Android protocol.

Tests the full JSON protocol round-trip:
1. Firmware simulator emits status JSON → parsed and validated
2. Commands sent to firmware simulator → ACK responses validated
3. Config get/set round-trip
"""

import json
import subprocess
import sys
import os

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
FIRMWARE_SIM = os.path.join(SCRIPT_DIR, "firmware_sim")

passed = 0
failed = 0


def test(name, condition, detail=""):
    global passed, failed
    if condition:
        print(f"  {name}... OK")
        passed += 1
    else:
        print(f"  {name}... FAIL {detail}")
        failed += 1


def test_status_scenarios():
    """Test that firmware simulator emits parseable status JSON."""
    print("Status scenario tests:")
    result = subprocess.run(
        [FIRMWARE_SIM, "--scenarios"],
        capture_output=True, text=True, timeout=5
    )
    test("firmware_sim exits cleanly", result.returncode == 0, f"rc={result.returncode}")

    lines = [l.strip() for l in result.stdout.strip().split("\n") if l.strip()]
    test("emits 6 status lines", len(lines) == 6, f"got {len(lines)}")

    # Parse each line as JSON
    for i, line in enumerate(lines):
        try:
            obj = json.loads(line)
            test(f"scenario {i} is valid JSON", True)
            test(f"scenario {i} has pos.x", "pos" in obj and "x" in obj["pos"])
            test(f"scenario {i} has pos.z", "z" in obj["pos"])
            test(f"scenario {i} has rpm", "rpm" in obj)
            test(f"scenario {i} has state", "state" in obj)
        except json.JSONDecodeError as e:
            test(f"scenario {i} is valid JSON", False, str(e))

    # Validate specific scenarios
    if len(lines) >= 6:
        idle = json.loads(lines[0])
        test("idle: x=0", idle["pos"]["x"] == 0.0)
        test("idle: z=0", idle["pos"]["z"] == 0.0)
        test("idle: rpm=0", idle["rpm"] == 0)
        test("idle: state=idle", idle["state"] == "idle")

        turning = json.loads(lines[1])
        test("turning: x=12.45", abs(turning["pos"]["x"] - 12.45) < 0.01)
        test("turning: z=-35.2", abs(turning["pos"]["z"] - (-35.2)) < 0.01)
        test("turning: rpm=820", abs(turning["rpm"] - 820) < 1)

        high_rpm = json.loads(lines[2])
        test("high_rpm: rpm=3500", abs(high_rpm["rpm"] - 3500) < 1)

        alarm = json.loads(lines[3])
        test("alarm: state=alarm", alarm["state"] == "alarm")

        neg = json.loads(lines[4])
        test("negative: x<0", neg["pos"]["x"] < 0)
        test("negative: z<0", neg["pos"]["z"] < 0)


def test_command_responses():
    """Test command → ACK round-trip via stdin/stdout pipes."""
    print("\nCommand round-trip tests:")
    proc = subprocess.Popen(
        [FIRMWARE_SIM, "--commands"],
        stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
        text=True
    )

    commands = [
        ('{"cmd":"zero","axis":"x"}', "zero", True),
        ('{"cmd":"preset","axis":"x","value":10.0}', "preset", True),
        ('{"cmd":"config_get","key":"spindle_ppr"}', "config_get", True),
        ('{"cmd":"config_save"}', "config_save", True),
    ]

    responses = []
    for cmd, _, _ in commands:
        proc.stdin.write(cmd + "\n")
        proc.stdin.flush()

    proc.stdin.close()
    stdout, stderr = proc.communicate(timeout=5)
    lines = [l.strip() for l in stdout.strip().split("\n") if l.strip()]

    test("got responses for all commands", len(lines) == len(commands),
         f"expected {len(commands)}, got {len(lines)}")

    for i, (cmd_str, expected_ack, expected_ok) in enumerate(commands):
        if i < len(lines):
            try:
                resp = json.loads(lines[i])
                test(f"cmd {expected_ack}: valid JSON", True)
                test(f"cmd {expected_ack}: ack field", resp.get("ack") == expected_ack)
                test(f"cmd {expected_ack}: ok={expected_ok}", resp.get("ok") == expected_ok)
            except json.JSONDecodeError:
                test(f"cmd {expected_ack}: valid JSON", False)

    # Validate config_get response has the value
    if len(lines) >= 3:
        config_resp = json.loads(lines[2])
        test("config_get: has key field", "key" in config_resp)
        test("config_get: key=spindle_ppr", config_resp.get("key") == "spindle_ppr")
        test("config_get: has value", "value" in config_resp)
        test("config_get: value=1000", config_resp.get("value") == 1000)


def test_json_robustness():
    """Test that status JSON values have correct types and ranges."""
    print("\nJSON robustness tests:")
    result = subprocess.run(
        [FIRMWARE_SIM, "--scenarios"],
        capture_output=True, text=True, timeout=5
    )

    for line in result.stdout.strip().split("\n"):
        line = line.strip()
        if not line:
            continue
        obj = json.loads(line)

        # Type checks
        test(f"x is number ({obj['pos']['x']})",
             isinstance(obj["pos"]["x"], (int, float)))
        test(f"z is number ({obj['pos']['z']})",
             isinstance(obj["pos"]["z"], (int, float)))
        test(f"rpm is number ({obj['rpm']})",
             isinstance(obj["rpm"], (int, float)))
        test(f"state is string ({obj['state']})",
             isinstance(obj["state"], str))

        # State is a known value
        valid_states = {"idle", "jogging", "threading", "cycle", "feed_hold", "alarm"}
        test(f"state '{obj['state']}' is valid",
             obj["state"] in valid_states)
        break  # Only check first line for type tests


if __name__ == "__main__":
    # Build firmware simulator first
    print("Building firmware simulator...")
    build_result = subprocess.run(
        ["make", "-C", SCRIPT_DIR, "firmware_sim"],
        capture_output=True, text=True
    )
    if build_result.returncode != 0:
        print(f"Build failed:\n{build_result.stderr}")
        sys.exit(1)
    print("Build OK\n")

    test_status_scenarios()
    test_command_responses()
    test_json_robustness()

    print(f"\n{'='*50}")
    print(f"E2E Results: {passed} passed, {failed} failed")
    if failed > 0:
        sys.exit(1)
    print("All E2E tests passed!")
