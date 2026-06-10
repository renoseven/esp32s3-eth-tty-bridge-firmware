// SPDX-License-Identifier: MIT
// Copyright (c) 2026 RenoSeven
//
// Single-page configuration UI for the SerialBridge firmware.
//
// Talks to the REST API defined in restful.h:
// * GET  /status (polled),
// * GET  /config/*/fields (metadata + values),
// * POST /config/*/save
// * POST /config/*/reset
// * POST /reboot
// * POST /factory-reset
//
// The wire keys, routes, and error codes here mirror restful.h and the
// I18N object below; keep both in sync.
//
// Cross-cutting behaviors: i18n (en_US/zh_CN) applied to [data-i18n] nodes;
// status polling that pauses during writes; and per-field "dirty" tracking so a
// background refresh never overwrites what the user is editing.
//
// Embedded into the firmware via scripts/embed_assets.py -> src/assets.cpp;
// re-run that script after editing this file.

// ============================================================
//                         Constants
// ============================================================

const ROUTE_STATUS = '/status';
const ROUTE_CONFIG_DEVICE_FIELDS = '/config/device/fields';
const ROUTE_CONFIG_DEVICE_SAVE = '/config/device/save';
const ROUTE_CONFIG_DEVICE_RESET = '/config/device/reset';
const ROUTE_CONFIG_WIFI_FIELDS = '/config/wifi/fields';
const ROUTE_CONFIG_WIFI_SAVE = '/config/wifi/save';
const ROUTE_CONFIG_WIFI_RESET = '/config/wifi/reset';
const ROUTE_CONFIG_SSH_FIELDS = '/config/ssh/fields';
const ROUTE_CONFIG_SSH_SAVE = '/config/ssh/save';
const ROUTE_CONFIG_SSH_RESET = '/config/ssh/reset';
const ROUTE_FACTORY_RESET = '/factory-reset';
const ROUTE_REBOOT = '/reboot';

const DISCONNECT_POST_ROUTES = [
    ROUTE_REBOOT,
    ROUTE_FACTORY_RESET,
    ROUTE_CONFIG_DEVICE_SAVE,
    ROUTE_CONFIG_DEVICE_RESET,
    ROUTE_CONFIG_WIFI_SAVE,
    ROUTE_CONFIG_WIFI_RESET
];

const STATUS_REFRESH_MS = 5000;
const REQUEST_TIMEOUT_MS = 45000;

const HTTP_STATUS_NETWORK_ERROR = 0;
const HTTP_STATUS_NO_CONTENT = 204;

const LOCALES = ['en_US', 'zh_CN'];
const LANGUAGE_STORAGE_KEY = 'language';

const MAIN_TABS = ['status', 'network', 'ssh', 'system'];
const MAIN_TAB_KEY = 'mainTab';
const MAIN_TAB_PANELS = {
    status: 'panel-status',
    network: 'panel-network',
    ssh: 'panel-ssh',
    system: 'panel-system'
};

const DEVICE_FORM_FIELDS = ['name'];
const WIFI_FORM_FIELDS = ['prefMode', 'staSsid', 'staPassword', 'apSsid', 'apPassword'];
const SSH_FORM_FIELDS = ['username', 'password', 'authorizedKey', 'allowNoAuth'];
const STATUS_VALUE_IDS = [
    'deviceName',
    'deviceFirmwareVersion',
    'deviceMemory',
    'deviceUptime',
    'wifiMode',
    'wifiRssi',
    'wifiMacAddr',
    'wifiIpv4Addr',
    'wifiIpv6Addr',
    'sshPublicKey',
    'sshConnected'
];

const WIFI_MODE = { OFF: 0, STA: 1, AP: 2 };

const I18N = {
    en_US: {
        ui: {
            appLanguageToggle: '中',
            appSubtitle: 'Serial over SSH',
            tabNetwork: 'Network',
            tabSsh: 'SSH',
            tabStatus: 'Status',
            tabSystem: 'System',
            statusDeviceFirmwareVersion: 'Firmware Version',
            statusDeviceName: 'Name',
            statusDeviceMemory: 'Memory',
            statusDeviceTitle: 'Device',
            statusDeviceUptime: 'Uptime',
            statusSshConnected: 'SSH Client',
            statusSshPublicKey: 'SSH Host Key',
            statusSshTitle: 'SSH',
            statusWifiIpv4Address: 'IPv4 Address',
            statusWifiIpv6Address: 'IPv6 Address',
            statusWifiMacAddress: 'MAC Address',
            statusWifiMode: 'Mode',
            statusWifiModeAccessPoint: 'Access Point',
            statusWifiModeOff: 'Off',
            statusWifiModeStation: 'Client',
            statusWifiSignalStrength: 'RSSI',
            statusWifiTitle: 'Wi-Fi',
            networkDeviceName: 'Name',
            networkDeviceReset: 'Reset',
            networkDeviceSave: 'Save',
            networkDeviceTitle: 'Device',
            networkWifiPrefMode: 'Mode',
            networkWifiModeHintClient: 'Connect this device to an existing Wi-Fi network.',
            networkWifiModeHintAp: 'Create a Wi-Fi hotspot for other devices to connect.',
            networkWifiStaSsid: 'SSID',
            networkWifiStaPassword: 'Password',
            networkWifiApSsid: 'SSID',
            networkWifiApPassword: 'Password',
            networkWifiReset: 'Reset',
            networkWifiSave: 'Save',
            networkWifiTitle: 'Wi-Fi',
            sshAllowNoAuth: 'Allow unauthenticated SSH',
            sshAuthorizedKey: 'Authorized Key (optional)',
            sshPassword: 'Password',
            sshReset: 'Reset',
            sshSave: 'Save',
            sshTitle: 'Authentication',
            sshUsername: 'Username',
            systemFactoryReset: 'Factory Config Reset',
            systemFactoryResetAction: 'Reset',
            systemFactoryResetDescription: 'Erase all saved configuration and restart with defaults.',
            systemReboot: 'Reboot Device',
            systemRebootAction: 'Reboot',
            systemRebootDescription: 'Restart the device without changing any saved settings.',
            systemTitle: 'System',
            dialogCancel: 'Cancel',
            dialogOk: 'OK'
        },
        msg: {
            appRequestFailed: 'Request failed.',
            statusConnected: 'Connected',
            statusDisconnected: 'Disconnected',
            statusRefreshFailed: 'Failed to load status.',
            deviceConfigLoadFailed: 'Failed to load device config.',
            deviceConfirmReset: 'Reset device name to default?',
            deviceResetOk: 'Device config reset to defaults.',
            deviceSaveOk: 'Device config saved.',
            wifiConfigLoadFailed: 'Failed to load Wi-Fi config.',
            wifiConfirmReset: 'Reset Wi-Fi config to defaults?',
            wifiResetOk: 'Wi-Fi config reset to defaults.',
            wifiSaveOk: 'Wi-Fi config saved.',
            wifiSavedAp: 'Wi-Fi config saved. Connecting to Wi-Fi, please rejoin to access the device.',
            sshConfigLoadFailed: 'Failed to load SSH config.',
            sshConfirmReset: 'Reset SSH config to defaults?',
            sshPublicKeyPending: 'Generating on first SSH startup...',
            sshResetOk: 'SSH config reset to defaults.',
            sshSaveOk: 'SSH config saved.',
            systemConfirmReboot: 'Reboot the device?',
            systemRebooting: 'Rebooting...',
            configNotReady: 'Config not loaded yet. Please wait and try again.',
            factoryConfirmReset: 'Factory config reset and reboot? All saved config will be erased.',
            factoryResetReboot: 'Factory config reset started. Rebooting...'
        },
        err: {
            statusOverflow: 'Status response overflow.',
            configDeviceBadName:
                'Invalid device name (1-32 chars: letters, digits, hyphens; cannot start/end with hyphen).',
            configDeviceOverflow: 'Device config response overflow.',
            configSshBadAllowNoAuth: 'Invalid no-auth option.',
            configSshBadAuthorizedKey: 'Invalid public key (OpenSSH key type, RSA-8192 max, 1536 chars).',
            configSshBadPassword: 'SSH password must be at most 64 characters.',
            configSshBadUsername: 'SSH username is required (max 32 characters).',
            configSshOverflow: 'SSH config response overflow.',
            configWifiBadPrefMode: 'Invalid Wi-Fi mode.',
            configWifiBadSsid: 'WiFi SSID is required (1-32 characters).',
            configWifiOverflow: 'Wi-Fi config response overflow.',
            configWifiPasswordTooLong: 'WiFi password must be at most 63 characters.',
            configWifiPasswordTooShort: 'WiFi password must be 8-63 characters, or leave blank for an open network.',
            jsonBadFieldType: 'Invalid field type.',
            jsonEmptyBody: 'Request body is required.',
            jsonInvalid: 'Invalid JSON.',
            jsonNotObject: 'JSON root must be an object.',
            jsonPayloadTooLarge: 'Request body too large.',
            jsonTooSmall: 'JSON document too small.'
        }
    },
    zh_CN: {
        ui: {
            appLanguageToggle: 'EN',
            appSubtitle: 'SSH 远程串口',
            tabNetwork: '网络配置',
            tabSsh: 'SSH',
            tabStatus: '运行状态',
            tabSystem: '系统管理',
            statusDeviceFirmwareVersion: '固件版本',
            statusDeviceName: '名称',
            statusDeviceMemory: '内存',
            statusDeviceTitle: '设备',
            statusDeviceUptime: '运行时长',
            statusSshConnected: 'SSH 连接',
            statusSshPublicKey: 'SSH 主机公钥',
            statusSshTitle: 'SSH',
            statusWifiIpv4Address: 'IPv4 地址',
            statusWifiIpv6Address: 'IPv6 地址',
            statusWifiMacAddress: 'MAC 地址',
            statusWifiMode: '模式',
            statusWifiModeAccessPoint: '接入点 (AP)',
            statusWifiModeOff: '关闭',
            statusWifiModeStation: '客户端',
            statusWifiSignalStrength: '信号',
            statusWifiTitle: '无线网络',
            networkDeviceName: '名称',
            networkDeviceReset: '重置',
            networkDeviceSave: '保存',
            networkDeviceTitle: '设备配置',
            networkWifiPrefMode: '模式',
            networkWifiModeHintClient: '将设备连接到现有的无线网络。',
            networkWifiModeHintAp: '创建无线网络热点，供其他设备连接。',
            networkWifiStaSsid: '网络名称 (SSID)',
            networkWifiStaPassword: '密码',
            networkWifiApSsid: '热点名称 (SSID)',
            networkWifiApPassword: '密码',
            networkWifiReset: '重置',
            networkWifiSave: '保存',
            networkWifiTitle: '无线网络配置',
            sshAllowNoAuth: '允许免认证连接',
            sshAuthorizedKey: '授权公钥 (可选)',
            sshPassword: '密码',
            sshReset: '重置',
            sshSave: '保存',
            sshTitle: '认证配置',
            sshUsername: '用户名',
            systemFactoryReset: '恢复出厂配置',
            systemFactoryResetAction: '恢复',
            systemFactoryResetDescription: '清除所有已保存的配置，并以默认设置重启。',
            systemReboot: '重启设备',
            systemRebootAction: '重启',
            systemRebootDescription: '重启设备，不会更改任何已保存的配置。',
            systemTitle: '系统管理',
            dialogCancel: '取消',
            dialogOk: '确定'
        },
        msg: {
            appRequestFailed: '请求失败。',
            statusConnected: '已连接',
            statusDisconnected: '未连接',
            statusRefreshFailed: '无法加载状态。',
            deviceConfigLoadFailed: '无法加载设备配置。',
            deviceConfirmReset: '重置设备名为默认值？',
            deviceResetOk: '设备配置已恢复为默认值。',
            deviceSaveOk: '设备配置已保存。',
            wifiConfigLoadFailed: '无法加载网络配置。',
            wifiConfirmReset: '重置无线网络配置为默认值？',
            wifiResetOk: '网络配置已恢复为默认值。',
            wifiSaveOk: '网络配置已保存。',
            wifiSavedAp: '网络配置已保存。设备正在连接无线网络，请连上同一路由器后重新访问。',
            sshConfigLoadFailed: '无法加载 SSH 配置。',
            sshConfirmReset: '重置 SSH 配置为默认值？',
            sshPublicKeyPending: '首次启动 SSH 服务后生成',
            sshResetOk: 'SSH 配置已恢复为默认值。',
            sshSaveOk: 'SSH 配置已保存。',
            systemConfirmReboot: '确定要重启设备吗？',
            systemRebooting: '正在重启...',
            configNotReady: '配置尚未加载，请稍后再试。',
            factoryConfirmReset: '恢复出厂配置并重启？所有已保存的配置将被清除。',
            factoryResetReboot: '正在恢复出厂配置并重启...'
        },
        err: {
            statusOverflow: '状态响应溢出。',
            configDeviceBadName: '设备名无效 (1-32 个字符，仅限字母、数字、连字符，且不能以连字符开头或结尾)。',
            configDeviceOverflow: '设备配置响应溢出。',
            configSshBadAllowNoAuth: '免认证选项无效。',
            configSshBadAuthorizedKey: '公钥无效 (需合法 OpenSSH 密钥类型，支持 RSA-8192，最多 1536 字符)。',
            configSshBadPassword: 'SSH 密码最多 64 个字符。',
            configSshBadUsername: 'SSH 用户名不能为空 (最多 32 个字符)。',
            configSshOverflow: 'SSH 配置响应溢出。',
            configWifiBadPrefMode: '无效的 Wi-Fi 模式。',
            configWifiBadSsid: '无线网络名称不能为空 (最多 32 个字符)。',
            configWifiOverflow: '无线网络配置响应溢出。',
            configWifiPasswordTooLong: '无线网络密码最多 63 个字符。',
            configWifiPasswordTooShort: '密码须为 8-63 个字符，或留空使用开放网络。',
            jsonBadFieldType: '字段类型无效。',
            jsonEmptyBody: '缺少请求体。',
            jsonInvalid: 'JSON 格式无效。',
            jsonNotObject: 'JSON 根节点必须是 object。',
            jsonPayloadTooLarge: '请求体过大。',
            jsonTooSmall: 'JSON 文档过小。'
        }
    }
};

// ============================================================
//                       DOM References
// ============================================================

const deviceForm = document.getElementById('form-device');
const wifiForm = document.getElementById('form-wifi');
const sshForm = document.getElementById('form-ssh');
const sshSaveBtn = sshForm?.querySelector('button[type="submit"]');

function deviceField(name) {
    return deviceForm?.elements.namedItem(name) ?? null;
}

function wifiField(name) {
    return wifiForm?.elements.namedItem(name) ?? null;
}

function sshField(name) {
    return sshForm?.elements.namedItem(name) ?? null;
}

const panelStatus = document.getElementById('panel-status');
const statusRows = panelStatus?.querySelector('[data-role="status-rows"]');
const statusError = panelStatus?.querySelector('[data-role="status-error"]');
const statusErrorMsg = panelStatus?.querySelector('[data-status="error"]');

const statusEl = {};
for (const id of STATUS_VALUE_IDS) {
    statusEl[id] = document.querySelector(`[data-status="${id}"]`);
}

const uiModal = document.getElementById('modal-ui');
const uiModalMsg = uiModal?.querySelector('[data-role="modal-msg"]');
const uiModalOk = uiModal?.querySelector('[data-action="ok"]');
const uiModalCancel = uiModal?.querySelector('[data-action="cancel"]');

const tabBar = document.querySelector('.tab-bar');
const tabIndicator = tabBar?.querySelector('.tab-indicator');

const mainTabBtn = {};
const mainTabPanel = {};
for (const name of MAIN_TABS) {
    mainTabBtn[name] = document.querySelector(`[data-tab="${name}"]`);
    mainTabPanel[name] = document.getElementById(MAIN_TAB_PANELS[name]);
}

// ============================================================
//                           State
// ============================================================

let language = LOCALES[0];
let lastStatus = null;
let uiModalResolver = null;
let wifiMode = null;
let refreshPaused = false;
let activeMainTab = '';
let statusRefreshTimer = null;
let sshConfigLoaded = false;

const deviceFields = {};
const wifiFields = {};
const sshFields = {};

const dirtyDeviceFields = new Set();
const dirtyWifiFields = new Set();
const dirtySshFields = new Set();

// ============================================================
//                           Util
// ============================================================

function fetchWithTimeout(url, opts, ms) {
    if (typeof AbortController !== 'undefined') {
        const ctrl = new AbortController();
        const timer = setTimeout(function () {
            ctrl.abort();
        }, ms);

        return fetch(url, { ...(opts || {}), signal: ctrl.signal }).finally(function () {
            clearTimeout(timer);
        });
    }

    return Promise.race([
        fetch(url, opts || {}),
        new Promise(function (_, reject) {
            setTimeout(function () {
                reject(new Error('timeout'));
            }, ms);
        })
    ]);
}

function bindTap(el, fn) {
    if (!el) {
        return;
    }

    el.addEventListener('click', function (e) {
        e.preventDefault();
        e.stopPropagation();
        fn();
    });
}

// ============================================================
//                           I18n
// ============================================================

function storedLanguage() {
    try {
        const saved = localStorage.getItem(LANGUAGE_STORAGE_KEY);

        if (saved && I18N[saved]) {
            return saved;
        }
    } catch {
        // ignore
    }

    return LOCALES[0];
}

function translate(section, key) {
    const bag = I18N[language][section];

    return bag && bag[key] ? bag[key] : '';
}

function errMsg(code) {
    return translate('err', code) || translate('msg', 'appRequestFailed');
}

function wifiModeLabel(mode) {
    switch (mode) {
        case WIFI_MODE.OFF:
            return translate('ui', 'statusWifiModeOff');
        case WIFI_MODE.STA:
            return translate('ui', 'statusWifiModeStation');
        case WIFI_MODE.AP:
            return translate('ui', 'statusWifiModeAccessPoint');
        default:
            return '';
    }
}

function applyI18n() {
    const dict = I18N[language].ui;

    document.querySelectorAll('[data-i18n]').forEach((el) => {
        const key = el.dataset.i18n;

        if (dict[key] !== undefined) {
            el.textContent = dict[key];
        }
    });
}

function setLanguage(next) {
    if (!I18N[next]) {
        next = LOCALES[0];
    }

    language = next;
    try {
        localStorage.setItem(LANGUAGE_STORAGE_KEY, language);
    } catch {
        // ignore
    }

    document.documentElement.lang = language.replace('_', '-');
    applyI18n();

    if (lastStatus) {
        renderStatusPanel(lastStatus);
    }

    requestAnimationFrame(function () {
        updateTabIndicator(activeMainTab);
    });
}

function toggleLanguage() {
    const i = LOCALES.indexOf(language);
    const next = LOCALES[(i + 1) % LOCALES.length];

    setLanguage(next);
}

// ============================================================
//                           Modal
// ============================================================

function closeUiModal(result) {
    if (uiModal) {
        uiModal.hidden = true;
    }

    document.body.classList.remove('modal-open');
    if (uiModalResolver) {
        const fn = uiModalResolver;
        uiModalResolver = null;
        fn(result);
    }
}

function openUiModal(msg, confirmMode) {
    return new Promise(function (resolve) {
        if (!uiModal || !uiModalMsg || !uiModalOk) {
            resolve(confirmMode ? false : true);
            return;
        }

        uiModalMsg.textContent = msg;
        uiModalResolver = resolve;
        uiModalOk.textContent = translate('ui', 'dialogOk');
        uiModalOk.classList.toggle('button-single', !confirmMode);
        if (uiModalCancel) {
            uiModalCancel.textContent = translate('ui', 'dialogCancel');
            uiModalCancel.hidden = !confirmMode;
        }

        uiModal.hidden = false;
        document.body.classList.add('modal-open');
    });
}

function uiAlert(msg) {
    return openUiModal(msg, false);
}

function uiConfirm(msg) {
    return openUiModal(msg, true);
}

// ============================================================
//                           HTTP
// ============================================================

function problemErr(body) {
    return body && body.type ? body.type : null;
}

async function postJson(url, body, expectDisconnect) {
    const opts = { method: 'POST', headers: {} };

    if (body !== undefined) {
        opts.headers['Content-Type'] = 'application/json';
        opts.body = JSON.stringify(body);
    }

    if (expectDisconnect === undefined) {
        expectDisconnect = DISCONNECT_POST_ROUTES.includes(url);
    }

    refreshPaused = true;
    try {
        try {
            const res = await fetchWithTimeout(url, opts, REQUEST_TIMEOUT_MS);

            if (expectDisconnect && res.status === HTTP_STATUS_NETWORK_ERROR) {
                return true;
            }
            let j = null;

            if (res.status !== HTTP_STATUS_NO_CONTENT) {
                try {
                    j = await res.json();
                } catch {
                    // ignore
                }
            }

            if (!res.ok) {
                const errType = problemErr(j);
                const msg = errType ? errMsg(errType) : translate('msg', 'appRequestFailed');

                await uiAlert(msg);
                return false;
            }

            return true;
        } catch {
            if (expectDisconnect) {
                return true;
            }

            await uiAlert(translate('msg', 'appRequestFailed'));
            return false;
        }
    } finally {
        refreshPaused = false;
    }
}

async function fetchGetJson(url) {
    const res = await fetchWithTimeout(url, {}, 15000);
    let data;

    try {
        data = await res.json();
    } catch {
        return { ok: false };
    }

    if (!res.ok) {
        return { ok: false, err: problemErr(data) };
    }

    if (!data) {
        return { ok: false };
    }
    return { ok: true, data };
}

function getLoadErrorMessage(result, fallbackKey) {
    if (result.err) {
        return errMsg(result.err);
    }
    return translate('msg', fallbackKey);
}

// ============================================================
//                      Config Field Meta
// ============================================================

function fieldSpec(store, name) {
    return store[name] || null;
}

function applyFieldDomLimits(el, spec) {
    if (!el || !spec) {
        return;
    }
    if (spec.min !== undefined && el.type !== 'checkbox') {
        el.minLength = spec.min;
    } else {
        el.removeAttribute('minlength');
    }

    if (spec.max !== undefined) {
        el.maxLength = spec.max;
    } else {
        el.removeAttribute('maxlength');
    }

    const htmlRequired =
        spec.required === true && Object.prototype.hasOwnProperty.call(spec, 'value') && el.type !== 'checkbox';

    if (htmlRequired) {
        el.required = true;
    } else if (el.type !== 'checkbox') {
        el.removeAttribute('required');
    }
}

function applyConfigGet(store, cfg, fieldNames, getFieldEl, syncField) {
    for (const key of Object.keys(store)) {
        delete store[key];
    }

    for (const name of fieldNames) {
        const spec = cfg[name];

        if (!spec) {
            continue;
        }

        store[name] = spec;
        applyFieldDomLimits(getFieldEl(name), spec);
        if (Object.prototype.hasOwnProperty.call(spec, 'value')) {
            syncField(name, spec.value);
        }
    }
}

function configReady(store, fieldNames) {
    for (const name of fieldNames) {
        if (!fieldSpec(store, name)) {
            return false;
        }
    }
    return true;
}

// ============================================================
//                        Validation
// ============================================================

function validateDeviceName(v, spec) {
    if (!v) {
        return !spec || spec.required !== true;
    }
    if (!spec) {
        return true;
    }

    if (spec.min !== undefined && v.length < spec.min) {
        return false;
    }
    if (spec.max !== undefined && v.length > spec.max) {
        return false;
    }

    const mid = spec.max - 2;

    return new RegExp(`^[A-Za-z0-9]([A-Za-z0-9-]{0,${mid}}[A-Za-z0-9])?$`).test(v);
}

function validateStrLen(v, max) {
    return v.length <= max;
}

function isOpenSshKeyFormat(s) {
    return /^(ssh-\S+|ecdsa-\S+|sk-\S+)\s+[A-Za-z0-9+/]+=*(\s+\S.*)?$/.test(s);
}

function validateAuthorizedKeyLen(v, spec) {
    const key = (v || '').trim();

    if (!key) {
        return true;
    }
    return !(spec && spec.max !== undefined && key.length > spec.max);
}

function validateWifiPassword(value, spec) {
    const pass = (value || '').trim();

    if (!pass) {
        return null;
    }

    if (spec && spec.min !== undefined && pass.length < spec.min) {
        return errMsg('configWifiPasswordTooShort');
    }
    if (spec && spec.max !== undefined && pass.length > spec.max) {
        return errMsg('configWifiPasswordTooLong');
    }
    return null;
}

function validateDeviceFields() {
    const nameEl = deviceField('name');
    const spec = fieldSpec(deviceFields, 'name');

    if (!configReady(deviceFields, DEVICE_FORM_FIELDS) || !nameEl) {
        return translate('msg', 'configNotReady');
    }

    if (!validateDeviceName(nameEl.value.trim(), spec)) {
        return errMsg('configDeviceBadName');
    }
    return null;
}

function validateWifiSsidField(el, spec) {
    if (!el || !spec) {
        return null;
    }
    const ssid = el.value.trim();
    if (spec.min !== undefined && ssid.length < spec.min) {
        return errMsg('configWifiBadSsid');
    }
    if (spec.max !== undefined && ssid.length > spec.max) {
        return errMsg('configWifiBadSsid');
    }
    return null;
}

function validateWifiFields() {
    const prefModeEl = wifiField('prefMode');
    const staSsidEl = wifiField('staSsid');
    const staPassEl = wifiField('staPassword');
    const apSsidEl = wifiField('apSsid');
    const apPassEl = wifiField('apPassword');
    const staSsidSpec = fieldSpec(wifiFields, 'staSsid');
    const staPassSpec = fieldSpec(wifiFields, 'staPassword');
    const apSsidSpec = fieldSpec(wifiFields, 'apSsid');
    const apPassSpec = fieldSpec(wifiFields, 'apPassword');

    if (!prefModeEl || !staSsidEl || !staPassEl || !apSsidEl || !apPassEl) {
        return translate('msg', 'configNotReady');
    }

    const mode = parseInt(prefModeEl.value, 10);
    let err;

    if (mode === WIFI_MODE.STA) {
        const ssid = staSsidEl.value.trim();
        if (!ssid) {
            return errMsg('configWifiBadSsid');
        }
        err = validateWifiSsidField(staSsidEl, staSsidSpec);
        if (err) {
            return err;
        }
        if (dirtyWifiFields.has('staPassword')) {
            err = validateWifiPassword(staPassEl.value, staPassSpec);
            if (err) {
                return err;
            }
        }
    } else if (mode === WIFI_MODE.AP) {
        err = validateWifiSsidField(apSsidEl, apSsidSpec);
        if (err) {
            return err;
        }
        if (dirtyWifiFields.has('apPassword')) {
            err = validateWifiPassword(apPassEl.value, apPassSpec);
            if (err) {
                return err;
            }
        }
    }

    return null;
}

function validateSshFields() {
    const usernameEl = sshField('username');
    const passEl = sshField('password');
    const authorizedKeyEl = sshField('authorizedKey');
    const allowNoAuthEl = sshField('allowNoAuth');
    const usernameSpec = fieldSpec(sshFields, 'username');
    const passSpec = fieldSpec(sshFields, 'password');
    const authorizedKeySpec = fieldSpec(sshFields, 'authorizedKey');

    if (!configReady(sshFields, SSH_FORM_FIELDS) || !usernameEl || !passEl || !authorizedKeyEl || !allowNoAuthEl) {
        return translate('msg', 'configNotReady');
    }

    const username = usernameEl.value.trim();
    const password = passEl.value.trim();
    const authorizedKey = authorizedKeyEl.value.trim();

    if (username) {
        if (usernameSpec && usernameSpec.min !== undefined && username.length < usernameSpec.min) {
            return errMsg('configSshBadUsername');
        }
        if (usernameSpec && usernameSpec.max !== undefined && username.length > usernameSpec.max) {
            return errMsg('configSshBadUsername');
        }
    } else if (!allowNoAuthEl.checked) {
        return errMsg('configSshBadUsername');
    }

    if (password && passSpec && passSpec.max !== undefined && !validateStrLen(password, passSpec.max)) {
        return errMsg('configSshBadPassword');
    }

    if (!validateAuthorizedKeyLen(authorizedKey, authorizedKeySpec)) {
        return errMsg('configSshBadAuthorizedKey');
    }

    if (authorizedKey && !isOpenSshKeyFormat(authorizedKey)) {
        return errMsg('configSshBadAuthorizedKey');
    }

    return null;
}

// ============================================================
//                       Status Panel
// ============================================================

function showStatusError(msg) {
    if (statusRows) {
        statusRows.hidden = true;
    }

    if (statusError && statusErrorMsg) {
        statusError.hidden = false;
        statusErrorMsg.textContent = msg;
    }
}

function showStatusOk() {
    if (statusError) {
        statusError.hidden = true;
    }

    if (statusRows) {
        statusRows.hidden = false;
    }
}

function setStatusText(el, value) {
    if (el && el.textContent !== value) {
        el.textContent = value;
    }
}

function hasValue(v) {
    return v !== undefined && v !== null && v !== '';
}

function formatBytes(n) {
    if (!hasValue(n) || isNaN(n)) {
        return '';
    }

    const kb = n / 1024;
    if (kb >= 1024) {
        return `${(kb / 1024).toFixed(1)} MB`;
    }
    return `${Math.round(kb)} KB`;
}

function formatMemory(free, total) {
    const hasFree = hasValue(free) && !isNaN(free);
    const hasTotal = hasValue(total) && !isNaN(total);

    if (hasFree && hasTotal) {
        return `${formatBytes(free)} / ${formatBytes(total)}`;
    }
    if (hasFree) {
        return formatBytes(free);
    }
    return '';
}

function formatUptime(sec) {
    if (!hasValue(sec) || isNaN(sec)) {
        return '';
    }
    sec = Math.floor(sec);

    const d = Math.floor(sec / 86400);
    const h = Math.floor((sec % 86400) / 3600);
    const m = Math.floor((sec % 3600) / 60);
    const parts = [];

    if (d) {
        parts.push(`${d}d`);
    }
    if (h) {
        parts.push(`${h}h`);
    }
    if (m) {
        parts.push(`${m}m`);
    }

    if (!parts.length) {
        parts.push(`${sec % 60}s`);
    }

    return parts.join(' ');
}

function renderStatusPanel(s) {
    if (!s) {
        return;
    }
    setStatusText(statusEl.deviceName, s.deviceName || '');
    setStatusText(statusEl.deviceFirmwareVersion, s.deviceFirmwareVersion || '');
    setStatusText(statusEl.deviceMemory, formatMemory(s.deviceMemoryFree, s.deviceMemoryTotal));
    setStatusText(statusEl.deviceUptime, formatUptime(s.deviceUptime));

    setStatusText(statusEl.wifiMode, wifiModeLabel(s.wifiMode));
    setStatusText(
        statusEl.wifiRssi,
        s.wifiRssi !== undefined && s.wifiRssi !== null && s.wifiRssi !== '' ? `${s.wifiRssi} dBm` : ''
    );
    setStatusText(statusEl.wifiMacAddr, s.wifiMacAddr || '');
    setStatusText(statusEl.wifiIpv4Addr, s.wifiIpv4Addr || '');
    setStatusText(statusEl.wifiIpv6Addr, s.wifiIpv6Addr || '');

    setStatusText(statusEl.sshPublicKey, s.sshPublicKey || translate('msg', 'sshPublicKeyPending'));
    setStatusText(
        statusEl.sshConnected,
        s.sshConnected ? translate('msg', 'statusConnected') : translate('msg', 'statusDisconnected')
    );
}

function stopStatusPolling() {
    if (statusRefreshTimer !== null) {
        clearInterval(statusRefreshTimer);
        statusRefreshTimer = null;
    }
}

function startStatusPolling() {
    stopStatusPolling();
    statusRefreshTimer = setInterval(refresh, STATUS_REFRESH_MS);
}

// ============================================================
//                  Form Sync & Visibility
// ============================================================

function syncDeviceField(name, value) {
    const el = deviceField(name);

    if (!el || dirtyDeviceFields.has(name) || document.activeElement === el) {
        return;
    }

    if (el.value !== value) {
        el.value = value;
    }
}

function syncWifiField(name, value) {
    const el = wifiField(name);

    if (!el || dirtyWifiFields.has(name) || document.activeElement === el) {
        return;
    }

    const strVal = String(value);
    if (el.value !== strVal) {
        el.value = strVal;
    }
}

function syncSshField(name, value, checkbox) {
    const el = sshField(name);

    if (!el || dirtySshFields.has(name) || document.activeElement === el) {
        return;
    }

    if (checkbox) {
        if (el.checked !== value) {
            el.checked = value;
        }
    } else if (el.value !== value) {
        el.value = value;
    }
}

function updateWifiFieldVisibility() {
    const prefModeEl = wifiField('prefMode');

    if (!prefModeEl || !wifiForm) {
        return;
    }

    const mode = parseInt(prefModeEl.value, 10);
    const off = mode === WIFI_MODE.OFF;
    const showGroup = mode === WIFI_MODE.STA ? 'sta' : 'ap';

    wifiForm.querySelectorAll('[data-wifi-group]').forEach(function (el) {
        const visible = !off && el.dataset.wifiGroup === showGroup;
        el.style.display = visible ? '' : 'none';

        const input = el.querySelector('input');
        if (input) {
            input.disabled = !visible;
        }
    });

    const hintEl = document.getElementById('wifi-mode-hint');
    if (hintEl) {
        if (off) {
            hintEl.textContent = '';
        } else {
            const hintKey = mode === WIFI_MODE.STA ? 'networkWifiModeHintClient' : 'networkWifiModeHintAp';
            hintEl.textContent = translate('ui', hintKey);
        }
    }
}

function updateSshAuthFields() {
    const usernameEl = sshField('username');
    const passEl = sshField('password');
    const authorizedKeyEl = sshField('authorizedKey');
    const allowNoAuthEl = sshField('allowNoAuth');

    if (!usernameEl || !passEl || !authorizedKeyEl || !allowNoAuthEl || !sshSaveBtn) {
        return;
    }

    const disabled = !sshConfigLoaded;
    usernameEl.disabled = disabled;
    passEl.disabled = disabled;
    authorizedKeyEl.disabled = disabled;
    allowNoAuthEl.disabled = disabled;
    sshSaveBtn.disabled = disabled;
}

function clearDeviceDirty(...names) {
    for (const name of names) {
        dirtyDeviceFields.delete(name);
    }
}

function clearWifiDirty(...names) {
    for (const name of names) {
        dirtyWifiFields.delete(name);
    }
}

function clearSshDirty(...names) {
    for (const name of names) {
        dirtySshFields.delete(name);
    }
}

// ============================================================
//                       Data Fetching
// ============================================================

async function fetchStatusSnapshot() {
    const result = await fetchGetJson(ROUTE_STATUS);

    if (!result.ok) {
        return { ok: false, error: getLoadErrorMessage(result, 'statusRefreshFailed') };
    }

    wifiMode = result.data.wifiMode;
    lastStatus = result.data;
    return { ok: true, data: result.data };
}

async function refresh() {
    if (refreshPaused) {
        return;
    }
    try {
        const result = await fetchStatusSnapshot();

        if (!result.ok) {
            showStatusError(result.error);
            return;
        }

        showStatusOk();
        renderStatusPanel(result.data);
    } catch {
        showStatusError(translate('msg', 'statusRefreshFailed'));
    }
}

async function fetchDeviceConfig() {
    const result = await fetchGetJson(ROUTE_CONFIG_DEVICE_FIELDS);

    if (!result.ok) {
        await uiAlert(getLoadErrorMessage(result, 'deviceConfigLoadFailed'));
        return false;
    }

    applyConfigGet(deviceFields, result.data, DEVICE_FORM_FIELDS, deviceField, syncDeviceField);

    return true;
}

async function fetchWifiConfig() {
    const result = await fetchGetJson(ROUTE_CONFIG_WIFI_FIELDS);

    if (!result.ok) {
        await uiAlert(getLoadErrorMessage(result, 'wifiConfigLoadFailed'));
        return false;
    }

    const cfg = result.data;

    if (cfg.prefMode && Object.prototype.hasOwnProperty.call(cfg.prefMode, 'value')) {
        wifiMode = cfg.prefMode.value;
    }

    applyConfigGet(wifiFields, cfg, WIFI_FORM_FIELDS, wifiField, syncWifiField);
    updateWifiFieldVisibility();

    return true;
}

async function fetchNetworkConfig() {
    const results = await Promise.all([fetchDeviceConfig(), fetchWifiConfig()]);

    return results[0] && results[1];
}

async function fetchSshConfig() {
    const result = await fetchGetJson(ROUTE_CONFIG_SSH_FIELDS);

    if (!result.ok) {
        await uiAlert(getLoadErrorMessage(result, 'sshConfigLoadFailed'));
        return false;
    }

    applyConfigGet(sshFields, result.data, SSH_FORM_FIELDS, sshField, function (name, value) {
        syncSshField(name, value, name === 'allowNoAuth');
    });

    sshConfigLoaded = true;
    updateSshAuthFields();

    return true;
}

// ============================================================
//                         Actions
// ============================================================

async function saveDeviceConfig() {
    const nameEl = deviceField('name');

    if (!nameEl) {
        return;
    }

    const err = validateDeviceFields();

    if (err) {
        await uiAlert(err);
        return;
    }

    if (
        await postJson(ROUTE_CONFIG_DEVICE_SAVE, {
            name: nameEl.value.trim()
        })
    ) {
        clearDeviceDirty('name');
        await uiAlert(translate('msg', 'deviceSaveOk'));
    }
}

async function resetDeviceConfig() {
    if (!(await uiConfirm(translate('msg', 'deviceConfirmReset')))) {
        return;
    }

    if (await postJson(ROUTE_CONFIG_DEVICE_RESET)) {
        await uiAlert(translate('msg', 'deviceResetOk'));
    }
}

function wifiConfigMsg(mode) {
    return translate('msg', mode === WIFI_MODE.AP ? 'wifiSavedAp' : 'wifiSaveOk');
}

async function saveWifiConfig() {
    const prefModeEl = wifiField('prefMode');
    const staSsidEl = wifiField('staSsid');
    const staPassEl = wifiField('staPassword');
    const apSsidEl = wifiField('apSsid');
    const apPassEl = wifiField('apPassword');

    if (!prefModeEl || !staSsidEl || !staPassEl || !apSsidEl || !apPassEl) {
        return;
    }

    const err = validateWifiFields();
    if (err) {
        await uiAlert(err);
        return;
    }

    const modeBeforeSave = wifiMode;

    const mode = parseInt(prefModeEl.value, 10);
    const body = { prefMode: mode };

    if (mode === WIFI_MODE.STA) {
        body.staSsid = staSsidEl.value.trim();
        if (dirtyWifiFields.has('staPassword')) {
            body.staPassword = staPassEl.value.trim();
        }
    } else if (mode === WIFI_MODE.AP) {
        body.apSsid = apSsidEl.value.trim();
        if (dirtyWifiFields.has('apPassword')) {
            body.apPassword = apPassEl.value.trim();
        }
    }

    if (await postJson(ROUTE_CONFIG_WIFI_SAVE, body)) {
        clearWifiDirty('prefMode', 'staSsid', 'staPassword', 'apSsid', 'apPassword');
        await uiAlert(wifiConfigMsg(modeBeforeSave));
    }
}

async function resetWifiConfig() {
    if (!(await uiConfirm(translate('msg', 'wifiConfirmReset')))) {
        return;
    }

    if (await postJson(ROUTE_CONFIG_WIFI_RESET)) {
        await uiAlert(translate('msg', 'wifiResetOk'));
    }
}

async function saveSsh() {
    const usernameEl = sshField('username');
    const passEl = sshField('password');
    const authorizedKeyEl = sshField('authorizedKey');
    const allowNoAuthEl = sshField('allowNoAuth');

    if (!usernameEl || !passEl || !authorizedKeyEl || !allowNoAuthEl) {
        return;
    }

    const err = validateSshFields();

    if (err) {
        await uiAlert(err);
        return;
    }

    const username = usernameEl.value.trim();
    const authorizedKey = authorizedKeyEl.value.trim();
    const password = passEl.value.trim();

    const body = {
        username,
        authorizedKey,
        allowNoAuth: allowNoAuthEl.checked
    };

    if (password) {
        body.password = password;
    }

    if (await postJson(ROUTE_CONFIG_SSH_SAVE, body)) {
        clearSshDirty('username', 'password', 'authorizedKey', 'allowNoAuth');
        await uiAlert(translate('msg', 'sshSaveOk'));
    }
}

async function resetSsh() {
    if (!(await uiConfirm(translate('msg', 'sshConfirmReset')))) {
        return;
    }

    if (await postJson(ROUTE_CONFIG_SSH_RESET)) {
        await uiAlert(translate('msg', 'sshResetOk'));
    }
}

async function factoryReset() {
    if (!(await uiConfirm(translate('msg', 'factoryConfirmReset')))) {
        return;
    }

    if (await postJson(ROUTE_FACTORY_RESET, undefined, true)) {
        await uiAlert(translate('msg', 'factoryResetReboot'));
    }
}

async function rebootDevice() {
    if (!(await uiConfirm(translate('msg', 'systemConfirmReboot')))) {
        return;
    }

    if (await postJson(ROUTE_REBOOT)) {
        await uiAlert(translate('msg', 'systemRebooting'));
    }
}

// ============================================================
//                           Tabs
// ============================================================

function updateTabIndicator(tabName) {
    const btn = mainTabBtn[tabName || activeMainTab];

    if (!tabIndicator || !btn) {
        return;
    }

    tabIndicator.style.width = `${btn.offsetWidth}px`;
    tabIndicator.style.transform = `translateX(${btn.offsetLeft}px)`;
}

function setMainTab(name) {
    if (!MAIN_TABS.includes(name)) {
        name = 'status';
    }

    const prev = activeMainTab;
    for (const tab of MAIN_TABS) {
        const active = tab === name;
        const btn = mainTabBtn[tab];
        const panel = mainTabPanel[tab];

        if (btn) {
            btn.classList.toggle('active', active);
            btn.setAttribute('aria-selected', active ? 'true' : 'false');
        }
        if (panel) {
            panel.hidden = !active;
        }
    }

    activeMainTab = name;
    updateTabIndicator(name);
    try {
        localStorage.setItem(MAIN_TAB_KEY, name);
    } catch {
        // ignore
    }

    if (name === 'status') {
        startStatusPolling();
        if (prev !== 'status') {
            refresh();
        }
    } else {
        stopStatusPolling();
        if (name === 'network') {
            fetchNetworkConfig();
        } else if (name === 'ssh') {
            fetchSshConfig();
        }
    }
}

// ============================================================
//                      Event Binding
// ============================================================

const PAGE_ACTIONS = {
    languageToggle: toggleLanguage,
    'reset-device': resetDeviceConfig,
    'reset-wifi': resetWifiConfig,
    'reset-ssh': resetSsh,
    reboot: rebootDevice,
    'factory-reset': factoryReset
};

function bindDirtyTracking() {
    for (const name of DEVICE_FORM_FIELDS) {
        const el = deviceField(name);

        if (!el) {
            continue;
        }

        el.addEventListener('input', function () {
            dirtyDeviceFields.add(name);
        });
    }

    for (const name of WIFI_FORM_FIELDS) {
        const el = wifiField(name);

        if (!el) {
            continue;
        }

        el.addEventListener(el.tagName === 'SELECT' ? 'change' : 'input', function () {
            dirtyWifiFields.add(name);
        });
    }

    for (const name of SSH_FORM_FIELDS) {
        const el = sshField(name);

        if (!el) {
            continue;
        }

        el.addEventListener(el.type === 'checkbox' ? 'change' : 'input', function () {
            dirtySshFields.add(name);
        });
    }
}

function bindActions() {
    document.querySelectorAll('[data-action]').forEach((el) => {
        const action = el.dataset.action;

        if (action === 'ok' || action === 'cancel') {
            return;
        }

        const fn = PAGE_ACTIONS[action];

        if (fn) {
            bindTap(el, fn);
        }
    });

    const allowNoAuthEl = sshField('allowNoAuth');
    if (allowNoAuthEl) {
        allowNoAuthEl.addEventListener('change', function () {
            dirtySshFields.add('allowNoAuth');
            updateSshAuthFields();
        });
    }

    if (deviceForm) {
        deviceForm.addEventListener('submit', function (e) {
            e.preventDefault();
            saveDeviceConfig();
        });
    }

    if (wifiForm) {
        wifiForm.addEventListener('submit', function (e) {
            e.preventDefault();
            saveWifiConfig();
        });

        const prefModeEl = wifiField('prefMode');
        if (prefModeEl) {
            prefModeEl.addEventListener('change', updateWifiFieldVisibility);
        }
    }

    if (sshForm) {
        sshForm.addEventListener('submit', function (e) {
            e.preventDefault();
            saveSsh();
        });
    }
}

function bindMainTabs() {
    let tab = 'status';
    try {
        tab = localStorage.getItem(MAIN_TAB_KEY);

        if (!tab || !MAIN_TABS.includes(tab)) {
            tab = 'status';
        }
    } catch {
        // ignore
    }

    setMainTab(tab);
    requestAnimationFrame(function () {
        updateTabIndicator(tab);
    });
    window.addEventListener('resize', function () {
        updateTabIndicator(activeMainTab);
    });

    for (const name of MAIN_TABS) {
        bindTap(mainTabBtn[name], function () {
            setMainTab(name);
        });
    }
}

// ============================================================
//                           Init
// ============================================================

function initPage() {
    setLanguage(storedLanguage());
    bindDirtyTracking();
    bindActions();
    bindMainTabs();

    if (uiModalOk) {
        bindTap(uiModalOk, function () {
            closeUiModal(true);
        });
    }

    if (uiModalCancel) {
        bindTap(uiModalCancel, function () {
            closeUiModal(false);
        });
    }

    updateWifiFieldVisibility();
    updateSshAuthFields();
}

if (document.readyState === 'loading') {
    document.addEventListener('DOMContentLoaded', initPage);
} else {
    initPage();
}
