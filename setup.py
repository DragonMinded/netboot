import os
from setuptools import setup


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


if 'FULL_INSTALLATION' in os.environ:
    # We want to install this entire repo as an installation, so that we can
    # use it to run a netboot server.
    with open("MANIFEST.in", "w") as wfp:
        with open("MANIFEST.install", "r") as rfp:
            wfp.write(rfp.read())

    setup(
        name='netboot',
        version='0.1',
        description='Code and utilities for netbooting a Naomi/Triforce/Chihiro, including a full web and netboot server.',
        author='DragonMinded',
        license='Public Domain',
        packages=[
            # Core packages.
            'naomi',
            'naomi.settings',
            # Web server package.
            'netboot',
            'netboot.web',
        ],
        install_requires=[
            requires(req) for req in open('requirements.txt').read().split('\n') if len(req) > 0
        ],
        include_package_data=True,
        zip_safe=False,
    )
else:
    # We want to install only the parts of this repo useful as a third-party
    # package so that other code can depend on us.
    with open("MANIFEST.in", "w") as wfp:
        with open("MANIFEST.package", "r") as rfp:
            wfp.write(rfp.read())

    setup(
        name='netboot',
        version='0.1',
        description='Code libraries for working with Naomi/Triforce/Chihiro.',
        author='DragonMinded',
        license='Public Domain',
        packages=[
            # Package for 3rd party.
            'naomi',
            'naomi.settings',
            'homebrew.settingstrojan',
        ],
        package_data={
            "naomi": ["py.typed"],
            "naomi.settings": ["py.typed"],
        },
        install_requires=[
            # Nothing is a dependency if we are just doing a package for 3rdparty.
        ],
        include_package_data=True,
        zip_safe=False,
    )
