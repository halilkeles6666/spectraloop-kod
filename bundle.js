(function(){const s=document.createElement("link").relList;if(s&&s.supports&&s.supports("modulepreload"))return;for(const a of document.querySelectorAll('link[rel="modulepreload"]'))t(a);new MutationObserver(a=>{for(const i of a)if(i.type==="childList")for(const C of i.addedNodes)C.tagName==="LINK"&&C.rel==="modulepreload"&&t(C)}).observe(document,{childList:!0,subtree:!0});function e(a){const i={};return a.integrity&&(i.integrity=a.integrity),a.referrerPolicy&&(i.referrerPolicy=a.referrerPolicy),a.crossOrigin==="use-credentials"?i.credentials="include":a.crossOrigin==="anonymous"?i.credentials="omit":i.credentials="same-origin",i}function t(a){if(a.ep)return;a.ep=!0;const i=e(a);fetch(a.href,i)}})();class ct{constructor(){this.listeners=new Map}on(s,e){return this.listeners.has(s)||this.listeners.set(s,[]),this.listeners.get(s).push(e),()=>this.off(s,e)}off(s,e){const t=this.listeners.get(s);if(t){const a=t.indexOf(e);a!==-1&&t.splice(a,1)}}emit(s,e){const t=this.listeners.get(s);t&&t.forEach(a=>a(e))}}const o=new ct,dt=`ws://${window.location.hostname}:5006`;class ht{constructor(){this._piWs=null,this._piConnected=!1,this._piReconnect=null,this._pingSentAt=null,this._connectPi()}_connectPi(){try{this._piWs=new WebSocket(dt),this._piWs.onopen=()=>{console.log("[WsBridge] Pi köprüsüne bağlandı"),clearTimeout(this._piReconnect)},this._piWs.onmessage=s=>{const e=s.data.trim();if(e==="PI_CONNECTED")this._piConnected=!0,this._setPiBadge(!0),console.log("[WsBridge] Pi bağlantısı aktif");else if(e==="PI_OFFLINE")this._piConnected=!1,this._setPiBadge(!1),console.warn("[WsBridge] Pi çevrimdışı");else if(e==="PONG")this._pingSentAt&&(o.emit("pi:latency",{ms:Math.round(performance.now()-this._pingSentAt)}),this._pingSentAt=null);else if(e.startsWith("{"))try{const t=JSON.parse(e);t.type==="status"?o.emit("conn:status",t.data):t.type==="temp"?o.emit("temp:update",t.data):t.type==="omron"?o.emit("omron:update",t.data):t.type==="motor"?o.emit("motor:update",t.data):t.type==="motor_dir_ack"?o.emit("motor_dir_ack",t):t.type==="cmd_log"?o.emit("cmd_log",t):t.type==="arduino_event"&&o.emit("arduino_event",t.event)}catch{}},this._piWs.onerror=()=>{this._piConnected=!1,this._setPiBadge(!1)},this._piWs.onclose=()=>{this._piConnected=!1,this._setPiBadge(!1),console.log("[WsBridge] Pi köprüsü bağlantısı koptu — 5sn sonra yeniden deneniyor"),this._piReconnect=setTimeout(()=>this._connectPi(),5e3)},setInterval(()=>{var s;((s=this._piWs)==null?void 0:s.readyState)===WebSocket.OPEN&&(this._pingSentAt=performance.now(),this._piWs.send("PING"))},5e3)}catch(s){console.warn("[WsBridge] Pi köprüsü başlatılamadı:",s),this._piReconnect=setTimeout(()=>this._connectPi(),5e3)}}_sendToPi(s){var e;((e=this._piWs)==null?void 0:e.readyState)===WebSocket.OPEN?(this._piWs.send(s),console.log("[WsBridge] Pi'ye gönderildi:",s)):console.warn("[WsBridge] Pi bağlı değil, komut gönderilemedi:",s)}_setPiBadge(s){const e=document.getElementById("pi-status-badge");e&&(e.textContent=s?"Pi Bağlı":"Pi Yok",e.className="pi-badge "+(s?"pi-badge-ok":"pi-badge-off")),o.emit("pi:status",{connected:s})}}class bt{constructor(s){this.container=s,this.brakeData={relay1:!0,valve1:!0,relay2:!0,valve2:!0},this.buildHTML(),this.cacheElements(),this.setupEventListeners(),this.updateBrakes(),this._startBrakeAnimation(),this.voiceAssistant=new ht,this._cmdLogLines=[],o.on("conn:status",e=>{let bc=!1;e.brake_f!==void 0&&(this.brakeData.relay1=!e.brake_f,this.brakeData.valve1=!e.brake_f,bc=!0),e.brake_r!==void 0&&(this.brakeData.relay2=!e.brake_r,this.brakeData.valve2=!e.brake_r,bc=!0),bc&&this.updateBrakes(),e.contactor!==void 0&&(this.contactorOn=e.contactor,this.els.contactorStatus&&(this.els.contactorStatus.textContent=e.contactor?"Kontaktör: Açık":"Kontaktör: Kapalı",this.els.contactorStatus.className="brake-status-item "+(e.contactor?"status-active":"status-free")),this.els.contactorToggleBtn&&this.els.contactorToggleBtn.classList.toggle("brake-btn-active",e.contactor)),e.ssr!==void 0&&(this.ssrOn=e.ssr,this.els.ssrStatus&&(this.els.ssrStatus.textContent=e.ssr?"SSR: Açık":"SSR: Kapalı",this.els.ssrStatus.className="brake-status-item "+(e.ssr?"status-active":"status-free")),this.els.ssrBtn&&this.els.ssrBtn.classList.toggle("brake-btn-active",e.ssr)),e.flasher!==void 0&&(this.flasherOn=e.flasher,this.els.flasherStatus&&(this.els.flasherStatus.textContent=e.flasher?"Flaşör: Açık":"Flaşör: Kapalı",this.els.flasherStatus.className="brake-status-item "+(e.flasher?"status-active":"status-free")),this.els.flasherBtn&&this.els.flasherBtn.classList.toggle("brake-btn-active",e.flasher)),e.buzzer!==void 0&&(this.buzzerOn=e.buzzer,this.els.buzzerBtn&&this.els.buzzerBtn.classList.toggle("brake-btn-active",e.buzzer)),e.stop_light!==void 0&&(this.stopLightOn=e.stop_light,this.els.stopLightStatus&&(this.els.stopLightStatus.textContent=e.stop_light?"Stop: Açık":"Stop: Kapalı",this.els.stopLightStatus.className="brake-status-item "+(e.stop_light?"status-active":"status-free")),this.els.stopLightBtn&&this.els.stopLightBtn.classList.toggle("brake-btn-active",e.stop_light))}),o.on("cmd_log",e=>{const tm=new Date((e.ts||Date.now()/1000)*1000).toLocaleTimeString("tr-TR");const line=`[${tm}] ${e.target==="system"?"⚠ ":"→"+e.target+": "}${e.command}`;this._cmdLogLines.unshift(line);if(this._cmdLogLines.length>80)this._cmdLogLines.length=80;const panel=document.getElementById("cmd-log-panel");if(panel)panel.innerHTML=this._cmdLogLines.map(l=>`<div>${l}</div>`).join("")})}_startBrakeAnimation(){this.drawBrakeVisual(0),this.drawMotorVisual(0)}buildHTML(){this.container.innerHTML=`
      <!-- ─── Üst Bağlantı Durumu Çubuğu ──────────────── -->
      <div class="conn-bar">
        <span class="conn-item" id="conn-jetson"><span class="conn-dot"></span><span class="conn-label">Jetson Nano</span></span>
        <span class="conn-item" id="conn-ipcam"><span class="conn-dot"></span><span class="conn-label">IP Kamera</span></span>
        <span class="conn-item" id="conn-arduino"><span class="conn-dot"></span><span class="conn-label">Arduino</span></span>
        <span class="conn-item conn-latency" id="conn-latency"><span class="conn-dot"></span><span class="conn-label">İletişim Hızı</span><span id="conn-latency-value" style="margin-left:4px;">--ms</span></span>
      </div>

      <div class="section-group theme-brake" style="margin-top:6px;">
        <div class="section-title">Sistem Komut Logu (Arduino/Pi)</div>
        <div class="section-content">
          <div class="panel-card" id="cmd-log-panel" style="padding:6px;max-height:160px;overflow-y:auto;font-family:monospace;font-size:10px;line-height:1.5;"></div>
        </div>
      </div>

      <!-- 2-Sutun Layout: Fren | Sesli Asistan -->
      <div class="dashboard-layout">

        <!-- ─── SOL SUTUN: Fren Sistemi ──────────────── -->
        <div class="layout-column layout-side">

          <div class="section-group theme-brake">
            <div class="section-title">Fren Sistemi</div>
            <div class="section-content">
              <div class="panel-card card-brake" style="padding:7px;">
                <div class="brake-visual-wrap">
                  <canvas id="brake-visual" class="brake-canvas" width="220" height="75"></canvas>
                </div>
                <div class="brake-btn-row">
                  <button class="brake-action-btn" id="brake-front-btn">On Fren</button>
                  <button class="brake-action-btn" id="brake-rear-btn">Arka Fren</button>
                  <button class="brake-action-btn brake-both-btn" id="brake-both-btn">Hepsi</button>
                </div>
                <div class="brake-btn-row" style="margin-top:2px;">
                  <button class="brake-action-btn" id="abs-front-btn" style="border-color:rgba(0,170,255,0.3);color:#00aaff;background:rgba(0,170,255,0.08);">ABS On</button>
                  <button class="brake-action-btn" id="abs-rear-btn" style="border-color:rgba(0,170,255,0.3);color:#00aaff;background:rgba(0,170,255,0.08);">ABS Arka</button>
                  <button class="brake-action-btn brake-both-btn" id="abs-all-btn" style="border-color:rgba(0,170,255,0.3);color:#00aaff;background:rgba(0,170,255,0.08);">ABS Hepsi</button>
                </div>
                <div class="brake-status-row">
                  <span class="brake-status-item" id="brake-front-status">On: Serbest</span>
                  <span class="brake-status-item" id="brake-rear-status">Arka: Serbest</span>
                </div>
                <div class="brake-status-row">
                  <span class="brake-status-item status-free" id="abs-front-status">ABS On: Kapalı</span>
                  <span class="brake-status-item status-free" id="abs-rear-status">ABS Arka: Kapalı</span>
                </div>
              </div>
            </div>
          </div>

          <div class="section-group theme-brake">
            <div class="section-title">Motor</div>
            <div class="section-content">
              <div class="panel-card" style="padding:7px;">
                <div class="motor-visual-wrap">
                  <canvas id="motor-visual" class="motor-canvas" width="220" height="56"></canvas>
                </div>
                <div class="brake-btn-row">
                  <button class="brake-action-btn" id="motor-forward-btn">İleri</button>
                  <button class="brake-action-btn" id="motor-reverse-btn">Geri</button>
                </div>
                <div class="motor-command-row">
                  <input type="number" class="motor-freq-input" id="motor-freq-input" min="0" max="60" step="0.1" value="0" placeholder="Hz"/>
                  <span class="metric-unit" style="margin-right:3px;">Hz</span>
                </div>
                <div class="brake-btn-row" style="margin-top:4px;">
                  <button class="brake-action-btn motor-start-btn" id="motor-start-btn">Başla</button>
                  <button class="brake-action-btn motor-stop-btn" id="motor-stop-btn">Dur</button>
                </div>
                <div class="brake-status-row" style="margin-top:6px;">
                  <span class="brake-status-item status-free" id="motor-status">Motor: Durduruldu</span>
                </div>
                <div class="metric"><span class="metric-label">Frekans</span><span><span class="metric-value" id="motor-freq-read">0.0</span><span class="metric-unit">Hz</span></span></div>
                <div class="metric"><span class="metric-label">Voltaj</span><span><span class="metric-value" id="motor-voltage-read">0.0</span><span class="metric-unit">V</span></span></div>
                <div class="metric"><span class="metric-label">Akım</span><span><span class="metric-value" id="motor-current-read">0.0</span><span class="metric-unit">A</span></span></div>
              </div>
            </div>
          </div>

          <div class="section-group theme-brake">
            <div class="section-title">Kontaktör</div>
            <div class="section-content">
              <div class="panel-card" style="padding:7px;">
                <div class="brake-btn-row">
                  <button class="brake-action-btn brake-both-btn" id="contactor-toggle-btn">Kontaktör</button>
                </div>
                <div class="brake-status-row" style="margin-top:4px;">
                  <span class="brake-status-item status-free" id="contactor-status">Kontaktör: -</span>
                </div>
              </div>
            </div>
          </div>

          <div class="section-group theme-brake">
            <div class="section-title">Sistemi Sıfırla</div>
            <div class="section-content">
              <div class="panel-card" style="padding:7px;">
                <div class="brake-btn-row">
                  <button class="brake-action-btn" id="system-reset-btn" style="background:rgba(255,80,0,0.12);border-color:rgba(255,120,0,0.4);color:#ff9944;">Sistemi Başlangıç Durumuna Getir</button>
                </div>
                <div class="brake-status-row" style="margin-top:4px;">
                  <span class="brake-status-item status-free" id="system-reset-status">Hazır</span>
                </div>
              </div>
            </div>
          </div>

          <div class="section-group theme-brake">
            <div class="section-title">Kontrol Paneli</div>
            <div class="section-content">
              <div class="panel-card" style="padding:7px;">
                <div class="brake-btn-row">
                  <button class="brake-action-btn ctrl-toggle-btn" id="ssr-btn">SSR</button>
                  <button class="brake-action-btn ctrl-toggle-btn" id="flasher-btn">Flaşör</button>
                  <button class="brake-action-btn ctrl-toggle-btn" id="stop-light-btn">Stop Lambası</button>
                  <button class="brake-action-btn ctrl-toggle-btn" id="neon-btn">Neon LED</button>
                </div>
                <div class="brake-btn-row">
                  <button class="brake-action-btn ctrl-toggle-btn" id="buzzer-btn">Buzzer</button>
                  <button class="brake-action-btn ctrl-toggle-btn" id="buzzer-beep-btn">Bip</button>
                </div>
                <div class="brake-status-row" style="margin-top:4px;">
                  <span class="brake-status-item status-free" id="ssr-status">SSR: Kapalı</span>
                  <span class="brake-status-item status-free" id="flasher-status">Flaşör: Kapalı</span>
                  <span class="brake-status-item status-free" id="stop-light-status">Stop: Kapalı</span>
                </div>
                <div class="brake-status-row">
                  <span class="brake-status-item status-free" id="neon-status">Neon: Kapalı</span>
                </div>
              </div>
            </div>
          </div>

          <div class="section-group theme-brake">
            <div class="section-title">Sensörler</div>
            <div class="section-content">
              <div class="panel-card" style="padding:7px;">
                <div class="metric">
                  <span class="metric-label">Sıcaklık</span>
                  <span><span class="metric-value" id="temp-value">--</span><span class="metric-unit">°C</span></span>
                </div>
                <div class="brake-status-row" style="margin-top:4px;">
                  <span class="brake-status-item status-free" id="temp-status">Sensör: Bekleniyor</span>
                </div>
              </div>
            </div>
          </div>

        </div>

        <!-- ─── SAG SUTUN: Sesli Asistan (kare, buyutulmus) ── -->
        <div class="layout-column layout-center">

          <div class="va-center-panel">

            <!-- Baslatma overlay -->
            <div class="va-overlay" id="va-overlay">
              <div class="va-overlay-inner">
                <img src="/videos/spectraloop_logo.png" class="va-overlay-logo" onerror="this.style.display='none'" />
                <div class="va-overlay-title">SPECTRALOOP</div>
                <div class="va-overlay-sub">Sesli Komut Sistemi</div>
                <div class="va-overlay-hint">▶ Baslat</div>
              </div>
            </div>

            <!-- Video kaldırıldı — Spectraloop logo -->
            <div class="va-video-stage" style="display:flex;align-items:center;justify-content:center;">
              <img src="/videos/spectraloop_logo.png" style="width:120px;height:120px;object-fit:contain;opacity:0.4;" onerror="this.style.display='none'" />
            </div>

            <!-- Alt durum cubugu -->
            <div class="va-statusbar">
              <button class="va-mic-btn va-idle" id="va-mic-btn" title="S tusu — bas ve tut">
                <svg width="16" height="16" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round">
                  <path d="M12 2a3 3 0 0 1 3 3v7a3 3 0 0 1-6 0V5a3 3 0 0 1 3-3z"/>
                  <path d="M19 10v2a7 7 0 0 1-14 0v-2"/>
                  <line x1="12" y1="19" x2="12" y2="23"/>
                  <line x1="8" y1="23" x2="16" y2="23"/>
                </svg>
              </button>
              <span class="va-status" id="va-status">Hazir  [ S = konus ]</span>
              <span class="pi-badge pi-badge-off" id="pi-status-badge">Pi Yok</span>
              <span class="va-badge va-badge-idle" id="va-badge">SPECTRA</span>
            </div>

            <!-- Bilgi satiri (transkript / komut / yanit) -->
            <div class="va-infobar">
              <span class="va-transcript-line" id="va-transcript"></span>
              <span class="va-cmd-line" id="va-command"></span>
              <span class="va-response-line" id="va-response"></span>
            </div>

          </div>

          <!-- IP Kamera + Omron — sağa yaslı, kamera üstte Omron altta -->
          <div class="ipcam-stack">
            <div class="ipcam-panel">
              <div class="ipcam-viewport" id="ipcam-viewport">
                <div class="ipcam-placeholder" id="ipcam-placeholder">
                  <div class="ipcam-icon">&#9881;</div>
                  <span id="ipcam-placeholder-text">IP Kamera Bekleniyor</span>
                </div>
                <iframe id="ipcam-frame" class="ipcam-frame" style="display:none;" scrolling="no" frameborder="0"></iframe>
                <img id="ipcam-img" class="ipcam-frame" style="display:none; object-fit:contain;" />
              </div>
            </div>

            <div class="section-group theme-brake omron-panel">
              <div class="section-title">Omron Sensör</div>
              <div class="section-content">
                <div class="panel-card" style="padding:7px;">
                  <div class="metric">
                    <span class="metric-label">Şerit</span>
                    <span><span class="metric-value" id="omron-stripe" style="font-size:26px;">-</span><span class="metric-unit" id="omron-stripe-total"></span></span>
                  </div>
                  <div class="brake-status-row" style="margin-top:4px;">
                    <span class="brake-status-item status-free" id="omron-status">Omron: Bekleniyor</span>
                  </div>
                </div>
              </div>
            </div>
          </div>

        </div>

      </div>
    `}cacheElements(){this.els={brakeCanvas:document.getElementById("brake-visual"),brakeFrontBtn:document.getElementById("brake-front-btn"),brakeRearBtn:document.getElementById("brake-rear-btn"),brakeBothBtn:document.getElementById("brake-both-btn"),brakeFrontStatus:document.getElementById("brake-front-status"),brakeRearStatus:document.getElementById("brake-rear-status"),contactorToggleBtn:document.getElementById("contactor-toggle-btn"),contactorStatus:document.getElementById("contactor-status"),systemResetBtn:document.getElementById("system-reset-btn"),systemResetStatus:document.getElementById("system-reset-status"),emergencyStopBtn:document.getElementById("emergency-stop-btn"),emergencyStopStatus:document.getElementById("emergency-stop-status"),motorCanvas:document.getElementById("motor-visual"),motorForwardBtn:document.getElementById("motor-forward-btn"),motorReverseBtn:document.getElementById("motor-reverse-btn"),motorStartBtn:document.getElementById("motor-start-btn"),motorStopBtn:document.getElementById("motor-stop-btn"),motorFreqInput:document.getElementById("motor-freq-input"),motorStatus:document.getElementById("motor-status"),motorFreqRead:document.getElementById("motor-freq-read"),motorVoltageRead:document.getElementById("motor-voltage-read"),motorCurrentRead:document.getElementById("motor-current-read"),omronStripe:document.getElementById("omron-stripe"),omronStripeTotal:document.getElementById("omron-stripe-total"),omronStatus:document.getElementById("omron-status"),absFrontBtn:document.getElementById("abs-front-btn"),absRearBtn:document.getElementById("abs-rear-btn"),absAllBtn:document.getElementById("abs-all-btn"),absFrontStatus:document.getElementById("abs-front-status"),absRearStatus:document.getElementById("abs-rear-status"),ssrBtn:document.getElementById("ssr-btn"),flasherBtn:document.getElementById("flasher-btn"),stopLightBtn:document.getElementById("stop-light-btn"),stopLightStatus:document.getElementById("stop-light-status"),neonBtn:document.getElementById("neon-btn"),buzzerBtn:document.getElementById("buzzer-btn"),buzzerBeepBtn:document.getElementById("buzzer-beep-btn"),ssrStatus:document.getElementById("ssr-status"),flasherStatus:document.getElementById("flasher-status"),neonStatus:document.getElementById("neon-status"),tempValue:document.getElementById("temp-value"),tempStatus:document.getElementById("temp-status"),connJetson:document.getElementById("conn-jetson"),connIpcam:document.getElementById("conn-ipcam"),connArduino:document.getElementById("conn-arduino"),connLatencyValue:document.getElementById("conn-latency-value"),ipcamViewport:document.getElementById("ipcam-viewport"),ipcamPlaceholder:document.getElementById("ipcam-placeholder"),ipcamPlaceholderText:document.getElementById("ipcam-placeholder-text"),ipcamFrame:document.getElementById("ipcam-frame")}}setupEventListeners(){this._setConnBadge(this.els.connJetson,!1),this._setConnBadge(this.els.connIpcam,!1),this._setConnBadge(this.els.connArduino,!1),o.on("pi:status",({connected:e})=>{this._setConnBadge(this.els.connJetson,e),e||(this.els.connLatencyValue.textContent="--ms")}),o.on("pi:latency",({ms:e})=>{this.els.connLatencyValue.textContent=`${e}ms`}),o.on("conn:status",e=>{e.ipcam!==void 0&&(this._setConnBadge(this.els.connIpcam,e.ipcam),e.ipcam&&e.ipcamUrl&&(this._ipcamUrl=e.ipcamUrl,this._connectIpcam())),e.arduino!==void 0&&this._setConnBadge(this.els.connArduino,e.arduino)}),this._ipcamUrl="/api/ipcam/stream",this._connectIpcam(),o.on("battery:emergency",()=>{this.brakeData={relay1:!1,valve1:!1,relay2:!1,valve2:!1},this.updateBrakes()}),o.on("brake:data",e=>{e.relay1!==void 0&&(this.brakeData.relay1=!!e.relay1),e.valve1!==void 0&&(this.brakeData.valve1=!!e.valve1),e.relay2!==void 0&&(this.brakeData.relay2=!!e.relay2),e.valve2!==void 0&&(this.brakeData.valve2=!!e.valve2),this.updateBrakes()}),this.els.brakeFrontBtn.addEventListener("click",()=>{const e=this.brakeData.relay1&&this.brakeData.valve1;this.brakeData.relay1=!e,this.brakeData.valve1=!e,o.emit("brake:command",{...this.brakeData}),this.updateBrakes();const t=e;this.voiceAssistant._sendToPi(t?"FRONT_ON":"FRONT_OFF")}),this.els.brakeRearBtn.addEventListener("click",()=>{const e=this.brakeData.relay2&&this.brakeData.valve2;this.brakeData.relay2=!e,this.brakeData.valve2=!e,o.emit("brake:command",{...this.brakeData}),this.updateBrakes();const t=e;this.voiceAssistant._sendToPi(t?"REAR_ON":"REAR_OFF")}),this.els.brakeBothBtn.addEventListener("click",()=>{const e=this.brakeData.relay1&&this.brakeData.valve1&&this.brakeData.relay2&&this.brakeData.valve2;this.brakeData.relay1=!e,this.brakeData.valve1=!e,this.brakeData.relay2=!e,this.brakeData.valve2=!e,o.emit("brake:command",{...this.brakeData}),this.updateBrakes(),e?(this.voiceAssistant._sendToPi("FRONT_ON"),this.voiceAssistant._sendToPi("REAR_ON"),this.voiceAssistant._sendToPi("MOTOR_STOP"),this.voiceAssistant._sendToPi("MOTOR_STOP"),this.voiceAssistant._sendToPi("MOTOR_STOP")):(this.voiceAssistant._sendToPi("FRONT_OFF"),this.voiceAssistant._sendToPi("REAR_OFF"))}),this.motorDirection="forward",this.motorRunning=!1,this.motorFreq=0;const s=()=>{this.els.motorForwardBtn.classList.toggle("brake-btn-active",this.motorDirection==="forward"),this.els.motorReverseBtn.classList.toggle("brake-btn-active",this.motorDirection==="reverse")};s(),this.els.motorForwardBtn.addEventListener("click",()=>{this.motorDirection="forward",s(),this.voiceAssistant._sendToPi("MOTOR_DIR_FORWARD")}),this.els.motorReverseBtn.addEventListener("click",()=>{this.motorDirection="reverse",s(),this.voiceAssistant._sendToPi("MOTOR_DIR_REVERSE")}),this.els.motorStartBtn.addEventListener("click",()=>{const e=Math.max(0,Math.min(60,parseFloat(this.els.motorFreqInput.value)||0));this.els.motorFreqInput.value=e;const t=this.motorDirection;this.voiceAssistant._sendToPi(t==="forward"?"MOTOR_DIR_FORWARD":"MOTOR_DIR_REVERSE"),this.voiceAssistant._sendToPi(`MOTOR_FREQ:${e}`),this.voiceAssistant._sendToPi("MOTOR_GO"),o.emit("motor:command",{direction:t,freq:e,run:!0}),this.motorRunning=!0,this.motorFreq=e,this.els.motorStatus.textContent=`Motor: ${t==="forward"?"İleri":"Geri"} Çalışıyor`,this.els.motorStatus.className="brake-status-item status-active"}),this.els.motorStopBtn.addEventListener("click",()=>this._stopMotor()),o.on("motor:update",e=>{e.freq!=null&&(this.els.motorFreqRead.textContent=Number(e.freq).toFixed(1)),e.voltage!=null&&(this.els.motorVoltageRead.textContent=Number(e.voltage).toFixed(1)),e.current!=null&&(this.els.motorCurrentRead.textContent=Number(e.current).toFixed(2)),e.direction!=null&&e.ok!=null&&e.run==null&&(this.els.motorStatus.textContent=`Yön: ${e.direction==="forward"?"İleri":"Geri"} — VFD'de ${e.ok?"ayarlandı ✓":"AYARLANAMADI ✗"}`,this.els.motorStatus.className="brake-status-item "+(e.ok?"status-active":"status-free"))}),o.on("motor_dir_ack",e=>{const dirText=e.command==="MOTOR_DIR_FORWARD"?"İleri":"Geri";this.els.motorStatus.textContent=e.ok?`Yön: ${dirText} — komut ulaştı ✓`:`Yön: ${dirText} — komut ULAŞMADI ✗`,this.els.motorStatus.className="brake-status-item "+(e.ok?"status-active":"status-free")}),o.on("omron:update",e=>{e.stripe!=null&&(this.els.omronStripe.textContent=e.stripe,this.els.omronStatus.textContent="Omron: Okuyor",this.els.omronStatus.className="brake-status-item status-active"),this.els.omronStripeTotal.textContent=e.total!=null?` / ${e.total}`:""}),this.contactorOn=!1,this.els.contactorToggleBtn&&this.els.contactorToggleBtn.addEventListener("click",()=>{this.contactorOn?(this.contactorOn=!1,this.els.contactorToggleBtn.classList.remove("brake-btn-active"),this.voiceAssistant._sendToPi("CONTACTOR_OFF"),this.els.contactorStatus&&(this.els.contactorStatus.textContent="Kontaktör: Kapalı",this.els.contactorStatus.className="brake-status-item status-free")):(this.contactorOn=!0,this.els.contactorToggleBtn.classList.add("brake-btn-active"),this.voiceAssistant._sendToPi("SSR_ON"),this.els.contactorStatus&&(this.els.contactorStatus.textContent="Kontaktör: Ön yükleme (SSR)...",this.els.contactorStatus.className="brake-status-item status-active"),setTimeout(()=>{this.voiceAssistant._sendToPi("CONTACTOR_ON"),setTimeout(()=>{this.voiceAssistant._sendToPi("SSR_OFF"),this.els.contactorStatus&&(this.els.contactorStatus.textContent="Kontaktör: Açık",this.els.contactorStatus.className="brake-status-item status-active")},300)},3000))}),this.els.systemResetBtn&&this.els.systemResetBtn.addEventListener("click",()=>{const cmds=["RELEASE","FLASHER_OFF","BUZZER_OFF","STOP_LIGHT_OFF","CONTACTOR_OFF","SSR_OFF"];cmds.forEach(c=>this.voiceAssistant._sendToPi(c)),setTimeout(()=>cmds.forEach(c=>this.voiceAssistant._sendToPi(c)),400),this.brakeData={relay1:!0,valve1:!0,relay2:!0,valve2:!0},this.updateBrakes(),this.flasherOn=!1,this.buzzerOn=!1,this.stopLightOn=!1,this.els.flasherBtn&&this.els.flasherBtn.classList.remove("brake-btn-active"),this.els.flasherStatus&&(this.els.flasherStatus.textContent="Flaşör: Kapalı",this.els.flasherStatus.className="brake-status-item status-free"),this.els.buzzerBtn&&this.els.buzzerBtn.classList.remove("brake-btn-active"),this.els.stopLightBtn&&this.els.stopLightBtn.classList.remove("brake-btn-active"),this.els.stopLightStatus&&(this.els.stopLightStatus.textContent="Stop: Kapalı",this.els.stopLightStatus.className="brake-status-item status-free"),this.els.contactorStatus&&(this.els.contactorStatus.textContent="Kontaktör: Kapalı",this.els.contactorStatus.className="brake-status-item status-active"),this.els.systemResetStatus&&(this.els.systemResetStatus.textContent="Sıfırlandı ✓",setTimeout(()=>{this.els.systemResetStatus&&(this.els.systemResetStatus.textContent="Hazır")},2000))}),this.emergencyStopActive=!1,this.els.emergencyStopBtn&&this.els.emergencyStopBtn.addEventListener("click",()=>{this.emergencyStopActive=!this.emergencyStopActive,this.emergencyStopActive?(o.emit("battery:emergency",{}),this._stopMotor(),this.voiceAssistant._sendToPi("EMERGENCY_STOP"),this.voiceAssistant._sendToPi("CONTACTOR_OFF"),this.els.emergencyStopStatus.textContent="Acil Stop: AKTİF",this.els.emergencyStopStatus.className="brake-status-item status-active",this.els.emergencyStopBtn.textContent="Acil Stopu Kaldır",this.els.emergencyStopBtn.classList.add("system-power-active")):(this.voiceAssistant._sendToPi("RELEASE"),this.voiceAssistant._sendToPi("BUZZER_OFF"),this.voiceAssistant._sendToPi("FLASHER_OFF"),this.voiceAssistant._sendToPi("CONTACTOR_ON"),this.brakeData={relay1:!0,valve1:!0,relay2:!0,valve2:!0},this.updateBrakes(),this.flasherOn=!1,this.buzzerOn=!1,this.els.flasherBtn&&this.els.flasherBtn.classList.remove("brake-btn-active"),this.els.flasherStatus&&(this.els.flasherStatus.textContent="Flaşör: Kapalı",this.els.flasherStatus.className="brake-status-item status-free"),this.els.buzzerBtn&&this.els.buzzerBtn.classList.remove("brake-btn-active"),this.els.emergencyStopStatus.textContent="Acil Stop: Pasif",this.els.emergencyStopStatus.className="brake-status-item status-free",this.els.emergencyStopBtn.textContent="ACİL STOP",this.els.emergencyStopBtn.classList.remove("system-power-active"))}),this.ssrOn=!1,this.els.ssrBtn.addEventListener("click",()=>{this.ssrOn=!this.ssrOn,this.voiceAssistant._sendToPi(this.ssrOn?"SSR_ON":"SSR_OFF"),this.els.ssrStatus.textContent=this.ssrOn?"SSR: Açık":"SSR: Kapalı",this.els.ssrStatus.className="brake-status-item "+(this.ssrOn?"status-active":"status-free"),this.els.ssrBtn.classList.toggle("brake-btn-active",this.ssrOn)}),this.flasherOn=!1,this.els.flasherBtn.addEventListener("click",()=>{this.flasherOn=!this.flasherOn,this.voiceAssistant._sendToPi(this.flasherOn?"FLASHER_ON":"FLASHER_OFF"),this.els.flasherStatus.textContent=this.flasherOn?"Flaşör: Açık":"Flaşör: Kapalı",this.els.flasherStatus.className="brake-status-item "+(this.flasherOn?"status-active":"status-free"),this.els.flasherBtn.classList.toggle("brake-btn-active",this.flasherOn)}),this.stopLightOn=!1,this.els.stopLightBtn&&this.els.stopLightBtn.addEventListener("click",()=>{this.stopLightOn=!this.stopLightOn,this.voiceAssistant._sendToPi(this.stopLightOn?"STOP_LIGHT_ON":"STOP_LIGHT_OFF"),this.els.stopLightStatus&&(this.els.stopLightStatus.textContent=this.stopLightOn?"Stop: Açık":"Stop: Kapalı",this.els.stopLightStatus.className="brake-status-item "+(this.stopLightOn?"status-active":"status-free")),this.els.stopLightBtn.classList.toggle("brake-btn-active",this.stopLightOn)}),this.neonOn=!1,this.els.neonBtn.addEventListener("click",()=>{this.neonOn=!this.neonOn,this.voiceAssistant._sendToPi(this.neonOn?"LED_ON":"LED_OFF"),this.els.neonStatus.textContent=this.neonOn?"Neon: Açık":"Neon: Kapalı",this.els.neonStatus.className="brake-status-item "+(this.neonOn?"status-active":"status-free"),this.els.neonBtn.classList.toggle("brake-btn-active",this.neonOn)}),this.buzzerOn=!1,this.els.buzzerBtn.addEventListener("click",()=>{this.buzzerOn=!this.buzzerOn,this.voiceAssistant._sendToPi(this.buzzerOn?"BUZZER_ON":"BUZZER_OFF"),this.els.buzzerBtn.classList.toggle("brake-btn-active",this.buzzerOn)}),this.els.buzzerBeepBtn.addEventListener("click",()=>{this.voiceAssistant._sendToPi("BUZZER_BEEP")}),this.absFrontOn=!1,this.els.absFrontBtn.addEventListener("click",()=>{this.absFrontOn=!this.absFrontOn,this.voiceAssistant._sendToPi(this.absFrontOn?"FRONT_ABS_ON":"FRONT_ABS_OFF"),this.els.absFrontStatus.textContent=this.absFrontOn?"ABS Ön: Aktif":"ABS Ön: Kapalı",this.els.absFrontStatus.className="brake-status-item "+(this.absFrontOn?"status-active":"status-free"),this.els.absFrontBtn.classList.toggle("brake-btn-active",this.absFrontOn),this.absFrontOn?(this.brakeData.relay1=!1,this.brakeData.valve1=!1):(this.brakeData.relay1=!0,this.brakeData.valve1=!0),this.updateBrakes()}),this.absRearOn=!1,this.els.absRearBtn.addEventListener("click",()=>{this.absRearOn=!this.absRearOn,this.voiceAssistant._sendToPi(this.absRearOn?"REAR_ABS_ON":"REAR_ABS_OFF"),this.els.absRearStatus.textContent=this.absRearOn?"ABS Arka: Aktif":"ABS Arka: Kapalı",this.els.absRearStatus.className="brake-status-item "+(this.absRearOn?"status-active":"status-free"),this.els.absRearBtn.classList.toggle("brake-btn-active",this.absRearOn),this.absRearOn?(this.brakeData.relay2=!1,this.brakeData.valve2=!1):(this.brakeData.relay2=!0,this.brakeData.valve2=!0),this.updateBrakes()}),this.absAllOn=!1,this.els.absAllBtn.addEventListener("click",()=>{this.absAllOn=!this.absAllOn,this.absFrontOn=this.absAllOn,this.absRearOn=this.absAllOn,this.voiceAssistant._sendToPi(this.absAllOn?"ALL_ABS_ON":"ALL_ABS_OFF"),this.els.absFrontStatus.textContent=this.absAllOn?"ABS Ön: Aktif":"ABS Ön: Kapalı",this.els.absFrontStatus.className="brake-status-item "+(this.absAllOn?"status-active":"status-free"),this.els.absRearStatus.textContent=this.absAllOn?"ABS Arka: Aktif":"ABS Arka: Kapalı",this.els.absRearStatus.className="brake-status-item "+(this.absAllOn?"status-active":"status-free"),this.els.absFrontBtn.classList.toggle("brake-btn-active",this.absAllOn),this.els.absRearBtn.classList.toggle("brake-btn-active",this.absAllOn),this.els.absAllBtn.classList.toggle("brake-btn-active",this.absAllOn),this.absAllOn?this.brakeData={relay1:!1,valve1:!1,relay2:!1,valve2:!1}:this.brakeData={relay1:!0,valve1:!0,relay2:!0,valve2:!0},this.updateBrakes()}),o.on("temp:update",e=>{e.value!=null&&(this.els.tempValue.textContent=Number(e.value).toFixed(1),this.els.tempStatus.textContent="Sensör: Aktif",this.els.tempStatus.className="brake-status-item status-active",e.value>50&&(this.els.tempStatus.textContent="SICAKLIK UYARISI!",this.els.tempStatus.style.color="#ff4444"))})}drawBrakeVisual(s=1/60){const e=this.els.brakeCanvas,t=e.getContext("2d"),a=220,i=75,C=e.clientWidth||a,et=window.devicePixelRatio||1,O=Math.max(1,C/a*et),G=Math.round(a*O),z=Math.round(i*O);(e.width!==G||e.height!==z)&&(e.width=G,e.height=z),t.setTransform(O,0,0,O,0,0),t.clearRect(0,0,a,i),this._brakeTime||(this._brakeTime=0),this._brakeTime+=s*1.8;const d=this._brakeTime,D=this.brakeData,V=D.relay1&&D.valve1,st=D.relay2&&D.valve2,n=a*.5,Y=[{key:"front",y:.32,free:V,label:"ÖN"},{key:"rear",y:.72,free:st,label:"ARKA"}];this._pistonExtend||(this._pistonExtend={front:3,rear:3});const c=t.createRadialGradient(n,i*.5,10,n,i*.5,a*.45);c.addColorStop(0,"rgba(255, 100, 60, 0.04)"),c.addColorStop(.5,"rgba(255, 60, 30, 0.02)"),c.addColorStop(1,"rgba(0, 0, 0, 0)"),t.fillStyle=c,t.fillRect(0,0,a,i);const m=10,k=i*.06,Z=i*.94,_=Z-k,u=t.createLinearGradient(n-m/2,0,n+m/2,0);u.addColorStop(0,"rgba(70, 75, 90, 0.9)"),u.addColorStop(.2,"rgba(100, 105, 120, 0.95)"),u.addColorStop(.5,"rgba(120, 125, 140, 1)"),u.addColorStop(.8,"rgba(95, 100, 115, 0.95)"),u.addColorStop(1,"rgba(65, 70, 85, 0.9)"),t.fillStyle=u,t.beginPath(),t.roundRect(n-m/2,k,m,_,3),t.fill(),t.strokeStyle="rgba(255, 255, 255, 0.1)",t.lineWidth=.5,t.beginPath(),t.moveTo(n,k+3),t.lineTo(n,Z-3),t.stroke();const y=14;for(let b=0;b<y;b++){const r=k+6+b*((_-12)/(y-1));t.fillStyle="rgba(60, 50, 40, 0.7)",t.fillRect(n-m/2-3,r-1.5,m+6,3),t.strokeStyle="rgba(255, 255, 255, 0.04)",t.lineWidth=.5,t.strokeRect(n-m/2-3,r-1.5,m+6,3)}const p=.12+.06*Math.sin(d*3);t.save(),t.shadowColor=`rgba(255, 140, 60, ${p*2})`,t.shadowBlur=8,t.strokeStyle=`rgba(255, 120, 60, ${p})`,t.lineWidth=1,t.beginPath(),t.roundRect(n-m/2,k,m,_,3),t.stroke(),t.restore();const A=d*.3%1,h=k+4+A*(_-8),R=t.createRadialGradient(n,h,0,n,h,7);R.addColorStop(0,`rgba(255, 200, 100, ${.5+.2*Math.sin(d*5)})`),R.addColorStop(.4,"rgba(255, 140, 60, 0.15)"),R.addColorStop(1,"rgba(255, 100, 40, 0)"),t.fillStyle=R,t.beginPath(),t.arc(n,h,7,0,Math.PI*2),t.fill(),t.font='6px "Segoe UI"',t.fillStyle="rgba(255, 120, 60, 0.2)",t.textAlign="left",t.fillText("FREN SYS",4,9),t.textAlign="right",t.fillText("HLR-BRK",a-4,9),Y.forEach(b=>{const r=i*b.y,l=!b.free,j=a*.14-1,nt=l?j:3,ot=1-Math.exp(-6.3*s);this._pistonExtend[b.key]+=(nt-this._pistonExtend[b.key])*ot;const rt=this._pistonExtend[b.key],w=24,S=16,H=7,J=S+4;if([-1,1].forEach(B=>{const X=n+B*(a*.14+w/2),v=X-w/2,g=r-S/2;t.save();const lt=l?"rgba(255, 60, 40, 0.15)":"rgba(0, 200, 140, 0.08)";t.shadowColor=lt,t.shadowBlur=16,t.beginPath(),t.arc(X,r,S*.6,0,Math.PI*2),t.fillStyle="rgba(0,0,0,0.01)",t.fill(),t.restore(),t.save(),t.shadowColor="rgba(0, 0, 0, 0.3)",t.shadowBlur=4,t.shadowOffsetY=2;const x=t.createLinearGradient(v,g,v,g+S);x.addColorStop(0,"rgba(140, 150, 170, 0.95)"),x.addColorStop(.15,"rgba(190, 200, 215, 1)"),x.addColorStop(.4,"rgba(210, 218, 230, 1)"),x.addColorStop(.6,"rgba(180, 188, 200, 0.95)"),x.addColorStop(.85,"rgba(130, 140, 160, 0.9)"),x.addColorStop(1,"rgba(100, 110, 130, 0.85)"),t.fillStyle=x,t.beginPath(),t.roundRect(v,g,w,S,3),t.fill(),t.restore(),t.strokeStyle="rgba(255, 255, 255, 0.2)",t.lineWidth=.8,t.beginPath(),t.roundRect(v,g,w,S,3),t.stroke();for(let f=0;f<4;f++){const N=v+4+f*5;t.strokeStyle="rgba(0, 0, 0, 0.12)",t.lineWidth=.7,t.beginPath(),t.moveTo(N,g+3),t.lineTo(N,g+S-3),t.stroke(),t.strokeStyle="rgba(255, 255, 255, 0.06)",t.beginPath(),t.moveTo(N+1,g+3),t.lineTo(N+1,g+S-3),t.stroke()}t.save(),t.font='bold 6px "Segoe UI"',t.textAlign="center",t.textBaseline="middle",t.fillStyle="rgba(20, 24, 34, 0.8)",t.fillText(b.label,X,g+S/2+.5),t.restore();const L=Math.max(rt-H,1),Q=4,P=r-Q/2,K=t.createLinearGradient(0,P,0,P+Q);K.addColorStop(0,"rgba(170, 180, 195, 0.9)"),K.addColorStop(.5,"rgba(200, 210, 220, 0.95)"),K.addColorStop(1,"rgba(150, 160, 175, 0.85)");let E;B===-1?(E=v+w,t.fillStyle=K,t.fillRect(E,P,L,Q),t.strokeStyle="rgba(255, 255, 255, 0.15)",t.lineWidth=.5,t.beginPath(),t.moveTo(E,P+1),t.lineTo(E+L,P+1),t.stroke()):(E=v-L,t.fillStyle=K,t.fillRect(E,P,L,Q),t.strokeStyle="rgba(255, 255, 255, 0.15)",t.lineWidth=.5,t.beginPath(),t.moveTo(E,P+1),t.lineTo(E+L,P+1),t.stroke());let T;B===-1?T=v+w+L:T=v-L-H;const $=r-J/2;t.save(),t.shadowColor=l?"rgba(255, 50, 30, 0.4)":"rgba(0, 0, 0, 0.2)",t.shadowBlur=l?8:3;const W=t.createLinearGradient(T,$,T,$+J);W.addColorStop(0,"rgba(30, 30, 35, 0.95)"),W.addColorStop(.2,"rgba(50, 50, 58, 1)"),W.addColorStop(.5,"rgba(60, 60, 68, 1)"),W.addColorStop(.8,"rgba(45, 45, 52, 0.95)"),W.addColorStop(1,"rgba(25, 25, 30, 0.9)"),t.fillStyle=W;const it=B===-1?[0,3,3,0]:[3,0,0,3];t.beginPath(),t.roundRect(T,$,H,J,it),t.fill(),t.restore(),t.strokeStyle="rgba(255, 255, 255, 0.08)",t.lineWidth=.5,t.beginPath(),t.roundRect(T,$,H,J,it),t.stroke();for(let f=0;f<3;f++){const N=$+4+f*5;t.strokeStyle="rgba(255, 255, 255, 0.04)",t.lineWidth=.5,t.beginPath(),t.moveTo(T+1,N),t.lineTo(T+H-1,N),t.stroke()}const q=X,U=g-5;t.save();const I=t.createRadialGradient(q,U,0,q,U,8);l?(I.addColorStop(0,"rgba(255, 50, 50, 0.8)"),I.addColorStop(.4,"rgba(255, 30, 30, 0.2)"),I.addColorStop(1,"rgba(255, 0, 0, 0)")):(I.addColorStop(0,"rgba(0, 220, 120, 0.6)"),I.addColorStop(.4,"rgba(0, 200, 100, 0.15)"),I.addColorStop(1,"rgba(0, 200, 100, 0)")),t.fillStyle=I,t.beginPath(),t.arc(q,U,8,0,Math.PI*2),t.fill(),t.shadowColor=l?"rgba(255, 50, 50, 0.9)":"rgba(0, 220, 120, 0.9)",t.shadowBlur=4,t.beginPath(),t.arc(q,U,2.5,0,Math.PI*2),t.fillStyle=l?"#ff4444":"#00dd77",t.fill(),t.beginPath(),t.arc(q-.5,U-.5,1,0,Math.PI*2),t.fillStyle="rgba(255, 255, 255, 0.5)",t.fill(),t.restore();const at=g+S+4;[v+6,v+w-6].forEach(f=>{t.save(),t.shadowColor="rgba(0, 150, 255, 0.3)",t.shadowBlur=3,t.fillStyle="rgba(40, 50, 65, 0.9)",t.beginPath(),t.arc(f,at,2.5,0,Math.PI*2),t.fill(),t.strokeStyle="rgba(0, 170, 255, 0.3)",t.lineWidth=.5,t.stroke(),t.restore(),t.strokeStyle="rgba(0, 150, 255, 0.08)",t.lineWidth=.5,t.setLineDash([2,2]),t.beginPath(),t.moveTo(f,at+3),t.lineTo(f,at+10),t.stroke(),t.setLineDash([])})}),l){const B=t.createRadialGradient(n,r,0,n,r,14);B.addColorStop(0,`rgba(255, 180, 80, ${.5+.15*Math.sin(d*4)})`),B.addColorStop(.3,"rgba(255, 100, 40, 0.25)"),B.addColorStop(.6,"rgba(255, 50, 20, 0.08)"),B.addColorStop(1,"rgba(255, 0, 0, 0)"),t.beginPath(),t.arc(n,r,14,0,Math.PI*2),t.fillStyle=B,t.fill(),t.save(),t.strokeStyle=`rgba(255, 120, 60, ${.3+.1*Math.sin(d*5)})`,t.lineWidth=1,t.beginPath(),t.moveTo(n-8,r),t.lineTo(n+8,r),t.stroke(),t.restore()}t.save(),t.font='bold 6px "Segoe UI"',t.fillStyle=l?"rgba(255, 80, 60, 0.5)":"rgba(0, 200, 120, 0.3)",t.textAlign="right",t.fillText(l?"KILITLI":"SERBEST",a-4,r+3),t.restore()});const M=i*.08+d*.15%1*i*.84,F=t.createLinearGradient(0,M-3,0,M+3);F.addColorStop(0,"rgba(255, 120, 60, 0)"),F.addColorStop(.5,"rgba(255, 120, 60, 0.06)"),F.addColorStop(1,"rgba(255, 120, 60, 0)"),t.fillStyle=F,t.fillRect(0,M-3,a,6)}drawMotorVisual(s=1/60){const e=this.els.motorCanvas;if(!e)return;const t=e.getContext("2d"),a=220,i=56,C=e.clientWidth||a,et=window.devicePixelRatio||1,O=Math.max(1,C/a*et),G=Math.round(a*O),z=Math.round(i*O);(e.width!==G||e.height!==z)&&(e.width=G,e.height=z),t.setTransform(O,0,0,O,0,0),t.clearRect(0,0,a,i),this._motorTime||(this._motorTime=0),this._motorPhase||(this._motorPhase=0);const d=!!this.motorRunning,D=this.motorFreq||0,V=this.motorDirection==="reverse"?-1:1;this._motorTime+=s;const st=d?1.8:.25;this._motorPhase+=V*st*s;const n=10,Y=10,c=i*.52,m=a-n-Y;t.fillStyle="rgba(0, 0, 0, 0.15)",t.fillRect(0,0,a,i),t.font='6px "Segoe UI"',t.textAlign="left",t.fillStyle="rgba(120, 180, 255, 0.25)",t.fillText("LINEER MOTOR",4,9),t.textAlign="right",t.fillStyle=d?"rgba(0, 220, 255, 0.45)":"rgba(255, 255, 255, 0.15)",t.fillText(d?V>0?"İLERİ":"GERİ":"BEKLEMEDE",a-4,9),t.strokeStyle="rgba(255, 255, 255, 0.08)",t.lineWidth=1,t.beginPath(),t.moveTo(n,c+13),t.lineTo(a-Y,c+13),t.stroke();const k=12,Z=m/(k-1),_=9,u=15;for(let y=0;y<k;y++){const p=n+y*Z,A=y/k,h=d?Math.max(.12,Math.pow(Math.max(0,Math.sin((this._motorPhase-A)*Math.PI*2)),2)):.08+.03*Math.sin(this._motorTime*.6+y),R=Math.round(30+h*60),M=Math.round(150+h*90),F=Math.round(210+h*45);t.save(),h>.5&&(t.shadowColor=`rgba(80, 200, 255, ${Math.min(1,h)})`,t.shadowBlur=10*h);const b=t.createLinearGradient(p,c-u/2,p,c+u/2);b.addColorStop(0,`rgba(${R}, ${M}, ${F}, ${.3+h*.5})`),b.addColorStop(1,`rgba(${R}, ${M}, ${F}, ${.1+h*.2})`),t.fillStyle=b,t.beginPath(),t.roundRect(p-_/2,c-u/2,_,u,2),t.fill(),t.restore(),t.strokeStyle=`rgba(255, 255, 255, ${.06+h*.1})`,t.lineWidth=.6;for(let r=0;r<3;r++){const l=c-u/2+3+r*5;t.beginPath(),t.moveTo(p-_/2+1,l),t.lineTo(p+_/2-1,l),t.stroke()}if(d&&h>.8&&Math.random()<.35){t.save(),t.strokeStyle=`rgba(210, 235, 255, ${.5+Math.random()*.4})`,t.lineWidth=.8,t.beginPath();let r=p,l=c-u/2-1;t.moveTo(r,l);for(let j=0;j<3;j++)r+=(Math.random()-.5)*6,l-=2.5+Math.random()*2.5,t.lineTo(r,l);t.stroke(),t.restore()}}if(d){const y=(this._motorPhase%1+1)%1,p=n+y*m,A=t.createRadialGradient(p,c,0,p,c,13);A.addColorStop(0,"rgba(190, 235, 255, 0.65)"),A.addColorStop(.4,"rgba(80, 180, 255, 0.22)"),A.addColorStop(1,"rgba(0, 120, 255, 0)"),t.beginPath(),t.arc(p,c,13,0,Math.PI*2),t.fillStyle=A,t.fill()}t.font='6px "Segoe UI"',t.textAlign="left",t.fillStyle="rgba(255, 255, 255, 0.2)",t.fillText(`${(d?D:0).toFixed(1)} Hz`,4,i-3)}_setConnBadge(s,e){s&&(s.className="conn-item "+(e?"online":"offline"))}_connectIpcam(){const s=this.els.ipcamFrame,e=document.getElementById("ipcam-img");if(!e)return;s.style.display="none";let t=0;const a=()=>{const i=new Image;i.onload=()=>{e.src=i.src,e.style.display="block",this.els.ipcamPlaceholder.style.display="none",this._setConnBadge(this.els.connIpcam,!0),t=0,setTimeout(a,500)},i.onerror=()=>{t++,t>3&&(this._setConnBadge(this.els.connIpcam,!1),this.els.ipcamPlaceholder.style.display="flex",e.style.display="none"),setTimeout(a,3e3)},i.src="/api/ipcam/snapshot?t="+Date.now()};a()}_stopMotor(){this.motorRunning=!1,this.motorFreq=0,this.voiceAssistant._sendToPi("MOTOR_STOP"),o.emit("motor:command",{direction:this.motorDirection,freq:0,run:!1}),this.els.motorStatus&&(this.els.motorStatus.textContent="Motor: Durduruldu",this.els.motorStatus.className="brake-status-item status-free")}updateBrakes(){const s=this.brakeData,e=s.relay1&&s.valve1,t=s.relay2&&s.valve2;this.els.brakeFrontBtn.className="brake-action-btn"+(!e?" brake-btn-active":""),this.els.brakeRearBtn.className="brake-action-btn"+(!t?" brake-btn-active":""),this.els.brakeBothBtn.className="brake-action-btn brake-both-btn"+(!e&&!t?" brake-btn-active":""),this.els.brakeFrontStatus.textContent=e?"Ön: Serbest":"Ön: Aktif",this.els.brakeFrontStatus.className="brake-status-item"+(e?" status-free":" status-active"),this.els.brakeRearStatus.textContent=t?"Arka: Serbest":"Arka: Aktif",this.els.brakeRearStatus.className="brake-status-item"+(t?" status-free":" status-active"),this.drawBrakeVisual()}}new bt(document.getElementById("dashboard-overlay"));
