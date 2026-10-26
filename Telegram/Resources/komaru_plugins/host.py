# This is the source code of KomaruGram for Desktop.
#
# Plugin host. KomaruGram starts it as a child process and talks to it with
# one JSON object per line on stdin and stdout. Everything a plugin does runs
# here, so a broken plugin can take the host down but never the client.
import argparse
import os
import sys


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--data", required=True)
    args = parser.parse_args()

    here = os.path.dirname(os.path.abspath(__file__))
    for path in (os.path.join(here, "compat"), here):
        if path not in sys.path:
            sys.path.insert(0, path)

    from komaru_runtime.manager import run
    run(os.path.abspath(args.data))


if __name__ == "__main__":
    main()
