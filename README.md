# Tools for working with a NetDimm

This repository started when I looked at triforcetools.py and realized that it was ancient (Python2) and contained a lot of dead code. So, as one of the first projects I did when setting up my NNC to netboot was to port triforcetools.py to Python3, clean up dead code and add type hints. I also added percentage display for transfer and `--help` to the command-line. This has been tested on a Naomi with a NetDimm, but has not been verified on Triforce/Chihiro. There is no reason why it should not work, however.

## Setup Requirements

This requires at least Python 3.6, and a few packages installed. To install the required packages, run the following. You may need to preface with sudo if you are installing into system Python.

```
python3 -m pip install -r requirements.txt
```

## Script Invocation

Invoke the script like so to see options:

```
python3 netdimm_send.py --help
```

You can invoke it identically to the original triforcetools.py as well. Assuming your NetDimm is at 192.168.1.1, the following will load the ROM named `my_favorite_game.bin` from the current directory:


```
python3 netdimm_send.py 192.168.1.1 my_favorite_game.bin
```

## Developing

The tools here are fully typed, and should be kept that way. To verify type hints, run the following:

```
mypy --strict scripts/
```

The tools are also lint clean (save for line length lints which are useless drivel). To verify lint, run the following:

```
flake8 --ignore E501 scripts/
```
