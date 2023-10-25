
// DOM elements
const statusElement = document.getElementById('status');
const output = document.getElementById('output');
const canvas = document.getElementById('canvas');
const code = document.getElementById('code');
const progressElement = document.getElementById('progress');
const spinnerElement = document.getElementById('spinner');
const volumeElementFullScreen = document.getElementById('fullscreen_volume_icon');
const volumeElement = document.getElementById('volume_icon');


// Audio Context Setup
var audioContext;

window.addEventListener('load', init, false);
function init() {

    try {
        window.AudioContext = window.AudioContext || window.webkitAudioContext;
        audioContext = new AudioContext();
    }
    catch (e) {
        console.log("AudioContext not supported on this Browser.")
    }
}

//detecting keyboard layout...

//define valid layouts (this can be gotten by running the emulator with -keymap)
const layouts = [
    'en-us',
    'en-us-int',
    'en-gb',
    'sv',
    'de',
    'da',
    'it',
    'pl',
    'nb',
    'hu',
    'es',
    'fi',
    'pt-br',
    'cz',
    'jp',
    'fr',
    'de-ch',
    'en-us-dvo',
    'et',
    'fr-be',
    'fr-ca',
    'is',
    'pt',
    'hr',
    'sk',
    'sl',
    'lv',
    'lt'
];

lang = getFirstBrowserLanguage().toLowerCase().trim();

if (layouts.includes(lang)) {
    logOutput('Using keyboard map: ' + lang);
} else {
    logOutput('Language (' + lang + ') not found in keymaps so using keyboard map: en-us');
    lang = 'en-us';
}

var url = new URL(window.location.href);
var manifest_link = url.searchParams.get("manifest");

var emuArguments = ['-keymap', lang, '-rtc'];

if (manifest_link) {
    openFs();
}

var Module = {

    preRun: [
        function () { //Set the keyboard handling element (it's document by default). Keystrokes are stopped from propagating by emscripten, maybe there's an option to disable this?
            ENV.SDL_EMSCRIPTEN_KEYBOARD_ELEMENT = "#canvas";
        },
        function () {

            if (manifest_link) {
                loadManifestLink();
            }
        }
    ],
    postRun: [
        function () {
            canvas.focus();
        }
    ],
    arguments: emuArguments,
    print: (function () {

        if (output) output.value = ''; // clear browser cache
        return function (text) {
            if (arguments.length > 1) text = Array.prototype.slice.call(arguments).join(' ');
            logOutput(text);
        };
    })(),
    printErr: function (text) {
        if (arguments.length > 1) text = Array.prototype.slice.call(arguments).join(' ');

        // filtering out some known issues for easier reporting from people who have startup problems
        if (text.startsWith('wasm streaming compile failed:') ||
            text.startsWith('falling back to ArrayBuffer instantiation') ||
            text.startsWith('Calling stub instead of sigaction')) {
            logOutput("[known behavior] " + text);
            return;
        }

        logError(text);


    },
    canvas: (function () {

        // As a default initial behavior, pop up an alert when webgl context is lost. To make your
        // application robust, you may want to override this behavior before shipping!
        // See http://www.khronos.org/registry/webgl/specs/latest/1.0/#5.15.2
        canvas.addEventListener("webglcontextlost", function (e) {
            alert('WebGL context lost. You will need to reload the page.');
            e.preventDefault();
        }, false);
        return canvas;
    })(),
    setStatus: function (text) {
        if (!Module.setStatus.last) Module.setStatus.last = {
            time: Date.now(),
            text: ''
        };
        if (text === Module.setStatus.last.text) return;
        const m = text.match(/([^(]+)\((\d+(\.\d+)?)\/(\d+)\)/);
        let now = Date.now();
        if (m && now - Module.setStatus.last.time < 30) return; // if this is a progress update, skip it if too soon
        Module.setStatus.last.time = now;
        Module.setStatus.last.text = text;
        if (m) {
            text = m[1];
            progressElement.value = parseInt(m[2]) * 100;
            progressElement.max = parseInt(m[4]) * 100;
            progressElement.hidden = false;
            spinnerElement.hidden = false;
        } else {
            progressElement.value = null;
            progressElement.max = null;
            progressElement.hidden = true;
            if (!text) spinnerElement.hidden = true;
        }
        statusElement.innerHTML = text;
        logOutput(text);
    },
    totalDependencies: 0,
    monitorRunDependencies: function (left) {
        this.totalDependencies = Math.max(this.totalDependencies, left);
        Module.setStatus(left ? 'Preparing... (' + (this.totalDependencies - left) + '/' + this.totalDependencies + ')' : 'All downloads complete.');
    }
};

Module.setStatus('Downloading file...');
logOutput('Downloading file...');

window.onerror = function () {
    // Module.setStatus('Exception thrown, see JavaScript console');
    spinnerElement.style.display = 'none';
    Module.setStatus = function (text) {
        if (text) Module.printErr('[post-exception status] ' + text);
    };
};

function loadManifestLink() {
    addRunDependency('load-manifest-link');
    console.log("Loading URL:", manifest_link);
    fetch(manifest_link)
        .then(function (response) {
            var disposition = response.headers.get('Content-Disposition');
            if (disposition) {
                var filename = parseDispositionFilename(disposition);
                if (filename) {
                    console.log("Loading filename:", filename);
                    if (filename.toLowerCase().endsWith('.bas')) {
                        console.log("Loading from BAS.");
                        loadBas(manifest_link, filename);
                    } else if (filename.toLowerCase().endsWith('.prg')) {
                        console.log("Loading from PRG.");
                        loadPrg(manifest_link, filename);
                    } else if (filename.toLowerCase().endsWith('.zip')) {
                        console.log("Loading from ZIP.");
                        loadZip(manifest_link);
                    }
                }
            } else {
                if (manifest_link.toLowerCase().endsWith('.bas')) {
                    var filename = manifest_link.replace(/^.*[\\\/]/, '');
                    console.log("Loading from BAS.");
                    loadBas(manifest_link, filename);
                } else if (manifest_link.toLowerCase().endsWith('.prg')) {
                    var filename = manifest_link.replace(/^.*[\\\/]/, '');
                    console.log("Loading from PRG.");
                    loadPrg(manifest_link, filename);
                } else if (manifest_link.toLowerCase().endsWith('.zip')) {
                    console.log("Loading from ZIP.");
                    loadZip(manifest_link);
                } else {
                    console.log("Loading from directory.");
                    if (!manifest_link.endsWith('/')) {
                        manifest_link = manifest_link + '/';
                    }
                    loadManifest();
                }
            }
        })
        .then(function () {
            removeRunDependency('load-manifest-link');
        });
}

function parseDispositionFilename(disposition) {
    const utf8FilenameRegex = /filename\*=UTF-8''([\w%\-\.]+)(?:; ?|$)/i;
    const asciiFilenameRegex = /^filename=(["']?)(.*?[^\\])\1(?:; ?|$)/i;

    let fileName = null;
    if (utf8FilenameRegex.test(disposition)) {
        fileName = decodeURIComponent(utf8FilenameRegex.exec(disposition)[1]);
    } else {
        // prevent ReDos attacks by anchoring the ascii regex to string start and
        // slicing off everything before 'filename='
        const filenameStart = disposition.toLowerCase().indexOf('filename=');
        if (filenameStart >= 0) {
            const partialDisposition = disposition.slice(filenameStart);
            const matches = asciiFilenameRegex.exec(partialDisposition);
            if (matches != null && matches[2]) {
                fileName = matches[2];
            }
        }
    }
    return fileName;
}

function loadBas(basFileUrl, filename) {
    console.log('Adding start BAS:', filename)
    emuArguments.push('-bas', filename, '-run');
    FS.createPreloadedFile('/', filename, basFileUrl, true, true);
    console.log("Starting Emulator...")
    console.log("Emulator arguments: ", emuArguments)
}

function loadPrg(prgFileUrl, filename) {
    console.log('Adding start PRG:', filename)
    emuArguments.push('-prg', filename, '-run');
    FS.createPreloadedFile('/', filename, prgFileUrl, true, true);
    console.log("Starting Emulator...")
    console.log("Emulator arguments: ", emuArguments)
}

function loadZip(zipFileUrl) {
    addRunDependency('load-zip');
    fetch(zipFileUrl)
        .then(function (response) {
            if (response.status === 200 || response.status === 0) {
                return Promise.resolve(response.blob());
            } else {
                return Promise.reject(new Error(response.statusText));
                // todo error handling here, display to user
            }
        })
        .then(JSZip.loadAsync)
        .then(extractManifestFromBuffer)
        .then(function () {
            console.log("Starting Emulator...")
            console.log("Emulator arguments: ", emuArguments)
            removeRunDependency('load-zip');
        });
}

function extractManifestFromBuffer(zip) {
    if (zip.file("manifest.json") == null) {
        console.log("Unable to find manifest.json file. Writing all files from zip.");
        const promises = [];
        writeAllFilesFromZip(zip, promises, []);
        return Promise.all(promises).then((value) => {
            console.log("Emulator filesystem loading complete.")
        });
    }
    else {
        return zip.file("manifest.json").async("uint8array")
            .then(function (content) {
                let manifestString = new TextDecoder("utf-8").decode(content);
                let manifestObject = JSON.parse(manifestString);
                console.log("Parsed manifest from zip:")
                console.log(manifestObject);

                const promises = [];
                if (manifestObject.resources) {
                    console.log('Found resources section in manifest.');
                    var startFiles = [];
                    manifestObject.resources.forEach(function (element) {
                        let fileName = element.replace(/^.*[\\\/]/, '');
                        if (fileName.toLowerCase().endsWith(".bas") || fileName.toLowerCase().endsWith(".prg")) {
                            startFiles.push(fileName);
                        }
                        if (zip.file(fileName) == null) {
                            logError("Unable to find resources entry: " + fileName);
                            logError("This is likely an error, check resources section in manifest.")
                        } else {
                            promises.push(zip.file(fileName).async("uint8array").then(function (content) {
                                console.log('Writing to emulator filesystem:', fileName);
                                try {
                                    FS.writeFile(fileName, content);
                                }
                                catch(e) {
                                    console.log('Error writing to emulator filesystem:', file.name);
                                }
                            }));
                        }
                        addStartFile(manifestObject, startFiles);
                    });
                } else {
                    console.log('Resources section not found in manifest. Writing all files from zip.');
                    writeAllFilesFromZip(zip, promises, manifestObject);
                }
                return Promise.all(promises);
            })
            .then((value) => {
                console.log("Emulator filesystem loading complete.")
            });
    }
}

function writeAllFilesFromZip(zip, promises, manifestObject) {
    var startFiles = [];
    const writeResources = (zip) => {
        zip.forEach((path, file) => {
            if(file.dir) {
                FS.mkdirTree(file.name);
                writeResources(zip.folder(path));
                return;
            } else {
                if (file.name.toLowerCase().endsWith(".bas") || file.name.toLowerCase().endsWith(".prg")) {
                    startFiles.push(file.name);
                }
            }

            promises.push(zip.file(path).async("uint8array")
                .then(function (content) {
                    console.log('Writing to emulator filesystem:', file.name);
                    try {
                        FS.writeFile(file.name, content);
                    }
                    catch(e) {
                        console.log('Error writing to emulator filesystem:', file.name);
                    }
                })
            );
        });
    };
    writeResources(zip);
    addStartFile(manifestObject, startFiles);
}

function loadManifest() {
    addRunDependency('load-manifest');
    fetch(manifest_link + 'manifest.json').then(function (response) {
        return response.json();
    }).then(function (manifest) {
        console.log("Loading from manifest:")
        console.log(manifest);
        var startFiles = [];
        manifest.resources.forEach(element => {
            element = manifest_link + element;
            let filename = element.replace(/^.*[\\\/]/, '')
            if (filename.toLowerCase().endsWith(".bas") || filename.toLowerCase().endsWith(".prg")) {
                startFiles.push(filename);
            }
            FS.createPreloadedFile('/', filename, element, true, true);
        });
        addStartFile(manifest, startFiles);
        console.log("Starting Emulator...")
        console.log("Emulator arguments: ", emuArguments)
        removeRunDependency('load-manifest');
    }).catch(function () {
        console.log("Unable to read manifest. Check the manifest http parameter");
    });
}

function addStartFile(manifestObject, startFiles) {
    if (manifestObject && manifestObject.start_bas && manifestObject.start_prg) {
        logError("start_bas and start_prg used in manifest");
        logError("This is likely an error, defaulting to start_bas")
    }

    if (manifestObject && manifestObject.start_bas) {
        console.log('Adding start BAS:', manifestObject.start_bas)
        emuArguments.push('-bas', manifestObject.start_bas, '-run');
    } else if (manifestObject && manifestObject.start_prg) {
        console.log('Adding start PRG:', manifestObject.start_prg)
        emuArguments.push('-prg', manifestObject.start_prg, '-run');
    } else if (startFiles) {
        if (startFiles.length === 1) {
            // If there is a single BAS or PRG file, execute it.
            var filename = startFiles[0];
            if (filename.toLowerCase().endsWith(".bas")) {
                console.log('Adding start BAS:', filename)
                emuArguments.push('-bas', filename, '-run');
            } else if (filename.toLowerCase().endsWith(".prg")) {
                console.log('Adding start PRG:', filename)
                emuArguments.push('-prg', filename, '-run');
            }
        } else {
            logOutput("Start files: " + startFiles);
            // Let the user decide which file to LOAD.
        }
    }
}

function toggleAudio() {
    if (audioContext && audioContext.state != "running") {
        audioContext.resume().then(() => {
            volumeElement.innerHTML = "volume_up";
            volumeElementFullScreen.innerHTML = "volume_up";
            console.log("Resumed Audio.")
            Module.ccall("j2c_start_audio", "void", ["bool"], [true]);
        });
    } else if (audioContext && audioContext.state == "running") {
        audioContext.suspend().then(function () {
            console.log("Stopped Audio.")
            volumeElement.innerHTML = "volume_off";
            volumeElementFullScreen.innerHTML = "volume_off";
            Module.ccall("j2c_start_audio", "void", ["bool"], [false]);
        });
    }
    canvas.focus();
}

function resetEmulator() {
    j2c_reset = Module.cwrap("j2c_reset", "void", []);
    j2c_reset();
    canvas.focus();
}

function runCode() {
    Module.ccall("j2c_paste", "void", ["string"], ['\nNEW\n' + code.value + '\nRUN\n']);
    canvas.focus();
}

function closeFs() {
    canvas.parentElement.classList.remove("fullscreen");
    canvas.focus();
}

function openFs() {
    canvas.parentElement.classList.add("fullscreen");
    canvas.focus();
}

function logOutput(text) {
    if (output) {
        output.innerHTML += text + "\n";
        output.parentElement.scrollTop = output.parentElement.scrollHeight; // focus on bottom
    }
    console.log(text);
}

function logError(text) {
    if (output) {
        output.innerHTML += "[error] " + text + "\n";
        output.parentElement.scrollTop = output.parentElement.scrollHeight; // focus on bottom
    }
    console.error(text);
}



function getFirstBrowserLanguage() {
    const nav = window.navigator,
        browserLanguagePropertyKeys = ['language', 'browserLanguage', 'systemLanguage', 'userLanguage'];
    let i,
        language;

    // support for HTML 5.1 "navigator.languages"
    if (Array.isArray(nav.languages)) {
        for (i = 0; i < nav.languages.length; i++) {
            language = nav.languages[i];
            if (language && language.length) {
                return language;
            }
        }
    }

    // support for other well known properties in browsers
    for (i = 0; i < browserLanguagePropertyKeys.length; i++) {
        language = nav[browserLanguagePropertyKeys[i]];
        if (language && language.length) {
            return language;
        }
    }

    return null;
}
