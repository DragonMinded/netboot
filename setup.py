from setuptools import setup  # type: ignore

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
        req for req in open('requirements.txt').read().split('\n') if len(req) > 0 and "git+" not in req
    ],
    include_package_data=True,
    zip_safe=False,
)
