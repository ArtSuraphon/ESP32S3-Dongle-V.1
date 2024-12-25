const RESTART = '/restart';
const OTA_UPLOAD = '/ota/';
const BLE_DB = '/ble/bonding_db';
const BLE_DEVICES = '/ble/devices';
const CONFIG_FILE_PATH = '/fs/config.json';
const LATEST_RELEASE = 'https://github.com/shmuelzon/esp32-ble2mqtt/releases';
const STATUS = '/status';

function progress(show = true) {
    document.getElementById('progress').style.display = show ? 'flex' : 'none';
    // disable\enable all buttons
    [].concat(
        Array.from(document.getElementsByTagName('button')), //all buttons
        Array.from(document.getElementsByClassName('button'))  //all elements same button
    )
        .map(item => {
            if (show) item.setAttribute('disabled', show)
            else item.removeAttribute('disabled')
        });
}

function toaster(message) {
    document.getElementById('message').style.display = 'block';
    document.getElementById('message').innerText = message;
    setTimeout(() => document.getElementById('message').style.display = 'none', 3000);
}

function restart() {
    fetch(RESTART, {
        method: 'POST',
    })
        .then((res) => {
            window.open("/busy.html", "_self")
        })
        .catch((err) => {
            toaster("Can't restart");
            console.error(err)
        })
}

function getStatus() {
    fetch(STATUS)
        .then(response => response.json())
        .then(json => {
            document.getElementById('software-version').innerHTML = json.version;
        });
}

function getLatestReleaseInfo() {
    fetch('https://api.github.com/repos/shmuelzon/esp32-ble2mqtt/tags')
        .then(response => response.json())
        .then(json => {
            let release = json[0];
            if (release) document.getElementById('latest-release-id').innerHTML = `(latest <a href="${LATEST_RELEASE}" target="_blank">${release.name}</a>)`;
        });
}

function otaStartUpload(type) {
    let file = document.getElementById(type + '-file').files[0];
    if (!file)
        return;
    progress(true);
    fetch(OTA_UPLOAD + type, {
        method: 'POST',
        headers: {
            'Content-Type': 'application/octet-stream'
        },
        body: file
    })
        .then((res) => {
            toaster("Upload complete");
            window.open("/busy.html", "_self")
        })
        .catch((err) => {
            toaster(`Can't upload ${type}. Please refresh page`);
            console.error(err)
        })
}

function bleClearBonding() {
    progress(true);
    fetch(BLE_DB, {
        method: 'DELETE',
    })
        .then((res) => {
            progress(false);
            toaster("Cleared");
            console.log("cleared")
        })
        .catch((err) => {
            toaster("Can't clear ble");
            console.error(err)
        })
}

function downloadConfig() {
    fetch(CONFIG_FILE_PATH, {
        method: 'GET',
        cache: 'no-store'
    })
        .then(response => response.json())
        .then(json => {
            document.getElementById('config-file').innerHTML = JSON.stringify(json, null, 2);
        })
        .catch((err) => {
            toaster("Can't download config file");
            console.error(err)
        })
}

function uploadConfigFile() {
    let file = new File([document.getElementById('config-file').value], "config.json", {
        type: "text/plain",
    });
    if (file.size === 0) {
        toaster("File is empty");
        return;
    }
    progress(true);
    fetch(CONFIG_FILE_PATH, {
        method: 'POST',
        headers: {
            'Content-Type': 'application/octet-stream'
        },
        body: file
    })
        .then(response => {
            restart();
        })
        .catch((err) => {
            toaster("Can't upload config file");
            console.error(err)
        })
}

function bleListUpdate() {
    progress(true);
    fetch(BLE_DEVICES, {
        method: 'GET',
    })
        .then(response => {
            if (!response.ok) {
                throw new Error("HTTP status " + response.status);
            }
            return response.json();
        })
        .then(json => {
            progress(false);
            // render BLE table
            document.getElementById('ble-list').innerHTML = json.map(item => {
                return `
                <tr>
                    <td><mark ${item.connected ? 'class="tertiary" title="connected"' : 'class="secondary" title="disconnected"'}>${item.name || "[None]"}</mark></td>
                    <td>${item.mac}</td> 
                </tr>`;
            }).join('\n');
        })
        .catch((err) => {
            progress(false);
            toaster("Can't update list of ble");
            console.error(err)
        })
}


function loadFileManager(path) {
    path ||= document.getElementById('file-manager-path').innerText;

    fetch('/fs' + path,
        {
            method: "GET",
        })
        .then(response => {
            return response.json();
        })
        .then(data => {
            document.getElementById('file-manager-list').innerHTML =
                `<tr>
                 <td><span style="cursor: pointer; font-weight: bold" title="Up" onclick="loadFileManager('${path}')">..</span><span id="file-manager-path" style="cursor: pointer;">${path}</span></td>   
                 <td></td>   
                 <td></td>   
                </tr>` +
                data.map(entry => {
                    let name, del, download = '';
                    if (entry.type === 'directory') {
                        name = `<span style="cursor: pointer" onclick="loadFileManager('${path + '/' + entry.name}')">📁 ${entry.name}</span>`;
                    } else {
                        name = entry.name;
                        del = `<span class="icon-delete" title="delete file" style="cursor: pointer" onclick="deleteFile('${path + entry.name}')"></span>`;
                        download = `<a href="/fs${path}${entry.name}" target="_blank"><span class="icon-link" title="download file"></span></a>`;
                    }
                    return `<tr>
                            <td>${name}</td>
                            <td>${entry.type === 'file' ? entry.size : ''}</td>
                            <td>${del} 
                                ${download}
                            </td>
                            </tr>`
                }).join('\n')

        })
        .catch(err => {
                toaster(`Can't get ${path}`);
                console.error(err);
            }
        )
}

function uploadFile() {
    let file = document.getElementById('file-manager-file').files[0];
    if (!file) {
        toaster("File is empty");
        return;
    }
    let path = document.getElementById('file-manager-path').innerText;
    progress(true);
    fetch('/fs' + path + file.name, {
        method: 'POST',
        headers: {
            'Content-Type': 'application/octet-stream'
        },
        body: file
    })
        .then(response => {
            toaster("OK");
            progress(false);
            loadFileManager(path);
        })
        .catch((err) => {
            toaster("Can't upload file");
            console.error(err)
        })
}

function deleteFile(fullName) {
    if (!fullName) {
        return;
    }
    fetch('/fs' + fullName, {
        method: 'DELETE',
        headers: {
            'Content-Type': 'application/octet-stream'
        },
    })
        .then(response => {
            toaster("OK");
            progress(false);
            loadFileManager();
        })
        .catch((err) => {
            toaster("Can't delete file");
            console.error(err)
        })
}

function uploadConfigFile() {
    let file = new File([document.getElementById('config-file').value], "config.json", {
        type: "text/plain",
    });
    if (file.size === 0) {
        toaster("File is empty");
        return;
    }
    progress(true);
    fetch(CONFIG_FILE_PATH, {
        method: 'POST',
        headers: {
            'Content-Type': 'application/octet-stream'
        },
        body: file
    })
        .then(response => {
            restart();
        })
        .catch((err) => {
            toaster("Can't upload config file");
            console.error(err)
        })
}

function togglePasswordVisibility(passwordFieldId) {
    const passwordField = document.getElementById(passwordFieldId);
    const checkbox = document.getElementById(`show-${passwordFieldId}`);
    passwordField.type = checkbox.checked ? 'text' : 'password';
}

function toggleFields() {
    const authType = document.getElementById('auth-type').value;
    const personalFields = document.getElementById('personal-fields');
    const enterpriseFields = document.getElementById('enterprise-fields');

    if (authType === 'WPA2-Personal') {
        personalFields.style.display = 'block';
        enterpriseFields.style.display = 'none';
    } else {
        personalFields.style.display = 'none';
        enterpriseFields.style.display = 'block';
    }
}

// Fetch and populate form with current config
async function fetchConfig() {
    try {
        const response = await fetch('/fs/config.json');
        const config = await response.json();

        if (config.network.wifi.eap.method === 'PEAP') {
            document.getElementById('auth-type').value = 'WPA2-Enterprise';
            toggleFields();

            document.getElementById('enterprise-ssid').value = config.network.wifi.ssid || '';
            document.getElementById('eap-username').value = config.network.wifi.eap.username || '';
            document.getElementById('eap-password').value = config.network.wifi.eap.password || '';
        } else {
            document.getElementById('auth-type').value = 'WPA2-Personal';
            toggleFields();

            document.getElementById('wifi-ssid').value = config.network.wifi.ssid || '';
            document.getElementById('wifi-password').value = config.network.wifi.password || '';
        }
    } catch (error) {
        alert('Failed to fetch config: ' + error.message);
    }
}

// Save updated config back to the server
async function saveConfig() {
    // 1. ตรวจสอบข้อมูลที่กรอก
    if (!validateForm()) {
        return;
    }

    try {
        // 2. แสดง progress indicator
        progress(true);

        // 3. ดึงข้อมูลการตั้งค่าที่มีอยู่
        const response = await fetch('/fs/config.json');
        const existingConfig = await response.json();

        // 4. เตรียมข้อมูล WiFi ตามประเภทการเชื่อมต่อ
        const authType = document.getElementById('auth-type').value;
        let wifiConfig;

        if (authType === 'WPA2-Personal') {
            wifiConfig = {
                ssid: document.getElementById('wifi-ssid').value,
                password: document.getElementById('wifi-password').value,
                eap: {
                    method: null,
                    identity: null,
                    client_cert: null,
                    client_key: null,
                    server_cert: null,
                    username: null,
                    password: null,
                },
            };
        } else {
            wifiConfig = {
                ssid: document.getElementById('enterprise-ssid').value,
                password: null,
                eap: {
                    method: 'PEAP',
                    identity: 'EAP',
                    client_cert: null,
                    client_key: null,
                    server_cert: null,
                    username: document.getElementById('eap-username').value,
                    password: document.getElementById('eap-password').value,
                },
            };
        }

        // 5. อัปเดตเฉพาะส่วน wifi ในการตั้งค่า
        existingConfig.network = existingConfig.network || {};
        existingConfig.network.wifi = wifiConfig;

        // 6. บันทึกการตั้งค่า
        const saveResponse = await fetch('/fs/config.json', {
            method: 'POST',
            headers: {
                'Content-Type': 'application/json',
            },
            body: JSON.stringify(existingConfig),
        });

        if (!saveResponse.ok) {
            throw new Error('Failed to save configuration');
        }

        // 7. แสดงข้อความสำเร็จ
        toaster('Connecting to Wi-Fi, please wait...');

        // 8. หน่วงเวลาเล็กน้อยเพื่อให้ผู้ใช้เห็นข้อความ
        await new Promise(resolve => setTimeout(resolve, 2000));

        // 9. รีสตาร์ทระบบ
        await fetch(RESTART, {
            method: 'POST',
        });

        // 10. เปลี่ยนไปยังหน้า busy
        window.open("/busy.html", "_self");

    } catch (error) {
        // จัดการกรณีเกิดข้อผิดพลาด
        console.error('Error:', error);
        toaster('An error has occurred: ' + error.message);
        progress(false);
    }
}

function validateWiFiFields() {
    const requiredFields = document.querySelectorAll('#wifi-ssid, #wifi-password, #enterprise-ssid, #eap-username, #eap-password');
    let errors = [];

    requiredFields.forEach(field => {
        if (!field.value.trim()) {
            errors.push(`Please fill in the ${field.getAttribute('placeholder') || field.id.replace('-', ' ')}.`);
        }
    });

    // Display errors (if any)
    if (errors.length > 0) {
        const errorMessage = errors.join('\n');
        toaster(errorMessage);
        return false;
    }

    return true;
}
function validateWiFiFields() {
    const authType = document.getElementById('auth-type').value;
    let errors = [];

    if (authType === 'WPA2-Personal') {
        // Fields for WPA2-Personal
        const ssid = document.getElementById('wifi-ssid');
        const password = document.getElementById('wifi-password');

        if (!ssid.value.trim()) {
            errors.push('SSID');
        }
        if (!password.value.trim()) {
            errors.push('Password');
        }

        if (errors.length > 0) {
            toaster(`The following fields are empty: ${errors.join(', ')}.`);
            return false;
        }
    } else if (authType === 'WPA2-Enterprise') {
        // Fields for WPA2-Enterprise
        const enterpriseSsid = document.getElementById('enterprise-ssid');
        const username = document.getElementById('eap-username');
        const eapPassword = document.getElementById('eap-password');

        if (!enterpriseSsid.value.trim()) {
            errors.push('Enterprise SSID');
        }
        if (!username.value.trim()) {
            errors.push('Username');
        }
        if (!eapPassword.value.trim()) {
            errors.push('Password');
        }

        if (errors.length > 0) {
            toaster(`The following fields are empty: ${errors.join(', ')}.`);
            return false;
        }
    }

    return true;
}

// ปรับปรุงฟังก์ชัน validateForm เดิม
function validateForm() {
    return validateWiFiFields();
}

document.addEventListener("DOMContentLoaded", event => {
    getStatus();            // device status
    getLatestReleaseInfo(); // get github version
    bleListUpdate()         // update ble table
}); 

// async function saveConfig() {
//     try {
//         const authType = document.getElementById('auth-type').value;
//         let updatedConfig;

//         if (authType === 'WPA2-Personal') {
//             updatedConfig = {
//                 network: {
//                     wifi: {
//                         ssid: document.getElementById('wifi-ssid').value,
//                         password: document.getElementById('wifi-password').value,
//                         eap: {
//                             method: null,
//                             identity: null,
//                             client_cert: null,
//                             client_key: null,
//                             server_cert: null,
//                             username: null,
//                             password: null,
//                         },
//                     },
//                 },
//             };
//         } else {
//             updatedConfig = {
//                 network: {
//                     wifi: {
//                         ssid: document.getElementById('enterprise-ssid').value,
//                         password: null,
//                         eap: {
//                             method: 'PEAP',
//                             identity: 'EAP',
//                             client_cert: null,
//                             client_key: null,
//                             server_cert: null,
//                             username: document.getElementById('eap-username').value,
//                             password: document.getElementById('eap-password').value,
//                         },
//                     },
//                 },
//             };
//         }

//         const response = await fetch('/fs/config.json', {
//             method: 'POST',
//             headers: {
//                 'Content-Type': 'application/json',
//             },
//             body: JSON.stringify(updatedConfig),
//         });

//         if (response.ok) {
//             alert('Config saved successfully!');
//         } else {
//             alert('Failed to save config: ' + response.statusText);
//         }
//     } catch (error) {
//         alert('Error saving config: ' + error.message);
//     }
// }