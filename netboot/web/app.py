import os
import os.path
import yaml
import traceback
from functools import wraps
from typing import Callable, Dict, List, Any, cast

from flask import Flask, Response, request, render_template, make_response, jsonify as flask_jsonify
from werkzeug.routing import PathConverter
from netdimm import NetDimmVersionEnum, NetDimmTargetEnum
from naomi import NaomiRomRegionEnum
from netboot import Cabinet, CabinetRegionEnum, CabinetManager, DirectoryManager, PatchManager, SRAMManager, SettingsManager


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
        'enabled': cab.enabled,
        'time_hack': cab.time_hack,
        'send_timeout': cab.send_timeout,
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
    return make_response(
        render_template(
            'index.html',
            cabinets=sorted(
                [cabinet_to_dict(cab, dirman) for cab in cabman.cabinets],
                key=lambda cab: cast(str, cab['description']),
            ),
        ),
        200,
    )


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
    sramman = app.config['SRAMManager']
    srams: List[Dict[str, Any]] = []
    for directory in sramman.directories:
        srams.append({'name': directory, 'files': sorted(sramman.srams(directory))})
    settingsman = app.config['SettingsManager']
    settings: List[Dict[str, Any]] = []
    for directory in settingsman.directories:
        settings.append({'name': directory, 'files': sorted(settingsman.settings(directory))})

    return make_response(
        render_template(
            'systemconfig.html',
            roms=sorted(roms, key=lambda rom: cast(str, rom['name'])),
            patches=sorted(patches, key=lambda patch: cast(str, patch['name'])),
            settings=sorted(settings, key=lambda setting: cast(str, setting['name'])),
            srams=sorted(srams, key=lambda sram: cast(str, sram['name'])),
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
            targets=[t.value for t in NetDimmTargetEnum],
            versions=[tv.value for tv in NetDimmVersionEnum],
        ),
        200
    )


@app.route('/addcabinet')
def addcabinet() -> Response:
    return make_response(
        render_template(
            'addcabinet.html',
            regions=[cr.value for cr in CabinetRegionEnum if cr != CabinetRegionEnum.REGION_UNKNOWN],
            targets=[t.value for t in NetDimmTargetEnum],
            versions=[tv.value for tv in NetDimmVersionEnum],
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


@app.route('/settings/<filename:filename>')
@jsonify
def applicablesettings(filename: str) -> Dict[str, Any]:
    settingsman = app.config['SettingsManager']
    directories = set(settingsman.directories)
    settings_by_directory: Dict[str, List[str]] = {}
    try:
        settings = settingsman.settings_for_game(filename)
    except FileNotFoundError:
        if not filename.startswith('/'):
            filename = "/" + filename
        settings = settingsman.settings_for_game(filename)
    for setting in settings:
        dirname, filename = os.path.split(setting)
        if dirname not in directories:
            raise Exception("Expected all settings to be inside managed directories!")
        if dirname not in settings_by_directory:
            settings_by_directory[dirname] = []
        settings_by_directory[dirname].append(filename)
    return {
        'settings': sorted(
            [{'name': dirname, 'files': sorted(settings_by_directory[dirname])} for dirname in settings_by_directory],
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
        'patches': sorted(patches, key=lambda direntry: cast(str, direntry['name'])),
    }


@app.route('/srams')
@jsonify
def srams() -> Dict[str, Any]:
    sramman = app.config['SRAMManager']
    srams: List[Dict[str, Any]] = []
    for directory in sramman.directories:
        srams.append({'name': directory, 'files': sorted(sramman.srams(directory))})
    return {
        'srams': sorted(srams, key=lambda direntry: cast(str, direntry['name'])),
    }


@app.route('/srams', methods=['DELETE'])
@jsonify
def recalculateallsrams() -> Dict[str, Any]:
    sramman = app.config['SRAMManager']
    sramman.recalculate()
    return {}


@app.route('/srams/<filename:filename>')
@jsonify
def applicablesrams(filename: str) -> Dict[str, Any]:
    sramman = app.config['SRAMManager']
    directories = set(sramman.directories)
    srams_by_directory: Dict[str, List[str]] = {}
    try:
        srams = sramman.srams_for_game(filename)
    except FileNotFoundError:
        if not filename.startswith('/'):
            filename = "/" + filename
        srams = sramman.srams_for_game(filename)
    for sram in srams:
        dirname, filename = os.path.split(sram)
        if dirname not in directories:
            raise Exception("Expected all SRAM files to be inside managed directories!")
        if dirname not in srams_by_directory:
            srams_by_directory[dirname] = []
        srams_by_directory[dirname].append(filename)
    return {
        'srams': sorted(
            [{'name': dirname, 'files': sorted(srams_by_directory[dirname])} for dirname in srams_by_directory],
            key=lambda sram: cast(str, sram['name']),
        )
    }


@app.route('/srams/<filename:filename>', methods=['DELETE'])
def recalculateapplicablesrams(filename: str) -> Response:
    sramman = app.config['SRAMManager']
    sramman.recalculate(filename)
    if not filename.startswith('/'):
        filename = "/" + filename
    sramman.recalculate(filename)
    return applicablesrams(filename)


@app.route('/settings')
@jsonify
def settings() -> Dict[str, Any]:
    settingsman = app.config['SettingsManager']
    settings: List[Dict[str, Any]] = []
    for directory in settingsman.directories:
        settings.append({'name': directory, 'files': sorted(settingsman.settings(directory))})
    return {
        'settings': sorted(settings, key=lambda direntry: cast(str, direntry['name'])),
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
        srams={rom: None for rom in roms},
        target=NetDimmTargetEnum(request.json['target']),
        version=NetDimmVersionEnum(request.json['version']),
        enabled=True,
        time_hack=request.json['time_hack'],
        send_timeout=request.json['send_timeout'] or None,
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
    cabman.update_cabinet(
        ip,
        region=CabinetRegionEnum(request.json['region']),
        description=request.json['description'],
        target=NetDimmTargetEnum(request.json['target']),
        version=NetDimmVersionEnum(request.json['version']),
        enabled=request.json['enabled'],
        time_hack=request.json['time_hack'],
        send_timeout=request.json['send_timeout'] or None,
    )
    serialize_app(app)
    return cabinet_to_dict(cabman.cabinet(ip), dirman)


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
    sramman = app.config['SRAMManager']
    settingsman = app.config['SettingsManager']
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
            if cabinet.target == NetDimmTargetEnum.TARGET_NAOMI:
                naomi_region = {
                    CabinetRegionEnum.REGION_JAPAN: NaomiRomRegionEnum.REGION_JAPAN,
                    CabinetRegionEnum.REGION_USA: NaomiRomRegionEnum.REGION_USA,
                    CabinetRegionEnum.REGION_EXPORT: NaomiRomRegionEnum.REGION_EXPORT,
                    CabinetRegionEnum.REGION_KOREA: NaomiRomRegionEnum.REGION_KOREA,
                    CabinetRegionEnum.REGION_AUSTRALIA: NaomiRomRegionEnum.REGION_AUSTRALIA,
                }.get(cabinet.region, NaomiRomRegionEnum.REGION_JAPAN)
                settings, present = settingsman.get_naomi_settings(
                    full_filename,
                    cabinet.settings.get(full_filename, None),
                    patches=cabinet.patches.get(full_filename, []),
                    region=naomi_region,
                )
                if settings:
                    patches.append({
                        'file': 'eeprom',
                        'type': 'settings',
                        'enabled': present,
                        'settings': settings.to_json(),
                    })

                srams = sramman.srams_for_game(full_filename)
                if srams:
                    activesram = cabinet.srams.get(full_filename, None)
                    patches.append({
                        'file': 'sram',
                        'type': 'sram',
                        'active': activesram or "",
                        'choices': [
                            {
                                "v": "",
                                "t": "No SRAM File",
                            },
                            *sorted([{"v": f, "t": sramman.sram_name(f)} for f in srams], key=lambda e: cast(str, e['t']))
                        ],
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
    settingsman = app.config['SettingsManager']
    cabinet = cabman.cabinet(ip)
    for game in request.json['games']:
        if not game['enabled']:
            if game['file'] in cabinet.patches:
                del cabinet.patches[game['file']]
        else:
            cabinet.patches[game['file']] = [
                p['file'] for p in game['patches']
                if p['type'] == 'patch' and p['enabled']
            ]

            allsettings = [p for p in game['patches'] if p['type'] == 'settings']
            if len(allsettings) not in {0, 1}:
                raise Exception("Logic error, expected zero or one patch section!")
            settings = allsettings[0] if allsettings else None

            allsrams = [p for p in game['patches'] if p['type'] == 'sram']
            if len(allsrams) not in {0, 1}:
                raise Exception("Logic error, expected zero or one SRAM file section!")
            sram = allsrams[0] if allsrams else None

            if cabinet.target == NetDimmTargetEnum.TARGET_NAOMI:
                if settings is not None and settings['enabled']:
                    # Gotta convert this from JSON and set the settings.
                    cabinet.settings[game['file']] = settingsman.put_naomi_settings(settings['settings'])
                else:
                    cabinet.settings[game['file']] = None

                if sram is not None and sram['active']:
                    cabinet.srams[game['file']] = sram['active']
                else:
                    cabinet.srams[game['file']] = None
            else:
                cabinet.settings[game['file']] = None
                cabinet.srams[game['file']] = None
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
        data['patch_directory'] = 'patches'
    directory_or_list = data['patch_directory']
    if isinstance(directory_or_list, str):
        patches = [directory_or_list]
    elif isinstance(directory_or_list, list):
        patches = directory_or_list
    else:
        raise AppException(f"Invalid YAML file format for {config_file}, expected directory or list of directories for patch directory setting!")

    if 'sram_directory' not in data:
        data['sram_directory'] = 'srams'
    directory_or_list = data['sram_directory']
    if isinstance(directory_or_list, str):
        srams = [directory_or_list]
    elif isinstance(directory_or_list, list):
        srams = directory_or_list
    else:
        raise AppException(f"Invalid YAML file format for {config_file}, expected directory or list of directories for sram directory setting!")

    if 'settings_directory' not in data:
        raise AppException(f"Invalid YAML file format for {config_file}, missing naomi settings directory setting!")
    naomi_settings = data['settings_directory']
    if not isinstance(naomi_settings, str):
        raise AppException(f"Invalid YAML file format for {config_file}, expected directory for naomi settings directory setting!")

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
    app.config['SRAMManager'] = SRAMManager(srams)
    app.config['SettingsManager'] = SettingsManager(os.path.abspath(naomi_settings))
    app.config['config_file'] = os.path.abspath(config_file)
    app.config['cabinet_file'] = cabinet_file

    return app


def serialize_app(app: Flask) -> None:
    config = {
        'cabinet_config': app.config['cabinet_file'],
        'rom_directory': app.config['DirectoryManager'].directories,
        'patch_directory': app.config['PatchManager'].directories,
        'sram_directory': app.config['SRAMManager'].directories,
        'settings_directory': app.config['SettingsManager'].naomi_directory,
        'filenames': app.config['DirectoryManager'].checksums,
    }
    with open(app.config['config_file'], "w") as fp:
        yaml.dump(config, fp)

    config_dir = os.path.abspath(os.path.dirname(app.config['config_file']))
    cabinet_file = os.path.join(config_dir, app.config['cabinet_file'])
    app.config['CabinetManager'].to_yaml(cabinet_file)
