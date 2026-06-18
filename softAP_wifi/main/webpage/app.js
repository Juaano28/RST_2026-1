var seconds = null;
var otaTimerVar = null;
var wifiConnectInterval = null;
var tempHistory = [];
const MAX_TEMP_POINTS = 20;

$(document).ready(function(){
    getUpdateStatus();
    startDHTSensorInterval();
    
    // Listeners para mostrar valor numérico de los sliders RGB
    $('#red_val').on('input', function(){ $('#red_display').text($(this).val()); });
    $('#green_val').on('input', function(){ $('#green_display').text($(this).val()); });
    $('#blue_val').on('input', function(){ $('#blue_display').text($(this).val()); });

    // Botones de interacción
    $("#send_rgb").on("click", function(){
        send_rgb_values();
    });
    
    $("#wifi_connect_btn").on("click", function(){
        connect_wifi();
    });
});

/* ================== OTA (código original del profesor) ================== */
function getFileInfo() {
    var x = document.getElementById("selected_file");
    var file = x.files[0];
    document.getElementById("file_info").innerHTML = "<h4>File: " + file.name + "<br>Size: " + file.size + " bytes</h4>";
}

function updateFirmware() {
    var formData = new FormData();
    var fileSelect = document.getElementById("selected_file");
    if (fileSelect.files && fileSelect.files.length == 1) {
        var file = fileSelect.files[0];
        formData.set("file", file, file.name);
        document.getElementById("ota_update_status").innerHTML = "Uploading " + file.name + ", Firmware Update in Progress...";
        var request = new XMLHttpRequest();
        request.upload.addEventListener("progress", updateProgress);
        request.open('POST', "/OTAupdate");
        request.responseType = "blob";
        request.send(formData);
    } else {
        window.alert('Select A File First');
    }
}

function updateProgress(oEvent) {
    if (oEvent.lengthComputable) {
        getUpdateStatus();
    } else {
        window.alert('total size is unknown');
    }
}

function getUpdateStatus() {
    var xhr = new XMLHttpRequest();
    var requestURL = "/OTAstatus";
    xhr.open('POST', requestURL, false);
    xhr.send('ota_update_status');
    if (xhr.readyState == 4 && xhr.status == 200) {
        var response = JSON.parse(xhr.responseText);
        document.getElementById("latest_firmware").innerHTML = response.compile_date + " - " + response.compile_time;
        if (response.ota_update_status == 1) {
            seconds = 10;
            otaRebootTimer();
        } else if (response.ota_update_status == -1) {
            document.getElementById("ota_update_status").innerHTML = "!!! Upload Error !!!";
        }
    }
}

function otaRebootTimer() {
    document.getElementById("ota_update_status").innerHTML = "OTA Firmware Update Complete. This page will close shortly, Rebooting in: " + seconds;
    if (--seconds == 0) {
        clearTimeout(otaTimerVar);
        window.location.reload();
    } else {
        otaTimerVar = setTimeout(otaRebootTimer, 1000);
    }
}

/* ================== Sensor DHT + Gráfica ================== */
function getDHTSensorValues() {
    $.getJSON('/dhtSensor.json', function(data) {
        var t = parseFloat(data["temp"]);
        var h = parseFloat(data["humidity"]);
        $("#temperature_reading").text(t.toFixed(1));
        $("#humidity_reading").text(h.toFixed(1));
        
        // Actualizar histórico y dibujar gráfica
        tempHistory.push(t);
        if (tempHistory.length > MAX_TEMP_POINTS) {
            tempHistory.shift();
        }
        drawTempChart();
    });
}

function drawTempChart() {
    var canvas = document.getElementById("temp_chart");
    if (!canvas || tempHistory.length < 2) return;
    
    var ctx = canvas.getContext("2d");
    var w = canvas.width;
    var h = canvas.height;
    var pad = 25;
    
    ctx.clearRect(0, 0, w, h);
    
    // Fondo de área de dibujo
    ctx.fillStyle = "rgba(255,255,255,0.3)";
    ctx.fillRect(pad, pad, w - 2*pad, h - 2*pad);
    
    // Calcular escalas
    var minT = Math.min.apply(null, tempHistory) - 1;
    var maxT = Math.max.apply(null, tempHistory) + 1;
    if (maxT - minT < 2) { maxT += 1; minT -= 1; }
    
    // Dibujar línea de temperatura
    ctx.beginPath();
    ctx.strokeStyle = "#d62828";
    ctx.lineWidth = 3;
    
    for (var i = 0; i < tempHistory.length; i++) {
        var x = pad + (i / (MAX_TEMP_POINTS - 1)) * (w - 2*pad);
        var y = h - pad - ((tempHistory[i] - minT) / (maxT - minT)) * (h - 2*pad);
        if (i === 0) ctx.moveTo(x, y);
        else ctx.lineTo(x, y);
        
        // Dibujar punto
        ctx.fillStyle = "#1d3557";
        ctx.fillRect(x-2, y-2, 4, 4);
    }
    ctx.stroke();
    
    // Ejes
    ctx.strokeStyle = "#1d3557";
    ctx.lineWidth = 1;
    ctx.beginPath();
    ctx.moveTo(pad, pad); ctx.lineTo(pad, h-pad); // Y
    ctx.lineTo(w-pad, h-pad); // X
    ctx.stroke();
}

function startDHTSensorInterval() {
    setInterval(getDHTSensorValues, 5000);
}

/* ================== WiFi Connect Status ================== */
function stopWifiConnectStatusInterval() {
    if (wifiConnectInterval != null) {
        clearInterval(wifiConnectInterval);
        wifiConnectInterval = null;
    }
}

function getWifiConnectStatus() {
    var xhr = new XMLHttpRequest();
    var requestURL = "/wifiConnectStatus";
    xhr.open('POST', requestURL, false);
    xhr.send('wifi_connect_status');
    
    if (xhr.readyState == 4 && xhr.status == 200) {
        var response = JSON.parse(xhr.responseText);
        document.getElementById("wifi_connect_status").innerHTML = "Conectando...";
        
        if (response.wifi_connect_status == 2) {
            document.getElementById("wifi_connect_status").innerHTML = "<h4 class='rd'>Fallo de conexión. Revisa credenciales.</h4>";
            stopWifiConnectStatusInterval();
        } else if (response.wifi_connect_status == 3) {
            document.getElementById("wifi_connect_status").innerHTML = "<h4 class='gr'>¡Conectado exitosamente!</h4>";
            stopWifiConnectStatusInterval();
        }
    }
}

function startWifiConnectStatusInterval() {
    wifiConnectInterval = setInterval(getWifiConnectStatus, 2800);
}

function connect_wifi() {
    var ssid = $("#wifi_ssid").val();
    var pwd = $("#wifi_password").val();
    
    if (!ssid) {
        alert("Ingresa el SSID");
        return;
    }
    
    $.ajax({
        url: '/wifiConnect.json',
        dataType: 'json',
        method: 'POST',
        cache: false,
        headers: {'my-connect-ssid': ssid, 'my-connect-pwd': pwd},
        data: {'timestamp': Date.now()},
        success: function() {
            document.getElementById("wifi_connect_status").innerHTML = "Iniciando conexión...";
            startWifiConnectStatusInterval();
        }
    });
}

/* ================== RGB LED Control ================== */
function send_rgb_values() {
    var red_val = $("#red_val").val();
    var green_val = $("#green_val").val();
    var blue_val = $("#blue_val").val();
    
    $.ajax({
        url: '/rgb_vals.json',
        dataType: 'json',
        method: 'POST',
        cache: false,
        headers: {'red_val': red_val, 'green_val': green_val, 'blue_val': blue_val},
        data: {'timestamp': Date.now()},
        success: function() {
            console.log("RGB enviado: R=" + red_val + " G=" + green_val + " B=" + blue_val);
        }
    });
}