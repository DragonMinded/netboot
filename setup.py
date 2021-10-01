from setuptools import setup  # type: ignore


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
    version='0.1',
    description='Code and utilities for netbooting a Naomi/Triforce/Chihiro.',
    author='DragonMinded',
    license='Public Domain',
    packages=[
        # Core packages
        'naomi',
        'netboot',
        'netboot.web',
    ],
    install_requires=[
        requires(req) for req in open('requirements.txt').read().split('\n') if len(req) > 0
    ],
    include_package_data=True,
    zip_safe=False,
)
