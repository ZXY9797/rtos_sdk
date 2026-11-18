"""Reject a demo_foc image whose control callbacks became unreachable."""
import argparse
import subprocess

REQUIRED = ("current_irq(", "joint_irq(", "slow_entry(", "app::start_control(")


def missing_callbacks(symbols):
    return [name for name in REQUIRED if name not in symbols]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("elf")
    parser.add_argument("--nm", required=True)
    args = parser.parse_args()
    symbols = subprocess.check_output([args.nm, "-C", args.elf], text=True)
    missing = missing_callbacks(symbols)
    if missing:
        raise SystemExit("Motor control callbacks missing: " + ", ".join(missing))
    print("Motor control callback retention checks passed")


if __name__ == "__main__":
    main()
