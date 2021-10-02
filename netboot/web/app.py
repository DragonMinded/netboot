import os
import os.path
import yaml
import traceback
from functools import wraps
from typing import Callable, Dict, List, Any, cast

from flask import Flask, Response, request, render_template, make_response, jsonify as flask_jsonify
from werkzeug.routing import PathConverter
from netboot import Cabinet, CabinetManager, DirectoryManager, PatchManager, NetDimm


current_directory: str = os.path.abspath(os.path.dirname(__file__))

app = Flask(
    __name__,
    static_folder=os.path.join(current_directory, 'static'),
    template_folder=os.path.join(current_directory, 'templates'),
)


class EverythingConverter(PathConverter):
    regex = '.*?'


app.url_map.converters['filename'] = EverythingConverter


def jsonify(func: Callable[..., Dict[str, Any]]) -> Callable[..., Response]:
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


def cabinet_to_dict(cab: Cabinet, dirmanager: DirectoryManager) -> Dict[str, Any]:
    status, progress = cab.state

    return {
        'ip': cab.ip,
        'description': cab.description,
        'region': cab.region,
        'game': dirmanager.game_name(cab.filename, cab.region) if cab.filename is not None else "no game selected",
        'filename': cab.filename,
        'options': sorted(
            [{'file': filename, 'name': dirmanager.game_name(filename, cab.region)} for filename in cab.patches],
            key=lambda option: cast(str, option['name']),
        ),
        'target': cab.target,
        'version': cab.version,
        'status': status,
        'progress': progress,
    }


@app.after_request
def after_request(response: Response) -> Response:
    # Make sure our REST responses don't get cached, so that remote
    # servers which respect cache headers don't get confused.
    response.cache_control.no_cache = True
    response.cache_control.must_revalidate = True
    response.cache_control.private = True
    return response


@app.route('/')
def home() -> Response:
    cabman = app.config['CabinetManager']
    dirman = app.config['DirectoryManager']
    return make_response(render_template('index.html', cabinets=[cabinet_to_dict(cab, dirman) for cab in cabman.cabinets]), 200)


@app.route('/config')
def systemconfig() -> Response:
    # We don't look up the game names here because that requires a region which is cab-specific.
    dirman = app.config['DirectoryManager']
    roms: List[Dict[str, Any]] = []
    for directory in dirman.directories:
        roms.append({'name': directory, 'files': sorted(dirman.games(directory))})
    patchman = app.config['PatchManager']
    patches: List[Dict[str, Any]] = []
    for directory in patchman.directories:
        patches.append({'name': directory, 'files': sorted(patchman.patches(directory))})
    return make_response(
        render_template(
            'systemconfig.html',
            roms=sorted(roms, key=lambda rom: cast(str, rom['name'])),
            patches=sorted(patches, key=lambda patch: cast(str, patch['name'])),
        ),
        200,
    )


@app.route('/config/rom/<filename:filename>')
def romconfig(filename: str) -> Response:
    dirman = app.config['DirectoryManager']
    directory, name = os.path.split(filename)
    if directory not in dirman.directories and not directory.startswith('/'):
        directory = "/" + directory
        filename = "/" + filename
    if directory not in dirman.directories:
        raise Exception("This isn't a valid ROM file!")
    if name not in dirman.games(directory):
        raise Exception("This isn't a valid ROM file!")
    return make_response(
        render_template(
            'romconfig.html',
            filename=filename,
            names={
                Cabinet.REGION_JAPAN: dirman.game_name(filename, Cabinet.REGION_JAPAN),
                Cabinet.REGION_USA: dirman.game_name(filename, Cabinet.REGION_USA),
                Cabinet.REGION_EXPORT: dirman.game_name(filename, Cabinet.REGION_EXPORT),
                Cabinet.REGION_KOREA: dirman.game_name(filename, Cabinet.REGION_KOREA),
                Cabinet.REGION_AUSTRALIA: dirman.game_name(filename, Cabinet.REGION_AUSTRALIA),
            }
        ),
        200
    )


@app.route('/config/cabinet/<ip>')
def cabinetconfig(ip: str) -> Response:
    cabman = app.config['CabinetManager']
    dirman = app.config['DirectoryManager']
    cabinet = cabman.cabinet(ip)
    return make_response(
        render_template(
            'gameconfig.html',
            cabinet=cabinet_to_dict(cabinet, dirman),
            regions=[
                Cabinet.REGION_JAPAN,
                Cabinet.REGION_USA,
                Cabinet.REGION_EXPORT,
                Cabinet.REGION_KOREA,
                Cabinet.REGION_AUSTRALIA,
            ],
            targets=[
                NetDimm.TARGET_NAOMI,
                NetDimm.TARGET_CHIHIRO,
                NetDimm.TARGET_TRIFORCE,
            ],
            versions=[
                NetDimm.TARGET_VERSION_3_01,
                NetDimm.TARGET_VERSION_2_15,
                NetDimm.TARGET_VERSION_2_03,
                NetDimm.TARGET_VERSION_1_07,
            ],
        ),
        200
    )


@app.route('/addcabinet')
def addcabinet() -> Response:
    return make_response(
        render_template(
            'addcabinet.html',
            regions=[
                Cabinet.REGION_JAPAN,
                Cabinet.REGION_USA,
                Cabinet.REGION_EXPORT,
                Cabinet.REGION_KOREA,
                Cabinet.REGION_AUSTRALIA,
            ],
            targets=[
                NetDimm.TARGET_NAOMI,
                NetDimm.TARGET_CHIHIRO,
                NetDimm.TARGET_TRIFORCE,
            ],
            versions=[
                NetDimm.TARGET_VERSION_3_01,
                NetDimm.TARGET_VERSION_2_15,
                NetDimm.TARGET_VERSION_2_03,
                NetDimm.TARGET_VERSION_1_07,
            ],
        ),
        200
    )


@app.route('/roms')
@jsonify
def roms() -> Dict[str, Any]:
    dirman = app.config['DirectoryManager']
    roms: List[Dict[str, Any]] = []
    for directory in dirman.directories:
        roms.append({'name': directory, 'files': sorted(dirman.games(directory))})
    return {
        'roms': sorted(roms, key=lambda rom: cast(str, rom['name'])),
    }


@app.route('/roms/<filename:filename>', methods=['POST'])
@jsonify
def updaterom(filename: str) -> Dict[str, Any]:
    dirman = app.config['DirectoryManager']
    directory, name = os.path.split(filename)
    if directory not in dirman.directories and not directory.startswith('/'):
        directory = "/" + directory
        filename = "/" + filename
    if directory not in dirman.directories:
        raise Exception("This isn't a valid ROM file!")
    if name not in dirman.games(directory):
        raise Exception("This isn't a valid ROM file!")
    if request.json is not None:
        for region, name in request.json.items():
            dirman.rename_game(filename, region, name)
        serialize_app(app)
        return {
            Cabinet.REGION_JAPAN: dirman.game_name(filename, Cabinet.REGION_JAPAN),
            Cabinet.REGION_USA: dirman.game_name(filename, Cabinet.REGION_USA),
            Cabinet.REGION_EXPORT: dirman.game_name(filename, Cabinet.REGION_EXPORT),
            Cabinet.REGION_KOREA: dirman.game_name(filename, Cabinet.REGION_KOREA),
            Cabinet.REGION_AUSTRALIA: dirman.game_name(filename, Cabinet.REGION_AUSTRALIA),
        }
    else:
        raise Exception("Expected JSON data in request!")


@app.route('/patches', methods=['DELETE'])
@jsonify
def recalculateallpatches() -> Dict[str, Any]:
    patchman = app.config['PatchManager']
    patchman.recalculate()
    return {}


@app.route('/patches/<filename:filename>')
@jsonify
def applicablepatches(filename: str) -> Dict[str, Any]:
    patchman = app.config['PatchManager']
    directories = set(patchman.directories)
    patches_by_directory: Dict[str, List[str]] = {}
    try:
        patches = patchman.patches_for_game(filename)
    except FileNotFoundError:
        if not filename.startswith('/'):
            filename = "/" + filename
        patches = patchman.patches_for_game(filename)
    for patch in patches:
        dirname, filename = os.path.split(patch)
        if dirname not in directories:
            raise Exception("Expected all patches to be inside managed directories!")
        if dirname not in patches_by_directory:
            patches_by_directory[dirname] = []
        patches_by_directory[dirname].append(filename)
    return {
        'patches': sorted(
            [{'name': dirname, 'files': sorted(patches_by_directory[dirname])} for dirname in patches_by_directory],
            key=lambda patch: cast(str, patch['name']),
        )
    }


@app.route('/patches/<filename:filename>', methods=['DELETE'])
def recalculateapplicablepatches(filename: str) -> Response:
    patchman = app.config['PatchManager']
    patchman.recalculate(filename)
    if not filename.startswith('/'):
        filename = "/" + filename
    patchman.recalculate(filename)
    return applicablepatches(filename)


@app.route('/patches')
@jsonify
def patches() -> Dict[str, Any]:
    patchman = app.config['PatchManager']
    patches: List[Dict[str, Any]] = []
    for directory in patchman.directories:
        patches.append({'name': directory, 'files': sorted(patchman.patches(directory))})
    return {
        'patches': sorted(patches, key=lambda patch: cast(str, patch['name'])),
    }


@app.route('/cabinets')
@jsonify
def cabinets() -> Dict[str, Any]:
    cabman = app.config['CabinetManager']
    dirman = app.config['DirectoryManager']
    return {
        'cabinets': sorted(
            [cabinet_to_dict(cab, dirman) for cab in cabman.cabinets],
            key=lambda cab: cast(str, cab['description']),
        ),
    }


@app.route('/cabinets/<ip>')
@jsonify
def cabinet(ip: str) -> Dict[str, Any]:
    cabman = app.config['CabinetManager']
    dirman = app.config['DirectoryManager']
    cabinet = cabman.cabinet(ip)
    return cabinet_to_dict(cabinet, dirman)


@app.route('/cabinets/<ip>', methods=['PUT'])
@jsonify
def createcabinet(ip: str) -> Dict[str, Any]:
    if request.json is None:
        raise Exception("Expected JSON data in request!")
    cabman = app.config['CabinetManager']
    dirman = app.config['DirectoryManager']
    if cabman.cabinet_exists(ip):
        raise Exception("Cabinet already exists!")
    # As a convenience, start with all game selectable instad of none.
    roms: List[str] = []
    for directory in dirman.directories:
        roms.extend(os.path.join(directory, filename) for filename in dirman.games(directory))
    new_cabinet = Cabinet(
        ip=ip,
        region=request.json['region'],
        description=request.json['description'],
        filename=None,
        patches={rom: [] for rom in roms},
        target=request.json['target'],
        version=request.json['version'],
    )
    cabman.add_cabinet(new_cabinet)
    serialize_app(app)
    return cabinet_to_dict(new_cabinet, dirman)


@app.route('/cabinets/<ip>', methods=['POST'])
@jsonify
def updatecabinet(ip: str) -> Dict[str, Any]:
    if request.json is None:
        raise Exception("Expected JSON data in request!")
    cabman = app.config['CabinetManager']
    dirman = app.config['DirectoryManager']
    old_cabinet = cabman.cabinet(ip)
    new_cabinet = Cabinet(
        ip=ip,
        region=request.json['region'],
        description=request.json['description'],
        filename=old_cabinet.filename,
        patches=old_cabinet.patches,
        target=request.json['target'],
        version=request.json['version'],
    )
    cabman.update_cabinet(new_cabinet)
    serialize_app(app)
    return cabinet_to_dict(new_cabinet, dirman)


@app.route('/cabinets/<ip>', methods=['DELETE'])
@jsonify
def removecabinet(ip: str) -> Dict[str, Any]:
    cabman = app.config['CabinetManager']
    cabman.remove_cabinet(ip)
    serialize_app(app)
    return {}


@app.route('/cabinets/<ip>/games')
@jsonify
def romsforcabinet(ip: str) -> Dict[str, Any]:
    cabman = app.config['CabinetManager']
    dirman = app.config['DirectoryManager']
    patchman = app.config['PatchManager']
    cabinet = cabman.cabinet(ip)

    roms: List[Dict[str, Any]] = []
    for directory in dirman.directories:
        for filename in dirman.games(directory):
            full_filename = os.path.join(directory, filename)
            patches = patchman.patches_for_game(full_filename)
            patches = sorted(
                [
                    {
                        'file': patch,
                        'enabled': patch in cabinet.patches.get(full_filename, []),
                        'name': patchman.patch_name(patch),
                    }
                    for patch in patches
                ],
                key=lambda patch: cast(str, patch['name']),
            )
            roms.append({
                'file': full_filename,
                'name': dirman.game_name(full_filename, cabinet.region),
                'enabled': full_filename in cabinet.patches,
                'patches': patches,
            })
    return {'games': sorted(roms, key=lambda rom: cast(str, rom['name']))}


@app.route('/cabinets/<ip>/games', methods=['POST'])
def updateromsforcabinet(ip: str) -> Response:
    if request.json is None:
        raise Exception("Expected JSON data in request!")
    cabman = app.config['CabinetManager']
    cabinet = cabman.cabinet(ip)
    for game in request.json['games']:
        if not game['enabled']:
            if game['file'] in cabinet.patches:
                del cabinet.patches[game['file']]
        else:
            cabinet.patches[game['file']] = [
                p['file'] for p in game['patches']
                if p['enabled']
            ]
    serialize_app(app)
    return romsforcabinet(ip)


@app.route('/cabinets/<ip>/filename', methods=['POST'])
def changegameforcabinet(ip: str) -> Response:
    if request.json is None:
        raise Exception("Expected JSON data in request!")
    cabman = app.config['CabinetManager']
    cab = cabman.cabinet(ip)
    cab.filename = request.json['filename']
    serialize_app(app)
    return cabinet(ip)


class AppException(Exception):
    pass


def spawn_app(config_file: str, debug: bool = False) -> Flask:
    if debug and not os.environ.get('WERKZEUG_RUN_MAIN'):
        return app

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
    app.config['config_file'] = os.path.abspath(config_file)
    app.config['cabinet_file'] = cabinet_file

    return app


def serialize_app(app: Flask) -> None:
    config = {
        'cabinet_config': app.config['cabinet_file'],
        'rom_directory': app.config['DirectoryManager'].directories,
        'patch_directory': app.config['PatchManager'].directories,
        'filenames': app.config['DirectoryManager'].checksums,
    }
    with open(app.config['config_file'], "w") as fp:
        yaml.dump(config, fp)

    config_dir = os.path.abspath(os.path.dirname(app.config['config_file']))
    cabinet_file = os.path.join(config_dir, app.config['cabinet_file'])
    app.config['CabinetManager'].to_yaml(cabinet_file)
