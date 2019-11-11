import os.path
import yaml
import traceback
from functools import wraps
from typing import Callable, Dict, List, Any

from flask import Flask, Response, render_template, make_response, jsonify as flask_jsonify
from netboot import Cabinet, CabinetManager, DirectoryManager, PatchManager


current_directory: str = os.path.abspath(os.path.dirname(__file__))

app = Flask(
    __name__,
    static_folder=os.path.join(current_directory, 'static'),
    template_folder=os.path.join(current_directory, 'templates'),
)


def jsonify(func: Callable) -> Callable:
    @wraps(func)
    def decoratedfunction(*args: Any, **kwargs: Any) -> Response:
        try:
            return flask_jsonify({**func(*args, **kwargs), "error": False})
        except Exception as e:
            print(traceback.format_exc())
            return flask_jsonify({
                'error': True,
                'message': str(e),
            })
    return decoratedfunction


def cabinet_to_dict(cab: Cabinet, dirmanager: DirectoryManager) -> Dict[str, str]:
    status, progress = cab.state

    return {
        'ip': cab.ip,
        'description': cab.description,
        'game': dirmanager.game_name(cab.filename, cab.region),
        'target': cab.target,
        'version': cab.version,
        'status': status,
        'progress': progress,
    }


@app.route('/')
def home() -> Response:
    cabman = app.config['CabinetManager']
    dirman = app.config['DirectoryManager']
    return make_response(render_template('index.html', cabinets=[cabinet_to_dict(cab, dirman) for cab in cabman.cabinets]), 200)


@app.route('/config')
def systemconfig() -> Response:
    # We don't look up the game names here because that requires a region which is cab-specific.
    dirman = app.config['DirectoryManager']
    roms: List[Dict[str, str]] = []
    for directory in dirman.directories:
        roms.append({'name': directory, 'files': dirman.games(directory)})
    patchman = app.config['PatchManager']
    patches: List[Dict[str, str]] = []
    for directory in patchman.directories:
        patches.append({'name': directory, 'files': patchman.patches(directory)})
    return make_response(render_template('systemconfig.html', roms=roms, patches=patches), 200)


@app.route('/roms')
@jsonify
def roms() -> Dict[str, Any]:
    dirman = app.config['DirectoryManager']
    roms: List[Dict[str, str]] = []
    for directory in dirman.directories:
        roms.append({'name': directory, 'files': dirman.games(directory)})
    return {
        'roms': roms,
    }


@app.route('/patches')
@jsonify
def patches() -> Dict[str, Any]:
    patchman = app.config['PatchManager']
    patches: List[Dict[str, str]] = []
    for directory in patchman.directories:
        roms.append({'name': directory, 'files': patchman.patches(directory)})
    return {
        'patches': patches,
    }


@app.route('/cabinets')
@jsonify
def cabinets() -> Dict[str, Any]:
    cabman = app.config['CabinetManager']
    dirman = app.config['DirectoryManager']
    return {
        'cabinets': [cabinet_to_dict(cab, dirman) for cab in cabman.cabinets],
    }


@app.route('/cabinets/<ip>')
@jsonify
def cabinet(ip: str) -> Dict[str, Any]:
    manager = app.config['CabinetManager']
    cabinet = manager.cabinet(ip)
    return cabinet_to_dict(cabinet)


class AppException(Exception):
    pass


def spawn_app(config_file: str) -> Flask:
    with open(config_file, "r") as fp:
        data = yaml.safe_load(fp)
    config_dir = os.path.abspath(os.path.dirname(config_file))

    if not isinstance(data, dict):
        raise AppException(f"Invalid YAML file format for {config_file}, missing config entries!")

    if 'cabinet_config' not in data:
        raise AppException(f"Invalid YAML file format for {config_file}, missing cabinet config file setting!")
    cabinet_file = data['cabinet_config']
    if not os.path.isfile(cabinet_file):
        # Assume they want to create a new empty one.
        with open(cabinet_file, "w") as fp:
            fp.write("")

    if 'rom_directory' not in data:
        raise AppException(f"Invalid YAML file format for {config_file}, missing rom directory setting!")
    directory_or_list = data['rom_directory']
    if isinstance(directory_or_list, str):
        directories = [directory_or_list]
    elif isinstance(directory_or_list, list):
        directories = directory_or_list
    else:
        raise AppException(f"Invalid YAML file format for {config_file}, expected directory or list of directories for rom directory setting!")

    # Allow use of relative paths (relative to config file).
    directories = [os.path.abspath(os.path.join(config_dir, d)) for d in directories]
    for directory in directories:
        if not os.path.isdir(directory):
            raise AppException(f"Invalid YAML file format for {config_file}, {directory} is not a directory!")

    if 'patch_directory' not in data:
        raise AppException(f"Invalid YAML file format for {config_file}, missing patch directory setting!")
    directory_or_list = data['patch_directory']
    if isinstance(directory_or_list, str):
        patches = [directory_or_list]
    elif isinstance(directory_or_list, list):
        patches = directory_or_list
    else:
        raise AppException(f"Invalid YAML file format for {config_file}, expected directory or list of directories for patch directory setting!")

    # Allow use of relative paths (relative to config file).
    patches = [os.path.abspath(os.path.join(config_dir, d)) for d in patches]
    for patch in patches:
        if not os.path.isdir(patch):
            raise AppException(f"Invalid YAML file format for {config_file}, {patch} is not a directory!")

    if 'filenames' in data and isinstance(data, dict):
        checksums = data['filenames']
    else:
        checksums = {}

    app.config['CabinetManager'] = CabinetManager.from_yaml(cabinet_file)
    app.config['DirectoryManager'] = DirectoryManager(directories, checksums)
    app.config['PatchManager'] = PatchManager(patches)

    return app
