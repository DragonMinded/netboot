import os
from setuptools import setup


if 'FULL_INSTALLATION' in os.environ:
    # We want to install this entire repo as an installation, so that we can
    # use it to run a netboot server.
    def requires(req: str) -> str:
        if "git+" not in req:
            return req
        if "#egg" not in req:
            raise Exception(f"Unknown egg package for {req}!")
        _, egg = req.split("#egg", 1)
        egg = egg.strip()
        if egg.startswith("="):
            egg = egg[1:].strip()
        return egg

    setup(
        name='netboot',
        version='0.2.1',
        description='Code and utilities for netbooting a Naomi/Triforce/Chihiro, including a full web and netboot server.',
        author='DragonMinded',
        license='Public Domain',
        packages=[
            # Core packages.
            'naomi',
            'naomi.settings',
            # Include default trojan.
            'homebrew.settingstrojan',
            # Web server package.
            'netboot',
            'netboot.web',
            'netboot.web.static',
            'netboot.web.templates',
        ],
        install_requires=[
            requires(req) for req in open('requirements.txt').read().split('\n') if len(req) > 0
        ],
        package_data={
            # Make sure to actually include the trojan data.
            "homebrew.settingstrojan": ["settingstrojan.bin"],
            "netboot.web.static": ["*.js", "*.css"],
            "netboot.web.templates": ["*.html"],
        },
    )
else:
    # We want to install only the parts of this repo useful as a third-party
    # package so that other code can depend on us.
    with open(os.path.join("naomi", "README.md"), "r", encoding="utf-8") as fh:
        long_description = fh.read()
    long_description += os.linesep
    with open(os.path.join("naomi", "settings", "README.md"), "r", encoding="utf-8") as fh:
        long_description += fh.read()
    long_description += os.linesep
    with open(os.path.join("naomi", "settings", "definitions", "README.md"), "r", encoding="utf-8") as fh:
        long_description += fh.read()

    setup(
        name='naomiutils',
        version='0.2.1',
        description='Code libraries for working with Naomi ROMs and EEPROMs.',
        long_description=long_description,
        long_description_content_type="text/markdown",
        author='DragonMinded',
        author_email='dragonminded@dragonminded.com',
        license='Public Domain',
        url='https://github.com/DragonMinded/netboot',
        packages=[
            # Package for 3rd party.
            'naomi',
            'naomi.settings',
            # Include settings definitions.
            'naomi.settings.definitions',
            # Include default trojan.
            'homebrew.settingstrojan',
        ],
        package_data={
            # Make sure mypy sees us as typed.
            "naomi": ["py.typed", "README.md"],
            "naomi.settings": ["py.typed", "README.md"],
            # Make sure to include all existing settings.
            "naomi.settings.definitions": ["*.settings", "README.md"],
            # Make sure to actually include the trojan data.
            "homebrew.settingstrojan": ["settingstrojan.bin"],
        },
        python_requires=">=3.6",
    )
