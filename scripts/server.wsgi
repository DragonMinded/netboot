#!/usr/bin/env python3
from netboot.web import spawn_app

# Set up global configuration, overriding config port for convenience
app = spawn_app("config.yaml")
