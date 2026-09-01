#pragma once

/* Factory-style captive portal: POST JSON {ssid,password} to /wifi_config */
static const char WIFI_PORTAL_HTML[] = R"HTML(<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1.0">
<title>WiFi Configuration</title>
<style>
body{font-family:Arial,sans-serif;max-width:500px;margin:0 auto;padding:20px;background:#f5f5f5}
.header{text-align:center;margin-bottom:24px}
.container{background:#fff;padding:24px;border-radius:12px;box-shadow:0 2px 15px rgba(0,0,0,.08)}
label{display:block;margin:12px 0 6px;font-weight:600;color:#333}
input{width:100%;padding:11px 15px;border:1px solid #e0e0e0;border-radius:8px;box-sizing:border-box;font-size:16px}
button{width:100%;margin-top:24px;padding:13px;background:#4d90fe;color:#fff;border:none;border-radius:8px;font-size:16px;font-weight:600}
button:disabled{background:#e0e0e0}
.hint{color:#888;font-size:13px;margin-top:12px;text-align:center}
</style>
</head>
<body>
<div class="header"><h1>WiFi Configuration</h1></div>
<div class="container">
<label for="ssid">SSID</label>
<input id="ssid" placeholder="Enter WiFi SSID" required>
<label for="password">Password</label>
<input id="password" type="password" placeholder="Enter WiFi Password">
<button id="btn1" onclick="sendWifiConfig()">Submit WiFi Config</button>
<p class="hint">Device: ESP32S3_1_54</p>
</div>
<script>
function sendWifiConfig(){
  const ssid=document.getElementById('ssid').value.trim();
  const password=document.getElementById('password').value;
  const btn=document.getElementById('btn1');
  if(!ssid){alert('Please enter SSID');return;}
  btn.disabled=true;btn.textContent='Submitting...';
  const xhr=new XMLHttpRequest();
  xhr.open('POST','/wifi_config',true);
  xhr.setRequestHeader('Content-Type','application/json');
  xhr.onload=()=>{
    btn.disabled=false;btn.textContent='Submit WiFi Config';
    if(xhr.status>=200&&xhr.status<300){
      alert('Configuration Success');
    }else{
      alert('Submission Failed');
    }
  };
  xhr.onerror=()=>{btn.disabled=false;btn.textContent='Submit WiFi Config';alert('Submission Failed');};
  xhr.send(JSON.stringify({ssid,password}));
}
</script>
</body>
</html>
)HTML";
