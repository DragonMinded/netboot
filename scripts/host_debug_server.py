#!/usr/bin/env python3
import argparse
import sys
from netboot.web import spawn_app


def main() -> int:
    parser = argparse.ArgumentParser(description="A debug version of the Netboot web services.")
    parser.add_argument("-p", "--port", help="Port to listen on. Defaults to 80", type=int, default=80)
    parser.add_argument("-c", "--config", help="Core configuration. Defaults to config.yaml", type=str, default="config.yaml")
    args = parser.parse_args()

    # Set up global configuration, overriding config port for convenience
    app = spawn_app(args.config, debug=True)
    app.run(host='0.0.0.0', port=args.port, debug=True)


if __name__ == "__main__":
    sys.exit(main())
