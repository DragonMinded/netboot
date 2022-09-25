import os
from setuptools import setup


VERSION = "0.5.4"


if 'FULL_INSTALLATION' in os.environ:
    # We want to install this entire repo as an installation, so that we can
    # use it to run a netboot server.
    setup(
        name='netboot',
        version=VERSION,
        description='Code and utilities for netbooting a Naomi/Triforce/Chihiro, including a full web and netboot server.',
        author='DragonMinded',
        license='Public Domain',
        packages=[
            # Core packages.
            'netdimm',
            'naomi',
            'naomi.settings',
            'settings',
            # Include default trojan.
            'homebrew.settingstrojan',
            # Web server package.
            'netboot',
            'netboot.web',
            'netboot.web.static',
            'netboot.web.templates',
        ],
        install_requires=[
            req for req in open('requirements.txt').read().split('\n') if len(req) > 0
        ],
        package_data={
            # Make sure to actually include the trojan data.
            "homebrew.settingstrojan": ["settingstrojan.bin"],
            "netboot.web.static": ["*.js", "*.css", "*.gif"],
            "netboot.web.templates": ["*.html"],
        },
        python_requires=">=3.6",
    )
elif 'NAOMI_INSTALLATION' in os.environ:
    # We want to install only the naomi parts of this repo useful as a third-party
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
        version=VERSION,
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
            'settings',
            # Include settings definitions.
            'naomi.settings.definitions',
            # Include default trojan.
            'homebrew.settingstrojan',
        ],
        package_data={
            # Make sure mypy sees us as typed.
            "naomi": ["py.typed", "README.md"],
            "naomi.settings": ["py.typed", "README.md"],
            "settings": ["py.typed", "README.md"],
            # Make sure to include all existing settings.
            "naomi.settings.definitions": ["*.settings", "README.md"],
            # Make sure to actually include the trojan data.
            "homebrew.settingstrojan": ["settingstrojan.bin"],
        },
        install_requires=[
            'arcadeutils',
            'dragoncurses',
        ],
        python_requires=">=3.6",
    )
elif 'NETDIMM_INSTALLATION' in os.environ:
    # We want to install only the netdimm parts of this repo useful as a third-party
    # package so that other code can depend on us.
    with open(os.path.join("netdimm", "README.md"), "r", encoding="utf-8") as fh:
        long_description = fh.read()

    setup(
        name='netdimmutils',
        version=VERSION,
        description='Code libraries for working with a SEGA Net Dimm',
        long_description=long_description,
        long_description_content_type="text/markdown",
        author='DragonMinded',
        author_email='dragonminded@dragonminded.com',
        license='Public Domain',
        url='https://github.com/DragonMinded/netboot',
        packages=[
            # Package for 3rd party.
            'netdimm',
        ],
        package_data={
            # Make sure mypy sees us as typed.
            "netdimm": ["py.typed", "README.md"],
        },
        install_requires=[
            'pycryptodome',
            'arcadeutils',
            'naomiutils',
        ],
        python_requires=">=3.6",
    )
else:
    # We want to install the 3rdparty parts of this repo (netdimm and naomi) together
    # as a set of packages to depend on.
    setup(
        name='netbootutils',
        version=VERSION,
        description='Code and utilities for netbooting a Naomi/Triforce/Chihiro.',
        author='DragonMinded',
        license='Public Domain',
        packages=[
            # Core packages.
            'netdimm',
            'naomi',
            'naomi.settings',
            'settings',
            # Include settings definitions.
            'naomi.settings.definitions',
            # Include default trojan.
            'homebrew.settingstrojan',
        ],
        install_requires=[
            'pycryptodome',
            'arcadeutils',
            'dragoncurses',
        ],
        package_data={
            # Make sure mypy sees us as typed.
            "netdimm": ["py.typed", "README.md"],
            "naomi": ["py.typed", "README.md"],
            "naomi.settings": ["py.typed", "README.md"],
            "settings": ["py.typed", "README.md"],
            # Make sure to include all existing settings.
            "naomi.settings.definitions": ["*.settings", "README.md"],
            # Make sure to actually include the trojan data.
            "homebrew.settingstrojan": ["settingstrojan.bin"],
        },
    )
