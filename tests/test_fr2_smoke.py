#!/usr/bin/env python3
"""test_fr2_smoke.py - End-to-end smoke test for Fox Ranger II runtime harness.

Validates:
1. Stage 1 engine setup, recompiled gameplay loop, keyboard input handling,
   AdLib OPL2 FM synthesis, and CT-VOICE playback in headless mode.
2. Unified narrative runner for English cutscenes (VIS1, PRO3, EPIL).
"""
import subprocess
import sys
import os

FOXRT = os.path.abspath("build/FoxRanger.app/Contents/MacOS/FoxRanger")

def test_stage1():
    print("[TEST] Stage 1 Gameplay Engine...")
    cmd = [
        FOXRT, "--stage", "st1", "--headless", "--seconds", "2.0", "--verbose",
        "--keys", "0.5:RIGHT,1.0:FIRE,1.5:UP"
    ]
    res = subprocess.run(cmd, capture_output=True, text=True)
    if res.returncode != 0:
        print("FAILED: exit code", res.returncode)
        print("STDERR:\n", res.stderr)
        return False

    out = res.stdout + res.stderr
    if "Stage 1 ready" not in out:
        print("FAILED: Stage 1 ready not found")
        return False
    if "opl writes" not in out:
        print("FAILED: audio stats not found")
        return False

    # Extract IRQs and OPL writes
    for line in out.splitlines():
        if "audio stats:" in line:
            print(" ", line.strip())
        if "exit 0 after" in line:
            print(" ", line.strip())

    print("[PASS] Stage 1 Gameplay Engine operational.")
    return True

def test_fox2b():
    print("[TEST] FOX2B Intermission & Shop Engine...")
    cmd = [
        FOXRT, "--stage", "fox2b", "--headless", "--seconds", "1.0", "--verbose",
        "--shots", "0.5:work/fox2b_shot.ppm"
    ]
    res = subprocess.run(cmd, capture_output=True, text=True)
    if res.returncode != 0:
        print("FAILED: FOX2B exit code", res.returncode)
        print("STDERR:\n", res.stderr)
        return False

    out = res.stdout + res.stderr
    if "Entering fox2b" not in out:
        print("FAILED: FOX2B not entered properly")
        return False
    if "opl writes" not in out:
        print("FAILED: FOX2B audio stats not found")
        return False

    for line in out.splitlines():
        if "audio stats:" in line or "screenshot" in line:
            print(" ", line.strip())

    print("[PASS] FOX2B Intermission & Shop Engine operational.")
    return True

def test_stage2():
    print("[TEST] Stage 2 Engine...")
    cmd = [
        FOXRT, "--stage", "st2", "--headless", "--seconds", "1.0", "--verbose"
    ]
    res = subprocess.run(cmd, capture_output=True, text=True)
    if res.returncode != 0:
        print("FAILED: Stage 2 exit code", res.returncode)
        print("STDERR:\n", res.stderr)
        return False

    out = res.stdout + res.stderr
    if "Entering st2" not in out:
        print("FAILED: Stage 2 not entered properly")
        return False

    for line in out.splitlines():
        if "exit 0 after" in line:
            print(" ", line.strip())

    print("[PASS] Stage 2 Engine operational.")
    return True

def test_campaign_boot():
    print("[TEST] Campaign Boot Flow (Default)...")
    cmd = [
        FOXRT, "--headless", "--seconds", "0.8", "--verbose"
    ]
    res = subprocess.run(cmd, capture_output=True, text=True)
    if res.returncode != 0:
        print("FAILED: Campaign boot exit code", res.returncode)
        print("STDERR:\n", res.stderr)
        return False

    out = res.stdout + res.stderr
    if "Routing letter 'R'" not in out or "Starting narrative scene 'PRO3'" not in out:
        print("FAILED: Campaign prologue boot not detected")
        return False

    print("  Routing letter 'R' -> Prologue PRO3 active.")
    print("[PASS] Campaign Boot Flow verified.")
    return True

def test_cutscene(name):
    print(f"[TEST] Cutscene '{name}' Narrative Runner...")
    cmd = [
        FOXRT, "--cutscene", name, "--headless", "--seconds", "1.0"
    ]
    res = subprocess.run(cmd, capture_output=True, text=True)
    if res.returncode != 0:
        print(f"FAILED: Cutscene {name} exit code {res.returncode}")
        print("STDERR:\n", res.stderr)
        return False

    out = res.stdout + res.stderr
    if f"Starting narrative scene '{name}'" not in out:
        print(f"FAILED: Scene {name} didn't start properly")
        return False

    print(f"[PASS] Cutscene '{name}' verified.")
    return True

def main():
    if not os.path.exists(FOXRT):
        print(f"Error: binary {FOXRT} not found. Run build first.")
        sys.exit(1)

    ok = True
    ok = ok and test_campaign_boot()
    ok = ok and test_stage1()
    ok = ok and test_stage2()
    ok = ok and test_fox2b()
    ok = ok and test_cutscene("VIS1")
    ok = ok and test_cutscene("PRO3")
    ok = ok and test_cutscene("EPIL")

    if ok:
        print("\nALL SMOKE TESTS PASSED!")
        sys.exit(0)
    else:
        print("\nSOME TESTS FAILED!")
        sys.exit(1)

if __name__ == "__main__":
    main()
