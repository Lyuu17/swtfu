"""Experimental 60 FPS patch for the supported Steam build of SWTFU.

Launch the game normally through Steam first, then run:
    python -m swtfu.fps --apply
    python -m swtfu.fps --inspect
    python -m swtfu.fps --restore

Changes the running game's native setting and the investigated cinematic/DMM
timing sites. Restarting restores defaults. No executable, DLL, asset, or save
file is modified.
Runtime frame-pacing and gameplay validation is still pending.
"""
import argparse
import json

from swtfu.game import GameMemory, find_game
from swtfu.fps.profile import set_profile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    actions = parser.add_mutually_exclusive_group(required=True)
    actions.add_argument("--apply", action="store_true")
    actions.add_argument("--restore", action="store_true")
    actions.add_argument("--inspect", action="store_true")
    parser.add_argument("--pid", type=int)
    args = parser.parse_args()
    game = None
    try:
        game = GameMemory(args.pid or find_game(), writable=not args.inspect)
        before = game.inspect()
        changed = False
        if args.apply:
            changed = set_profile(game, 60)
        elif args.restore:
            changed = set_profile(game, 30)
        result = {"status": "experimental; runtime validation pending", "changed": changed,
                  "before": before, "after": game.inspect()}
        print(json.dumps(result, indent=2))
    except (OSError, RuntimeError) as error:
        parser.exit(1, f"{error}\n")
    finally:
        if game:
            game.close()


if __name__ == "__main__":
    main()
