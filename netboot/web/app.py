import os
import os.path
import yaml
import traceback
from functools import wraps
from typing import Callable, Dict, List, Any, Optional, cast

from flask import Flask, Response, request, render_template, make_response, jsonify as flask_jsonify
from werkzeug.routing import PathConverter
from arcadeutils.binary import BinaryDiff, BinaryDiffException
from naomi import NaomiRom, NaomiSettingsPatcher, NaomiRomRegionEnum
from naomi.settings import SettingsWrapper, SettingsManager
from netboot import Cabinet, CabinetRegionEnum, CabinetManager, DirectoryManager, PatchManager, TargetEnum, TargetVersionEnum


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
        'region': cab.region.value,
        'game': dirmanager.game_name(cab.filename, cab.region) if cab.filename is not None else "no game selected",
        'filename': cab.filename,
        'options': sorted(
            [{'file': filename, 'name': dirmanager.game_name(filename, cab.region)} for filename in cab.patches],
            key=lambda option: cast(str, option['name']),
        ),
        'target': cab.target.value,
        'version': cab.version.value,
        'status': status.value,
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
                CabinetRegionEnum.REGION_JAPAN.value: dirman.game_name(filename, CabinetRegionEnum.REGION_JAPAN),
                CabinetRegionEnum.REGION_USA.value: dirman.game_name(filename, CabinetRegionEnum.REGION_USA),
                CabinetRegionEnum.REGION_EXPORT.value: dirman.game_name(filename, CabinetRegionEnum.REGION_EXPORT),
                CabinetRegionEnum.REGION_KOREA.value: dirman.game_name(filename, CabinetRegionEnum.REGION_KOREA),
                CabinetRegionEnum.REGION_AUSTRALIA.value: dirman.game_name(filename, CabinetRegionEnum.REGION_AUSTRALIA),
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
            regions=[cr.value for cr in CabinetRegionEnum if cr != CabinetRegionEnum.REGION_UNKNOWN],
            targets=[t.value for t in TargetEnum],
            versions=[tv.value for tv in TargetVersionEnum],
        ),
        200
    )


@app.route('/addcabinet')
def addcabinet() -> Response:
    return make_response(
        render_template(
            'addcabinet.html',
            regions=[cr.value for cr in CabinetRegionEnum if cr != CabinetRegionEnum.REGION_UNKNOWN],
            targets=[t.value for t in TargetEnum],
            versions=[tv.value for tv in TargetVersionEnum],
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
            dirman.rename_game(filename, CabinetRegionEnum(region), name)
        serialize_app(app)
        return {
            CabinetRegionEnum.REGION_JAPAN.value: dirman.game_name(filename, CabinetRegionEnum.REGION_JAPAN),
            CabinetRegionEnum.REGION_USA.value: dirman.game_name(filename, CabinetRegionEnum.REGION_USA),
            CabinetRegionEnum.REGION_EXPORT.value: dirman.game_name(filename, CabinetRegionEnum.REGION_EXPORT),
            CabinetRegionEnum.REGION_KOREA.value: dirman.game_name(filename, CabinetRegionEnum.REGION_KOREA),
            CabinetRegionEnum.REGION_AUSTRALIA.value: dirman.game_name(filename, CabinetRegionEnum.REGION_AUSTRALIA),
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
        region=CabinetRegionEnum(request.json['region']),
        description=request.json['description'],
        filename=None,
        patches={rom: [] for rom in roms},
        settings={rom: None for rom in roms},
        target=TargetEnum(request.json['target']),
        version=TargetVersionEnum(request.json['version']),
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
        region=CabinetRegionEnum(request.json['region']),
        description=request.json['description'],
        filename=old_cabinet.filename,
        patches=old_cabinet.patches,
        settings=old_cabinet.settings,
        target=TargetEnum(request.json['target']),
        version=TargetVersionEnum(request.json['version']),
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


@app.route('/cabinets/<ip>/info')
@jsonify
def cabinetinfo(ip: str) -> Dict[str, Any]:
    cabman = app.config['CabinetManager']
    cabinet = cabman.cabinet(ip)
    info = cabinet.info()

    if info is None:
        return {}
    else:
        return {
            'version': info.firmware_version.value,
            'memsize': info.memory_size,
            'memavail': int(info.available_game_memory / 1024 / 1024),
            'available': True,
        }


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
                        'type': 'patch',
                        'enabled': patch in cabinet.patches.get(full_filename, []),
                        'name': patchman.patch_name(patch),
                    }
                    for patch in patches
                ],
                key=lambda patch: cast(str, patch['name']),
            )

            # Calculate whether we are allowed to modify settings or not, and
            # if so, is there a setting enabled for this game.
            if cabinet.target == TargetEnum.TARGET_NAOMI:
                settingsdata = cabinet.settings.get(full_filename, None)
                settings: Optional[SettingsWrapper] = None

                # TODO: This is Naomi-specific and really should be moved into
                # "Cabinet". However, if we do that we need to generalize the
                # settings so it isn't naomi-specific.
                manager = SettingsManager(app.config['settings_directory'])
                if settingsdata is not None:
                    if len(settingsdata) != NaomiSettingsPatcher.EEPROM_SIZE:
                        raise Exception("We don't support non-EEPROM settings!")

                    settings = manager.from_eeprom(settingsdata)
                else:
                    with open(full_filename, "rb") as fp:
                        data = fp.read(0x1000)

                    # First, attempt to patch with any patches that fit in the first
                    # chunk, so the defaults we get below match any force settings
                    # patches we did to the header.
                    for patch in cabinet.patches.get(full_filename, []):
                        with open(patch, "r") as pp:
                            differences = pp.readlines()
                        differences = [d.strip() for d in differences if d.strip()]
                        try:
                            data = BinaryDiff.patch(data, differences, ignore_size_differences=True)
                        except BinaryDiffException:
                            # Patch was for something not in the header.
                            pass

                    rom = NaomiRom(data)
                    if rom.valid:
                        naomi_region = {
                            CabinetRegionEnum.REGION_JAPAN: NaomiRomRegionEnum.REGION_JAPAN,
                            CabinetRegionEnum.REGION_USA: NaomiRomRegionEnum.REGION_USA,
                            CabinetRegionEnum.REGION_EXPORT: NaomiRomRegionEnum.REGION_EXPORT,
                            CabinetRegionEnum.REGION_KOREA: NaomiRomRegionEnum.REGION_KOREA,
                            CabinetRegionEnum.REGION_AUSTRALIA: NaomiRomRegionEnum.REGION_AUSTRALIA,
                        }.get(cabinet.region, NaomiRomRegionEnum.REGION_JAPAN)
                        settings = manager.from_rom(rom, naomi_region)

                if settings is not None:
                    # TODO: If we ever support editing SRAM from the frontend, this
                    # needs to change to also support SRAM file types, and the code
                    # in app.js also would need updating. For now we don't support it.
                    patches.append({
                        'file': 'eeprom',
                        'type': 'settings',
                        'enabled': settingsdata is not None,
                        'settings': settings.to_json(),
                    })

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
                if p['enabled'] and p['type'] == 'patch'
            ]

            allsettings = [p for p in game['patches'] if p['type'] == 'settings']
            if len(allsettings) not in {0, 1}:
                raise Exception("Logic error, expected zero or one patch section!")
            settings = allsettings[0] if allsettings else None

            if cabinet.target == TargetEnum.TARGET_NAOMI:
                # TODO: This is also Naomi-specific, much like the get method
                # above. It should be moved into cabman and generalized.
                if settings['enabled']:
                    # Gotta convert this from JSON and set the settings.
                    manager = SettingsManager(app.config['settings_directory'])
                    parsedsettings = manager.from_json(settings['settings'])
                    eepromdata = manager.to_eeprom(parsedsettings)
                    cabinet.settings[game['file']] = eepromdata
                else:
                    cabinet.settings[game['file']] = None
            else:
                cabinet.settings[game['file']] = None
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

    if 'settings_directory' not in data:
        raise AppException(f"Invalid YAML file format for {config_file}, missing settings directory setting!")
    settings = data['settings_directory']
    if not isinstance(settings, str):
        raise AppException(f"Invalid YAML file format for {config_file}, expected directory for settings directory setting!")

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
    app.config['settings_directory'] = os.path.abspath(settings)
    app.config['config_file'] = os.path.abspath(config_file)
    app.config['cabinet_file'] = cabinet_file

    return app


def serialize_app(app: Flask) -> None:
    config = {
        'cabinet_config': app.config['cabinet_file'],
        'rom_directory': app.config['DirectoryManager'].directories,
        'patch_directory': app.config['PatchManager'].directories,
        'settings_directory': app.config['settings_directory'],
        'filenames': app.config['DirectoryManager'].checksums,
    }
    with open(app.config['config_file'], "w") as fp:
        yaml.dump(config, fp)

    config_dir = os.path.abspath(os.path.dirname(app.config['config_file']))
    cabinet_file = os.path.join(config_dir, app.config['cabinet_file'])
    app.config['CabinetManager'].to_yaml(cabinet_file)
