function setCookie(cname, cvalue, exdays) {
    var d = new Date();
    d.setTime(d.getTime() + (exdays * 24 * 60 * 60 * 1000));
    var expires = "expires="+d.toUTCString();
    document.cookie = cname + "=" + cvalue + ";" + expires + ";path=/";
}

function getCookie(cname) {
    var name = cname + "=";
    var ca = document.cookie.split(';');
    for(var i = 0; i < ca.length; i++) {
        var c = ca[i];
        while (c.charAt(0) == ' ') {
            c = c.substring(1);
        }
        if (c.indexOf(name) == 0) {
            return c.substring(name.length, c.length);
        }
    }
    return "";
}

const eventBus = new Vue ({
    created() {
        window.keys = "";
        window.addEventListener('keydown', (e) => {
            window.keys = window.keys + e.key;
            if( window.keys.substr(window.keys.length - 6) == "config" ) {
                window.keys = "";
                setCookie('admin', 'true', 7);
                eventBus.$emit('changeConfigButtons', true)
            }
        });
    },
});

Vue.component('state', {
    props: ['status', 'progress'],
    template: `
        <span>
            <span v-if="status == 'turned_off'">turned off</span>
            <span v-if="status == 'startup' || status == 'wait_power_on'">waiting for cabinet</span>
            <span v-if="status == 'disabled'">disabled</span>
            <span v-if="status == 'wait_power_off'">running game</span>
            <span v-if="status == 'power_cycle'">rebooting cabinet</span>
            <span v-if="status == 'check_game'">verifying game crc</span>
            <span v-if="status == 'send_game'">sending game ({{ progress }}% complete)</span>
        </span>
    `,
});

Vue.component('cabinet', {
    props: ['cabinet'],
    data: function() {
        return {
            selecting: false,
            choice: this.cabinet.filename,
            games: {},
            admin: getCookie('admin') == 'true',
        };
    },
    methods: {
        change: function() {
            eventBus.$emit('selectingNewGame', true);
            this.selecting = true;
        },
        choose: function() {
            axios.post('/cabinets/' + this.cabinet.ip + '/filename', {filename: this.choice}).then(result => {
                if (!result.data.error) {
                    this.choice = result.data.filename;
                    this.cabinet = result.data;
                    this.selecting = false;
                    eventBus.$emit('selectedNewGame', true);
                }
            });
        },
        poweron: function() {
            axios.post('/cabinets/' + this.cabinet.ip + '/power/on', {}).then(result => {
                if (!result.data.error) {
                    this.cabinet = result.data;
                }
            });
        },
        poweroff: function() {
            axios.post('/cabinets/' + this.cabinet.ip + '/power/off', {}).then(result => {
                if (!result.data.error) {
                    this.cabinet = result.data;
                }
            });
        },
        cancel: function() {
            this.selecting = false;
            eventBus.$emit('selectedNewGame', true);
        },
        update: function() {
            this.admin = getCookie('admin') == 'true';
        },
    },
    created () {
        eventBus.$on('changeConfigButtons', this.update);
    },
    template: `
        <div class='cabinet'>
            <h3>{{ cabinet.description }}</h3>
            <dl>
                <dt>Game</dt>
                <dd>
                    <span v-if="!selecting">{{ cabinet.game }}</span>
                    <span v-if="selecting">
                        <select v-model="choice">
                            <option v-for="game in cabinet.options" v-bind:value="game.file">{{ game.name }}</option>
                        </select>
                    </span>
                    <span class="right">
                        <button v-if="(cabinet.options.length > 1 || (cabinet.options.length == 1 && cabinet.options[0].name != cabinet.game)) && !selecting" class="small" v-on:click="change">Choose New Game</button>
                        <button v-if="selecting" class="small" v-on:click="choose">Run This Game</button>
                        <button v-if="selecting" class="small" v-on:click="cancel">Nevermind</button>
                    </span>
                </dd>
                <dt>Status</dt><dd><state v-bind:status="cabinet.status" v-bind:progress="cabinet.progress"></state></dd>
            </dl>
            <div class="configure">
                <button v-if="cabinet.controllable && cabinet.power_state == 'off'" :disabled="cabinet.status == 'power_cycle'" v-on:click="poweron">Turn Cabinet On</button>
                <button v-if="cabinet.controllable && cabinet.power_state == 'on'" :disabled="cabinet.status == 'power_cycle'" v-on:click="poweroff">Turn Cabinet Off</button>
                <a class="button" v-if="admin" v-bind:href="'config/cabinet/' + cabinet.ip">Configure Cabinet</a>
            </div>
        </div>
    `,
});

Vue.component('romlist', {
    props: ['dir'],
    template: `
        <div class='directory'>
            <div>{{ dir.name }}</div>
            <rom v-for="rom in dir.files" v-bind:dir="dir.name" v-bind:rom="rom" v-bind:key="rom"></rom>
        </div>
    `,
});

Vue.component('directory', {
    props: ['dir'],
    template: `
        <div class='directory'>
            <div>{{ dir.name }}</div>
            <file v-for="file in dir.files" v-bind:file="file" v-bind:key="file"></file>
        </div>
    `,
});

Vue.component('file', {
    props: ['file'],
    template: `
        <div class='file'>
            {{ file }}
        </div>
    `,
});

Vue.component('rom', {
    props: ['dir', 'rom'],
    template: `
        <div class='rom'>
            <a class="smallbutton" v-bind:href="'config/rom/' + encodeURI(dir + '/' + rom)">Configure ROM</a>
            {{ rom }}
        </div>
    `,
});

Vue.component('settings', {
    props: ['serial', 'settings'],
    methods: {
        visible: function(setting) {
            if (setting.readonly == true) {
                return false;
            }
            if (setting.readonly == false) {
                return true;
            }

            // Need to calculate dependent settings.
            pred = setting.readonly;
            for (key in this.settings.settings) {
                if (this.settings.settings[key].name.toLowerCase() == pred.name.toLowerCase()) {
                    for (otherkey in pred.values) {
                        if (pred.values[otherkey] == this.settings.settings[key].current) {
                            return !pred.negate;
                        }
                    }
                    return pred.negate;
                }
            }

            // This is an error, but we don't have a good way of showing
            // the user that there was an error, so make it invisible.
            return false;
        },
        key: function(setting) {
            return setting.name + this.visible(setting);
        },
        all_readonly: function() {
            // If there is a single visible setting, then we are good.
            for (key in this.settings.settings) {
                if (this.visible(this.settings.settings[key])) {
                    return false;
                }
            }

            return true;
        }
    },
    template: `
        <div class="settings">
            <table v-if="!all_readonly()">
                <tr v-for="setting in settings.settings" v-if="visible(setting)" :key="key(setting)">
                    <td>{{ setting.name }}</td>
                    <td>
                        <select v-model="setting.current">
                            <option v-for="(value, key) in setting.values" v-bind:value="key">{{ value }}</option>
                        </select>
                    </td>
                </td>
            </table>
            <span v-if="settings.settings.length == 0" class="italics">
                Settings definition file "{{ serial }}.settings" is missing.
                As a result, we cannot display or edit game settings for this game!
            </span>
            <span v-if="settings.settings.length != 0 && all_readonly()" class="italics">
                Settings definition file "{{ serial }}.settings" specifies no editable settings.
                As a result, we cannot display or edit game settings for this game!
            </span>
        </div>
    `,
});

Vue.component('settingscollection', {
    props: ['settings'],
    template: `
        <div class="settingscollection">
            <h4>System Settings</h4>
            <settings v-bind:serial="settings.serial" v-bind:settings="settings.system"></settings>
            <h4>Game Settings</h4>
            <settings v-bind:serial="settings.serial" v-bind:settings="settings.game"></settings>
        </div>
    `,
});

Vue.component('patch', {
    props: ['game', 'patch', 'enabled'],
    template: `
        <div>
            <div v-if="patch.type == 'patch'">
                <input type="checkbox" :disabled="!enabled" v-bind:id="game.file + patch.file" v-model="patch.enabled" />
                <label v-bind:for="game.file + patch.file">{{ patch.name }}</label>
            </div>
            <div v-if="patch.type == 'settings'">
                <input type="checkbox" :disabled="!enabled" v-bind:id="game.file + patch.file" v-model="patch.enabled" />
                <label v-bind:for="game.file + patch.file">force custom settings on boot</label>
                <settingscollection v-if="patch.enabled" v-bind:settings="patch.settings"></settingscollection>
            </div>
            <div v-if="patch.type == 'sram'">
                Attach SRAM File:
                <select v-bind:id="game.file + patch.file" v-model="patch.active">
                    <option v-for="option in patch.choices" :value="option.v">{{option.t}}</option>
                </select>
            </div>
        </div>
    `,
});

Vue.component('game', {
    props: ['game'],
    template: `
        <div class='patch'>
            <h4>
                <input type="checkbox" v-bind:id="game.file" v-model="game.enabled" />
                <label v-bind:for="game.file">{{ game.name }}</label>
            </h4>
            <div class="patches" v-if="game.enabled">
                <patch v-for="patch in game.patches" v-bind:game="game" v-bind:patch="patch" v-bind:enabled="game.enabled" v-bind:key="patch"></patch>
                <span v-if="game.patches.length == 0" class="italics">no patches available for game</span>
            </div>
        </div>
    `,
});

Vue.component('cabinetconfig', {
    data: function() {
        return {
            cabinet: window.cabinet,
            target: window.cabinet.target,
            info: {
                game: window.cabinet.game,
                status: window.cabinet.status,
                progress: window.cabinet.progress,
            },
            querying: false,
            saving: false,
            saved: false,
            invalid_description: false,
            invalid_timeout: false,
            send_timeout_enabled: window.cabinet.send_timeout !== null,
            send_timeout_seconds: window.cabinet.send_timeout !== null ? window.cabinet.send_timeout : window.timeouts[window.cabinet.target],
        };
    },
    methods: {
        changed: function() {
            this.saved = false;
        },
        save: function() {
            if (this.cabinet.description.length == 0){
                this.invalid_description = true;
            } else {
                this.invalid_description = false;
            }

            if (this.send_timeout_enabled) {
                if (/^\+?(0|[1-9]\d*)$/.test(this.send_timeout_seconds)) {
                    this.invalid_timeout = false;
                    this.cabinet.send_timeout = parseInt(this.send_timeout_seconds);
                } else {
                    this.invalid_timeout = true;
                }
            } else {
                this.invalid_timeout = false;
                this.cabinet.send_timeout = null;
            }

            if (!this.invalid_description && !this.invalid_timeout) {
                this.saving = true;
                this.saved = false;
                axios.post('/cabinets/' + this.cabinet.ip, this.cabinet).then(result => {
                    if (!result.data.error) {
                        if (this.target != result.data.target) {
                            eventBus.$emit('changeCabinetType', true)
                        }
                        this.cabinet = result.data;
                        this.send_timeout_enabled = this.cabinet.send_timeout !== null;
                        this.send_timeout_seconds = this.send_timeout_enabled ? this.cabinet.send_timeout : window.timeouts[this.cabinet.target],
                        this.target = this.cabinet.target;
                        this.saved = true;
                        this.saving = false;
                    }
                });
            }
        },
        query: function() {
            this.querying = true;
            axios.get('/cabinets/' + this.cabinet.ip + '/info').then(result => {
                if (!result.data.error) {
                    this.info.version = result.data.version;
                    this.info.memsize = result.data.memsize;
                    this.info.memavail = result.data.memavail;
                    this.info.available = result.data.available;
                    if (this.info.version) {
                        this.cabinet.version = this.info.version;
                    }
                    this.querying = false;
                }
            });
        },
        remove: function() {
            if (confirm("Are you sure you want to remove this cabinet?")) {
                axios.delete('/cabinets/' + this.cabinet.ip).then(result => {
                    if (!result.data.error) {
                        // Saved, go to home screen
                        window.location.href = '/';
                    }
                });
            }
        },
        refresh: function() {
            axios.get('/cabinets/' + this.cabinet.ip).then(result => {
                if (!result.data.error) {
                    this.info.status = result.data.status;
                    this.info.progress = result.data.progress;
                }
            });
        },
    },
    mounted: function() {
        setInterval(function () {
            this.refresh();
        }.bind(this), 1000);
    },
    watch: {
        'cabinet.target'(newValue) {
            if (!this.send_timeout_enabled) {
                this.send_timeout_seconds = window.timeouts[newValue];
            }
        },
    },
    template: `
        <div>
            <div class='cabinet'>
                <h3>Cabinet Configuration</h3>
                <div class="information top">
                    Target System and NetDimm Version are used to check availability for certain features.
                    They are optional, but it's a good idea to set them correctly. Keyless boot allows you
                    to boot without a PIC by constantly connecting to the net dimm and resetting the reboot
                    timeout. If you are running on a platform that takes awhile to boot, setting a custom
                    send timeout to something longer like 30 seconds can allow stubborn systems to work fine.
                    If you disable management of this cabinet then no games will be sent to it nor will it
                    be checked to see if it is up.
                </div>
                <dl>
                    <dt>IP Address</dt><dd>{{ cabinet.ip }}</dd>
                    <dt>Description</dt><dd>
                        <input v-model="cabinet.description" @input="changed"/>
                        <span class="errorindicator" v-if="invalid_description">invalid description</span>
                    </dd>
                    <dt>Target System</dt><dd>
                        <select v-model="cabinet.target" @input="changed">
                            <option v-for="target in targets" v-bind:value="target">{{ target }}</option>
                        </select>
                    </dd>
                    <dt>BIOS Region</dt><dd>
                        <select v-model="cabinet.region" @input="changed">
                            <option v-for="region in regions" v-bind:value="region">{{ region }}</option>
                        </select>
                    </dd>
                    <dt>NetDimm Version</dt><dd>
                        <select v-model="cabinet.version" @input="changed">
                            <option v-for="version in versions" v-bind:value="version">{{ version }}</option>
                        </select>
                    </dd>
                    <dt>Keyless Boot</dt><dd>
                        <input id="time_hack" type="checkbox" v-model="cabinet.time_hack" />
                        <label for="time_hack">allow boot without key chip</label>
                    </dd>
                    <dt>Send Timeout</dt><dd>
                        <input id="send_timeout_enabled" type="checkbox" v-model="send_timeout_enabled" />
                        <label for="send_timeout_enabled">enable alternative send timeout</label>
                    </dd>
                    <dt v-if="send_timeout_enabled"></dt><dd v-if="send_timeout_enabled">
                        <input v-model="send_timeout_seconds" />
                        <span>seconds</span>
                        <span class="errorindicator" v-if="invalid_timeout">invalid timeout</span>
                    </dd>
                    <dt>Management Enabled</dt><dd>
                        <input id="enabled" type="checkbox" v-model="cabinet.enabled" />
                        <label for="enabled">allow management of this cabinet</label>
                    </dd>
                </dl>
                <div class="update">
                    <button v-on:click="save">Update Properties</button>
                    <button v-on:click="remove">Remove Cabinet</button>
                    <span class="savingindicator" v-if="saving"><img src="/static/loading-16.gif" width=16 height=16 /> saving...</span>
                    <span class="successindicator" v-if="saved">&check; saved</span>
                </div>
            </div>
            <div class='cabinet'>
                <h3>Cabinet Information</h3>
                <div class="information top">
                    Querying the NetDimm will attempt to load some firmware properties. If the version
                    can be identified, it will be selected for you in the NetDimm Version in the above
                    section.
                </div>
                <dl>
                    <dt>Game</dt>
                    <dd>{{ info.game }}</dd>
                    <dt>Status</dt><dd><state v-bind:status="info.status" v-bind:progress="info.progress"></state></dd>
                    <dt v-if="info.available">NetDimm Memory Size</dt>
                    <dd v-if="info.available">{{ info.memsize }} MB</dd>
                    <dt v-if="info.available">NetDimm Available Game Memory</dt>
                    <dd v-if="info.available">{{ info.memavail }} MB</dd>
                </dl>
                <div class="query">
                    <button v-on:click="query" :disabled="info.status == 'turned_off' || info.status == 'power_cycle' || info.status == 'send_game' || info.status == 'startup' || info.status == 'wait_power_on'">Query Firmware Information</button>
                    <span class="queryindicator" v-if="querying"><img src="/static/loading-16.gif" width=16 height=16 /> querying...</span>
                </div>
            </div>
        </div>
    `,
});

Vue.component('romconfig', {
    data: function() {
        return {
            names: window.names,
            saved: false,
            saving: false,
        };
    },
    methods: {
        changed: function() {
            this.saved = false;
        },
        save: function() {
            this.saved = false;
            this.saving = true;
            axios.post('/roms/' + encodeURI(filename), this.names).then(result => {
                if (!result.data.error) {
                    this.names = result.data;
                    this.saved = true;
                    this.saving = false;
                }
            });
        },
    },
    template: `
        <div class='romconfig'>
            <dl>
                <dt>Filename</dt><dd>{{ filename }}</dd>
                <dt>Name (japan)</dt><dd><input v-model="names.japan" @input="changed"/></dd>
                <dt>Name (usa)</dt><dd><input v-model="names.usa" @input="changed"/></dd>
                <dt>Name (export)</dt><dd><input v-model="names.export" @input="changed"/></dd>
                <dt>Name (korea)</dt><dd><input v-model="names.korea" @input="changed"/></dd>
                <dt>Name (australia)</dt><dd><input v-model="names.australia" @input="changed"/></dd>
            </dl>
            <button v-on:click="save">Update Names</button>
            <span class="savingindicator" v-if="saving"><img src="/static/loading-16.gif" width=16 height=16 /> saving...</span>
            <span class="successindicator" v-if="saved">&check; saved</span>
            <div class="information">
                Game names are pulled out of the ROM header if possible, and if not they are taken
                from the ROM filename. If you want to change them so they look better on the main
                page drop-downs you can do so here.
            </div>
        </div>
    `,
});

Vue.component('outletconfig', {
    data: function() {
        return {
            cabinet: window.cabinet,
            saving: false,
            saved: false,
            invalid_ip: false,
            invalid_outlet: false,
            invalid_query_oid: false,
            invalid_query_on_value: false,
            invalid_query_off_value: false,
            invalid_update_oid: false,
            invalid_update_on_value: false,
            invalid_update_off_value: false,
        };
    },
    methods: {
        changed: function() {
            this.saved = false;
        },
        poweron: function() {
            axios.post('/cabinets/' + this.cabinet.ip + '/power/on', {'admin': true}).then(result => {
                if (!result.data.error) {
                    this.cabinet.power_state = result.data.power_state;
                }
            });
        },
        poweroff: function() {
            axios.post('/cabinets/' + this.cabinet.ip + '/power/off', {'admin': true}).then(result => {
                if (!result.data.error) {
                    this.cabinet.power_state = result.data.power_state;
                }
            });
        },
        refresh: function() {
            axios.get('/cabinets/' + this.cabinet.ip + '/power').then(result => {
                if (!result.data.error) {
                    this.cabinet.power_state = result.data.power_state;
                }
            });
        },
        save: function() {
            this.invalid_ip = false;
            this.invalid_outlet = false;
            this.invalid_query_oid = false;
            this.invalid_update_oid = false;
            this.invalid_query_on_value = false;
            this.invalid_query_off_value = false;
            this.invalid_update_on_value = false;
            this.invalid_update_off_value = false;

            if (cabinet.outlet.type != 'none') {
                if (/^(25[0-5]|2[0-4][0-9]|[01]?[0-9][0-9]?)\.(25[0-5]|2[0-4][0-9]|[01]?[0-9][0-9]?)\.(25[0-5]|2[0-4][0-9]|[01]?[0-9][0-9]?)\.(25[0-5]|2[0-4][0-9]|[01]?[0-9][0-9]?)$/.test(this.cabinet.outlet.host)) {
                    this.invalid_ip = false;
                } else {
                    this.invalid_ip = true;
                }

                if (cabinet.outlet.type == 'ap7900') {
                    if (/^[1-8]$/.test(this.cabinet.outlet.outlet)) {
                        this.invalid_outlet = false;
                    } else {
                        this.invalid_outlet = true;
                    }
                }

                if (cabinet.outlet.type == 'snmp') {
                    if (!this.cabinet.outlet.query_oid || this.cabinet.outlet.query_oid.length == 0) {
                        this.invalid_query_oid = true;
                    } else {
                        this.invalid_query_oid = false;
                    }
                    if (!this.cabinet.outlet.update_oid || this.cabinet.outlet.update_oid.length == 0) {
                        this.invalid_update_oid = true;
                    } else {
                        this.invalid_update_oid = false;
                    }
                    if (/^\d+$/.test(this.cabinet.outlet.query_on_value)) {
                        this.invalid_query_on_value = false;
                    } else {
                        this.invalid_query_on_value = true;
                    }
                    if (/^\d+$/.test(this.cabinet.outlet.query_off_value)) {
                        this.invalid_query_off_value = false;
                    } else {
                        this.invalid_query_off_value = true;
                    }
                    if (/^\d+$/.test(this.cabinet.outlet.update_on_value)) {
                        this.invalid_update_on_value = false;
                    } else {
                        this.invalid_update_on_value = true;
                    }
                    if (/^\d+$/.test(this.cabinet.outlet.update_off_value)) {
                        this.invalid_update_off_value = false;
                    } else {
                        this.invalid_update_off_value = true;
                    }
                }

                if (cabinet.outlet.type == 'np-02b') {
                    if (/^[1-2]$/.test(this.cabinet.outlet.outlet)) {
                        this.invalid_outlet = false;
                    } else {
                        this.invalid_outlet = true;
                    }
                }
            }

            if (
                !this.invalid_ip && !this.invalid_outlet && !this.invalid_query_oid && !this.invalid_update_oid &&
                !this.invalid_query_on_value && !this.invalid_query_off_value && !this.invalid_update_on_value &&
                !this.invalid_update_off_value
            ) {
                this.saving = true;
                this.saved = false;
                axios.post(
                    '/cabinets/' + this.cabinet.ip + '/outlet',
                    {outlet: this.cabinet.outlet, controllable: this.cabinet.controllable, power_cycle: this.cabinet.power_cycle}
                ).then(result => {
                    if (!result.data.error) {
                        this.cabinet.outlet = result.data.outlet;
                        this.cabinet.controllable = result.data.controllable;
                        this.cabinet.power_cycle = result.data.power_cycle;
                        this.saved = true;
                        this.saving = false;
                    }
                });
            }
        },
    },
    mounted: function() {
        setInterval(function () {
            this.refresh();
        }.bind(this), 1000);
    },
    template: `
        <div>
            <div class='outlet'>
                <h3>Smart Outlet Configuration</h3>
                <div class="information top">
                    Configuring a smart outlet that this cabinet is plugged into will allow you to control the power
                    state of the cabinet from the game select screen as well as request a cabinet power cycle in order
                    to switch games. This can help when loading a game over another stubborn game which will not
                    reboot.
                </div>
                <dl>
                    <dt>Attached Smart Outlet</dt><dd>
                        <select v-model="cabinet.outlet.type">
                            <option v-for="outlet in outlets" v-bind:value="outlet">{{ outlet }}</option>
                        </select>
                    </dd>
                    <dt v-if="cabinet.outlet.type != 'none'">IP Address</dt><dd v-if="cabinet.outlet.type != 'none'">
                        <input v-model="cabinet.outlet.host" />
                        <span class="errorindicator" v-if="invalid_ip">invalid IP address</span>
                    </dd>
                    <dt v-if="cabinet.outlet.type == 'ap7900' || cabinet.outlet.type =='np-02b'">Outlet Number</dt>
                    <dd v-if="cabinet.outlet.type == 'ap7900' || cabinet.outlet.type =='np-02b'">
                        <input v-model="cabinet.outlet.outlet" />
                        <span class="errorindicator" v-if="invalid_outlet">invalid outlet</span>
                    </dd>
                    <dt v-if="cabinet.outlet.type == 'snmp'">Query OID</dt><dd v-if="cabinet.outlet.type == 'snmp'">
                        <input v-model="cabinet.outlet.query_oid" />
                        <span class="errorindicator" v-if="invalid_query_oid">invalid query OID</span>
                    </dd>
                    <dt v-if="cabinet.outlet.type == 'snmp'">Query On Value</dt><dd v-if="cabinet.outlet.type == 'snmp'">
                        <input v-model="cabinet.outlet.query_on_value" />
                        <span class="errorindicator" v-if="invalid_query_on_value">invalid query on value</span>
                    </dd>
                    <dt v-if="cabinet.outlet.type == 'snmp'">Query Off Value</dt><dd v-if="cabinet.outlet.type == 'snmp'">
                        <input v-model="cabinet.outlet.query_off_value" />
                        <span class="errorindicator" v-if="invalid_query_off_value">invalid query off value</span>
                    </dd>
                    <dt v-if="cabinet.outlet.type == 'snmp'">Update OID</dt><dd v-if="cabinet.outlet.type == 'snmp'">
                        <input v-model="cabinet.outlet.update_oid" />
                        <span class="errorindicator" v-if="invalid_update_oid">invalid update OID</span>
                    </dd>
                    <dt v-if="cabinet.outlet.type == 'snmp'">Update On Value</dt><dd v-if="cabinet.outlet.type == 'snmp'">
                        <input v-model="cabinet.outlet.update_on_value" />
                        <span class="errorindicator" v-if="invalid_update_on_value">invalid update on value</span>
                    </dd>
                    <dt v-if="cabinet.outlet.type == 'snmp'">Update Off Value</dt><dd v-if="cabinet.outlet.type == 'snmp'">
                        <input v-model="cabinet.outlet.update_off_value" />
                        <span class="errorindicator" v-if="invalid_update_off_value">invalid update off value</span>
                    </dd>
                    <dt v-if="cabinet.outlet.type == 'snmp' || cabinet.outlet.type == 'ap7900'">Read Community</dt>
                    <dd v-if="cabinet.outlet.type == 'snmp' || cabinet.outlet.type == 'ap7900'">
                        <input v-model="cabinet.outlet.read_community" />
                    </dd>
                    <dt v-if="cabinet.outlet.type == 'snmp' || cabinet.outlet.type == 'ap7900'">Write Community</dt>
                    <dd v-if="cabinet.outlet.type == 'snmp' || cabinet.outlet.type == 'ap7900'">
                        <input v-model="cabinet.outlet.write_community" />
                    </dd>
                    <dt v-if="cabinet.outlet.type == 'np-02b'">Username</dt>
                    <dd v-if="cabinet.outlet.type == 'np-02b'">
                        <input v-model="cabinet.outlet.username" />
                    </dd>
                    <dt v-if="cabinet.outlet.type == 'np-02b'">Password</dt>
                    <dd v-if="cabinet.outlet.type == 'np-02b'">
                        <input v-model="cabinet.outlet.password" />
                    </dd>
                    <dt v-if="cabinet.outlet.type != 'none'">User Controllable</dt><dd v-if="cabinet.outlet.type != 'none'">
                        <input id="controllable" type="checkbox" v-model="cabinet.controllable" />
                        <label for="controllable">allow users to turn cabinet on or off</label>
                    </dd>
                    <dt v-if="cabinet.outlet.type != 'none'">Power Cycle on Load</dt><dd v-if="cabinet.outlet.type != 'none'">
                        <input id="power_cycle" type="checkbox" v-model="cabinet.power_cycle" />
                        <label for="power_cycle">power cycle the cabinet when loading a new game</label>
                    </dd>
                </dl>
                <div class="update">
                    <button v-on:click="save">Update Outlet Configuration</button>
                    <span class="savingindicator" v-if="saving"><img src="/static/loading-16.gif" width=16 height=16 /> saving...</span>
                    <span class="successindicator" v-if="saved">&check; saved</span>
                </div>
            </div>
            <div class='outlet' v-if="cabinet.power_state != 'disabled'">
                <h3>Smart Outlet Information</h3>
                <div class="information top">
                    Status information about the smart outlet configured above is shown here. If the outlet was detected
                    properly, then the current state will be displayed alongside the ability to turn the outlet on or
                    off. If the outlet was not detected, then the ability to turn the outlet on or off will be disabled.
                </div>
                <dl>
                    <dt>Outlet Status</dt><dd>
                        <span v-if="cabinet.power_state == 'disabled'">disabled</span>
                        <span v-if="cabinet.power_state == 'unknown'">misconfigured or not found</span>
                        <span v-if="cabinet.power_state == 'on'">on</span>
                        <span v-if="cabinet.power_state == 'off'">off</span>
                    </dd>
                </dl>
                <button v-if="cabinet.power_state == 'off'" v-on:click="poweron">Turn Cabinet On</button>
                <button v-if="cabinet.power_state == 'on'" v-on:click="poweroff">Turn Cabinet Off</button>
            </div>
        </div>
    `,
});

Vue.component('availablegames', {
    data: function() {
        axios.get('/cabinets/' + cabinet.ip + '/games').then(result => {
            if (!result.data.error) {
                this.games = result.data.games;
                this.loading = false;
            }
        });
        return {
            loading: true,
            saved: false,
            saving: false,
            games: {}
        };
    },
    methods: {
        update: function() {
            this.loading = true;
            axios.get('/cabinets/' + cabinet.ip + '/games').then(result => {
                if (!result.data.error) {
                    this.games = result.data.games;
                    this.loading = false;
                }
            });
        },
        save: function() {
            this.saved = false;
            this.saving = true;
            axios.post('/cabinets/' + cabinet.ip + '/games', {games: this.games}).then(result => {
                if (!result.data.error) {
                    this.games = result.data.games;
                    this.saved = true;
                    this.saving = false;
                }
            });
        },
    },
    created () {
        eventBus.$on('changeCabinetType', this.update);
    },
    template: `
        <div class='gamelist'>
            <h3>Available Games For This Cabinet</h3>
            <div class="information top">
                Games that can be sent to this cabinet, as well as the applicable patches and settings
                that can be enabled for those games. Check off every game you want available for this
                cabinet and they will show up on the game selection dropdown on the main screen for this
                cabinet. If you select patches or settings for a particular game, they will be applied
                to the game when it is sent to the cabinet.
            </div>
            <game v-if="!loading" v-for="game in games" v-bind:game="game" v-bind:key="game"></game>
            <div v-if="loading"><img src="/static/loading-16.gif" width=16 height=16 /> loading...</div>
            <div v-if="!loading && Object.keys(games).length === 0">no applicable games</div>
            <div>&nbsp;</div>
            <button v-on:click="save">Update Games</button>
            <span class="savingindicator" v-if="saving"><img src="/static/loading-16.gif" width=16 height=16 /> saving...</span>
            <span class="successindicator" v-if="saved">&check; saved</span>
        </div>
    `,
});

Vue.component('availablepatches', {
    data: function() {
        axios.get('/patches/' + encodeURI(filename)).then(result => {
            if (!result.data.error) {
                this.patches = result.data.patches;
                this.loading = false;
            }
        });
        return {
            loading: true,
            patches: {}
        };
    },
    methods: {
        recalculatepatches: function() {
            this.loading = true;
            axios.delete('/patches/' + encodeURI(filename)).then(result => {
                if (!result.data.error) {
                    this.patches = result.data.patches;
                    this.loading = false;
                }
            });
        },
    },
    template: `
        <div class='patchlist'>
            <h3>Available Patches For This Rom</h3>
            <directory v-if="!loading" v-for="patch in patches" v-bind:dir="patch" v-bind:key="patch"></directory>
            <div v-if="loading"><img src="/static/loading-16.gif" width=16 height=16 /> loading...</div>
            <div v-if="!loading && Object.keys(patches).length === 0">no applicable patches</div>
            <div>&nbsp;</div>
            <button v-on:click="recalculatepatches">Recalculate Patch Files</button>
        </div>
    `,
});

Vue.component('availablesrams', {
    data: function() {
        axios.get('/srams/' + encodeURI(filename)).then(result => {
            if (!result.data.error) {
                this.srams = result.data.srams;
                this.loading = false;
            }
        });
        return {
            loading: true,
            srams: {}
        };
    },
    methods: {
        recalculatesrams: function() {
            this.loading = true;
            axios.delete('/srams/' + encodeURI(filename)).then(result => {
                if (!result.data.error) {
                    this.srams = result.data.srams;
                    this.loading = false;
                }
            });
        },
    },
    template: `
        <div class='sramlist'>
            <h3>Available SRAM Files For This Rom</h3>
            <directory v-if="!loading" v-for="sram in srams" v-bind:dir="sram" v-bind:key="sram"></directory>
            <div v-if="loading"><img src="/static/loading-16.gif" width=16 height=16 /> loading...</div>
            <div v-if="!loading && Object.keys(srams).length === 0">no applicable SRAM files</div>
            <div>&nbsp;</div>
            <button v-on:click="recalculatesrams">Recalculate SRAM Files</button>
        </div>
    `,
});

Vue.component('availabledefinitions', {
    data: function() {
        axios.get('/settings/' + encodeURI(filename)).then(result => {
            if (!result.data.error) {
                this.settings = result.data.settings;
                this.loading = false;
            }
        });
        return {
            loading: true,
            settings: {}
        };
    },
    template: `
        <div class='patchlist'>
            <h3>Available Settings Definitions For This Rom</h3>
            <directory v-if="!loading" v-for="setting in settings" v-bind:dir="setting" v-bind:key="setting"></directory>
            <div v-if="loading"><img src="/static/loading-16.gif" width=16 height=16 /> loading...</div>
            <div v-if="!loading && Object.keys(settings).length === 0">no applicable settings definitions</div>
        </div>
    `,
});

Vue.component('newcabinet', {
    data: function() {
        return {
            cabinet: {
                'description': '',
                'ip': '',
                'time_hack': false,
                'send_timeout_enabled': false,
                'send_timeout_seconds': window.timeouts[window.targets[0]],
                'region': window.regions[0],
                'target': window.targets[0],
                'version': window.versions[0],
            },
            invalid_ip: false,
            invalid_description: false,
            invalid_timeout: false,
            duplicate_ip: false,
        };
    },
    watch: {
        'cabinet.target'(newValue) {
            if (!this.cabinet.send_timeout_enabled) {
                this.cabinet.send_timeout_seconds = window.timeouts[newValue];
            }
        },
    },
    methods: {
        save: function() {
            if (this.cabinet.description.length == 0){
                this.invalid_description = true;
            } else {
                this.invalid_description = false;
            }
            if (/^(25[0-5]|2[0-4][0-9]|[01]?[0-9][0-9]?)\.(25[0-5]|2[0-4][0-9]|[01]?[0-9][0-9]?)\.(25[0-5]|2[0-4][0-9]|[01]?[0-9][0-9]?)\.(25[0-5]|2[0-4][0-9]|[01]?[0-9][0-9]?)$/.test(this.cabinet.ip)) {
                this.invalid_ip = false;
            } else {
                this.invalid_ip = true;
            }

            if (this.cabinet.send_timeout_enabled) {
                if (/^\+?(0|[1-9]\d*)$/.test(this.cabinet.send_timeout_seconds)) {
                    this.invalid_timeout = false;
                    this.cabinet.send_timeout = parseInt(this.cabinet.send_timeout_seconds);
                } else {
                    this.invalid_timeout = true;
                }
            } else {
                this.invalid_timeout = false;
                this.cabinet.send_timeout = null;
            }

            if (!this.invalid_ip && !this.invalid_description && !this.invalid_timeout) {
                axios.put('/cabinets/' + this.cabinet.ip, this.cabinet).then(result => {
                    if (!result.data.error) {
                        // Saved, go to cabinet screen
                        window.location.href = '/config/cabinet/' + result.data.ip;
                    } else {
                        // Cabinet IP already in use
                        this.duplicate_ip = true;
                    }
                });
            }
        },
    },
    template: `
        <div class='cabinet'>
            <dl>
                <dt>IP Address</dt><dd>
                    <input v-model="cabinet.ip" />
                    <span class="errorindicator" v-if="invalid_ip">invalid IP address</span>
                    <span class="errorindicator" v-if="duplicate_ip">IP address already in use</span>
                </dd>
                <dt>Description</dt><dd>
                    <input v-model="cabinet.description" />
                    <span class="errorindicator" v-if="invalid_description">invalid description</span>
                </dd>
                <dt>Target System</dt><dd>
                    <select v-model="cabinet.target">
                        <option v-for="target in targets" v-bind:value="target">{{ target }}</option>
                    </select>
                </dd>
                <dt>BIOS Region</dt><dd>
                    <select v-model="cabinet.region">
                        <option v-for="region in regions" v-bind:value="region">{{ region }}</option>
                    </select>
                </dd>
                <dt>NetDimm Version</dt><dd>
                    <select v-model="cabinet.version">
                        <option v-for="version in versions" v-bind:value="version">{{ version }}</option>
                    </select>
                </dd>
                <dt>Keyless Boot</dt><dd>
                    <input id="time_hack" type="checkbox" v-model="cabinet.time_hack" />
                    <label for="time_hack">allow boot without key chip</label>
                </dd>
                <dt>Send Timeout</dt><dd>
                    <input id="send_timeout_enabled" type="checkbox" v-model="cabinet.send_timeout_enabled" />
                    <label for="send_timeout_enabled">enable alternative send timeout</label>
                </dd>
                <dt v-if="cabinet.send_timeout_enabled"></dt><dd v-if="cabinet.send_timeout_enabled">
                    <input v-model="cabinet.send_timeout_seconds" />
                    <span>seconds</span>
                    <span class="errorindicator" v-if="invalid_timeout">invalid timeout</span>
                </dd>
            </dl>
            <button v-on:click="save">Add Cabinet</button>
            <div class="information">
                Target System and NetDimm Version are used to check availability for certain features.
                They are optional, but it's a good idea to set them correctly. Keyless boot allows you
                to boot without a PIC by constantly connecting to the net dimm and resetting the reboot
                timeout. If you are running on a platform that takes awhile to boot, setting a custom
                send timeout to something longer like 30 seconds can allow stubborn systems to work fine.
            </div>
        </div>
    `,
});

Vue.component('cabinetlist', {
    data: function() {
        return {
            cabinets: window.cabinets,
            allowRefresh: true,
        };
    },
    created () {
        eventBus.$on('selectingNewGame', this.blockUpdates);
        eventBus.$on('selectedNewGame', this.unblockUpdates);
    },
    methods: {
        refresh: function() {
            axios.get('/cabinets').then(result => {
                if (!result.data.error) {
                    if (this.allowRefresh) {
                        this.cabinets = result.data.cabinets;
                    }
                }
            });
        },
        blockUpdates: function() {
            this.allowRefresh = false;
        },
        unblockUpdates: function() {
            this.allowRefresh = true;
        },
    },
    mounted: function() {
        setInterval(function () {
            this.refresh();
        }.bind(this), 1000);
    },
    template: `
        <div>
            <cabinet v-for="cabinet in cabinets" v-bind:cabinet="cabinet" v-bind:key="cabinet.ip"></cabinet>
        </div>
    `,
});

Vue.component('roms', {
    data: function() {
        return {
            roms: window.roms,
            deletedpatches: false,
            deletedsrams: false,
        };
    },
    methods: {
        refresh: function() {
            axios.get('/roms').then(result => {
                if (!result.data.error) {
                    this.roms = result.data.roms;
                }
            });
        },
        recalculatepatches: function() {
            this.deletedpatches = false;
            axios.delete('/patches').then(result => {
                if (!result.data.error) {
                    this.deletedpatches = true;
                }
            });
        },
        recalculatesrams: function() {
            this.deletedsrams = false;
            axios.delete('/srams').then(result => {
                if (!result.data.error) {
                    this.deletedsrams = true;
                }
            });
        },
    },
    mounted: function() {
        setInterval(function () {
            this.refresh();
        }.bind(this), 5000);
    },
    template: `
        <div class='romlist'>
            <h3>Available ROMs</h3>
            <romlist v-for="rom in roms" v-bind:dir="rom" v-bind:key="rom"></romlist>
            <div>&nbsp;</div>
            <button v-on:click="recalculatepatches">Recalculate All Patch Files</button>
            <span class="successindicator" v-if="deletedpatches">&check; recalculated</span>
            <div>&nbsp;</div>
            <button v-on:click="recalculatesrams">Recalculate All SRAM Files</button>
            <span class="successindicator" v-if="deletedsrams">&check; recalculated</span>
        </div>
    `,
});

Vue.component('patches', {
    data: function() {
        return {
            patches: window.patches,
        };
    },
    methods: {
        refresh: function() {
            axios.get('/patches').then(result => {
                if (!result.data.error) {
                    this.patches = result.data.patches;
                }
            });
        },
    },
    mounted: function() {
        setInterval(function () {
            this.refresh();
        }.bind(this), 5000);
    },
    template: `
        <div class='patchlist'>
            <h3>Available Patches</h3>
            <directory v-for="patch in patches" v-bind:dir="patch" v-bind:key="patch"></directory>
        </div>
    `,
});

Vue.component('srams', {
    data: function() {
        return {
            srams: window.srams,
        };
    },
    methods: {
        refresh: function() {
            axios.get('/srams').then(result => {
                if (!result.data.error) {
                    this.srams = result.data.srams;
                }
            });
        },
    },
    mounted: function() {
        setInterval(function () {
            this.refresh();
        }.bind(this), 5000);
    },
    template: `
        <div class='sramlist'>
            <h3>Available SRAM Files</h3>
            <directory v-for="sram in srams" v-bind:dir="sram" v-bind:key="sram"></directory>
        </div>
    `,
});

Vue.component('definitions', {
    data: function() {
        return {
            settings: window.settings,
        };
    },
    methods: {
        refresh: function() {
            axios.get('/settings').then(result => {
                if (!result.data.error) {
                    this.settings = result.data.settings;
                }
            });
        },
    },
    mounted: function() {
        setInterval(function () {
            this.refresh();
        }.bind(this), 5000);
    },
    template: `
        <div class='patchlist'>
            <h3>Available Settings Definitions</h3>
            <directory v-for="setting in settings" v-bind:dir="setting" v-bind:key="setting"></directory>
        </div>
    `,
});

Vue.component('systemconfig', {
    data: function() {
        if (window.cabinets.length == 0) {
            // Force admin mode even after adding the first cabinet if we're currently empty.
            setCookie('admin', 'true', 7);
        }
        return {
            admin: window.cabinets.length == 0 || getCookie('admin') == 'true',
        };
    },
    methods: {
        hide: function() {
            setCookie('admin', 'false', -1);
            this.admin = false;
            eventBus.$emit('changeConfigButtons', true)
        },
        update: function() {
            this.admin = getCookie('admin') == 'true';
        },
    },
    created () {
        eventBus.$on('changeConfigButtons', this.update);
    },
    template: `
        <div v-if="admin" class="config">
            <div>
                <a class="button" href="/addcabinet">Add New Cabinet</a>
                <a class="button" href="/config">Configure System</a>
                <button :disabled="window.cabinets.length < 1" v-on:click="hide">Hide Config Buttons</button>
            </div>
            <div v-if="window.cabinets.length == 0" class="information">
                Once you add your first cabinet, you will have the option
                to hide this config section so the page will be prettier.
            </div>
            <div v-if="window.cabinets.length > 0" class="information">
                Once you click "Hide Config Buttons", you can get them back again
                by typing "config" into your browser window on any screen.
            </div>
        </div>
    `,
});
