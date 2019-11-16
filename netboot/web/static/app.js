Vue.config.devtools = true;

Vue.component('state', {
    props: ['status', 'progress'],
    template: `
        <span>
            <span v-if="status == 'startup' || status == 'wait_power_on'">powered off</span>
            <span v-if="status == 'wait_power_off'">running game</span>
            <span v-if="status == 'send_game'">sending game ({{ progress }}% complete)</span>
        </span>
    `,
});

Vue.component('cabinet', {
    props: ['cabinet'],
    template: `
        <div class='cabinet'>
            <h3>{{ cabinet.description }}</h3>
            <dl>
                <dt>Game</dt><dd>{{ cabinet.game }}</dd>
                <dt>Status</dt><dd><state v-bind:status="cabinet.status" v-bind:progress="cabinet.progress"></state></dd>
            </dl>
            <a class="button" v-bind:href="'config/cabinet/' + cabinet.ip">Configure Cabinet</a>
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

Vue.component('patch', {
    props: ['patch'],
    template: `
        <div>
            <input type="checkbox" v-bind:id="patch.file" v-model="patch.enabled" />
            <label v-bind:for="patch.file">{{ patch.name }}</label>
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
            <div class="patches">
                <patch v-for="patch in game.patches" v-bind:patch="patch" v-bind:key="patch"></patch>
                <span v-if="game.patches.length == 0">no patches available for game</span>
            </div>
        </div>
    `,
});

Vue.component('cabinetconfig', {
    data: function() {
        return {
            cabinet: window.cabinet,
            saved: false,
        };
    },
    methods: {
        changed: function() {
            this.saved = false;
        },
        save: function() {
            axios.post('/cabinets/' + this.cabinet.ip, this.cabinet).then(result => {
                if (!result.data.error) {
                    this.cabinet = result.data;
                    this.saved = true;
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
    },
    template: `
        <div class='cabinet'>
            <dl>
                <dt>IP Address</dt><dd>{{ cabinet.ip }}</dd>
                <dt>Description</dt><dd><input v-model="cabinet.description" @input="changed"/></dd>
                <dt>Region</dt><dd>
                    <select v-model="cabinet.region" @input="changed">
                        <option v-for="region in regions" v-bind:value="region">{{ region }}</option>
                    </select>
                </dd>
                <dt>Target</dt><dd>
                    <select v-model="cabinet.target" @input="changed">
                        <option v-for="target in targets" v-bind:value="target">{{ target }}</option>
                    </select>
                </dd>
                <dt>NetDimm Version</dt><dd>
                    <select v-model="cabinet.version" @input="changed">
                        <option v-for="version in versions" v-bind:value="version">{{ version }}</option>
                    </select>
                </dd>
            </dl>
            <button v-on:click="save">Update Properties</button>
            <button v-on:click="remove">Remove Cabinet</button>
            <span class="successindicator" v-if="saved">&check; saved</span>
        </div>
    `,
});

Vue.component('romconfig', {
    data: function() {
        return {
            names: window.names,
            saved: false,
        };
    },
    methods: {
        changed: function() {
            this.saved = false;
        },
        save: function() {
            axios.post('/roms/' + encodeURI(filename), this.names).then(result => {
                if (!result.data.error) {
                    this.names = result.data;
                    this.saved = true;
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
            <span class="successindicator" v-if="saved">&check; saved</span>
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
            games: {}
        };
    },
    methods: {
        save: function() {
            this.saved = false;
            axios.post('/cabinets/' + cabinet.ip + '/games', {games: this.games}).then(result => {
                if (!result.data.error) {
                    this.games = result.data.games;
                    this.saved = true;
                }
            });
        },
    },
    template: `
        <div class='gamelist'>
            <h3>Available Games For This Cabinet</h3>
            <game v-if="!loading" v-for="game in games" v-bind:game="game" v-bind:key="game"></game>
            <div v-if="loading">loading...</div>
            <div v-if="!loading && Object.keys(games).length === 0">no applicable games</div>
            <div>&nbsp;</div>
            <button v-on:click="save">Update Games</button>
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
        recalculate: function() {
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
            <div v-if="loading">loading...</div>
            <div v-if="!loading && Object.keys(patches).length === 0">no applicable patches</div>
            <div>&nbsp;</div>
            <button v-on:click="recalculate">Recalculate Patch Files</button>
        </div>
    `,
});

Vue.component('newcabinet', {
    data: function() {
        return {
            cabinet: {
                'description': '',
                'ip': '',
                'region': window.regions[0],
                'target': window.targets[0],
                'version': window.versions[0],
                'filename': window.roms[0].file,
            },
            invalid_ip: false,
            invalid_description: false,
            duplicate_ip: false,
        };
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
            } else{
                this.invalid_ip = true;
            }

            if (!this.invalid_ip && !this.invalid_description) {
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
                <dt>Region</dt><dd>
                    <select v-model="cabinet.region">
                        <option v-for="region in regions" v-bind:value="region">{{ region }}</option>
                    </select>
                </dd>
                <dt>Target</dt><dd>
                    <select v-model="cabinet.target">
                        <option v-for="target in targets" v-bind:value="target">{{ target }}</option>
                    </select>
                </dd>
                <dt>NetDimm Version</dt><dd>
                    <select v-model="cabinet.version">
                        <option v-for="version in versions" v-bind:value="version">{{ version }}</option>
                    </select>
                </dd>
            </dl>
            <button v-on:click="save">Save Properties</button>
        </div>
    `,
});

Vue.component('cabinetlist', {
    data: function() {
        return {
            cabinets: window.cabinets,
        };
    },
    methods: {
        refresh: function() {
            axios.get('/cabinets').then(result => {
                if (!result.data.error) {
                    this.cabinets = result.data.cabinets;
                }
            });
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
            deleted: false,
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
        recalculate: function() {
            this.deleted = false;
            axios.delete('/patches').then(result => {
                if (!result.data.error) {
                    this.deleted = true;
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
            <button v-on:click="recalculate">Recalculate All Patch Files</button>
            <span class="successindicator" v-if="deleted">&check; recalculated</span>
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
