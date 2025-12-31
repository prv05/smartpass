#include <Wire.h>
#include <WiFi.h>
#include <WebServer.h>
#include <Adafruit_SSD1306.h>
#include <Adafruit_MPU6050.h>
#include <Adafruit_BMP085.h>   // BMP180

/* ================= WIFI ================= */
const char* ssid = "PRV11R";//enter your wifi name 
const char* password = "PRAT2005";// enter your wifi password 
WebServer server(80);

/* ================= OLED ================= */
Adafruit_SSD1306 display(128, 64, &Wire, -1);

/* ================= MPU ================= */
Adafruit_MPU6050 mpu;
bool mpuOK = false;

/* ================= BMP180 ================= */
Adafruit_BMP085 bmp;
bool bmpOK = false;
float basePressure = 0;

/* ================= BUZZER ================= */
#define BUZZER_PIN 26

/* ================= APDS9960 ================= */
#define APDS_ADDR     0x39
#define ENABLE        0x80
#define GSTATUS       0xAF
#define GFIFO_L       0xFE
#define GFIFO_R       0xFF

void writeReg(uint8_t r, uint8_t v){
  Wire.beginTransmission(APDS_ADDR);
  Wire.write(r);
  Wire.write(v);
  Wire.endTransmission();
}
uint8_t readReg(uint8_t r){
  Wire.beginTransmission(APDS_ADDR);
  Wire.write(r);
  Wire.endTransmission();
  Wire.requestFrom(APDS_ADDR,1);
  return Wire.available()?Wire.read():0;
}

/* ================= STATE ================= */
int peopleCount = 0;
int maxCapacity = 30;
unsigned long lastEvent = 0;

/* ---- ENTRY / EXIT FIX ---- */
unsigned long lastDirectionTime = 0;
#define DIR_COOLDOWN 2000
#define ENTRY_TH  12
#define EXIT_TH  -18

/* ---- STAMPEDE ---- */
bool stampede = false;
unsigned long stampedeTime = 0;

/* ================= VIBRATION ================= */
unsigned long vibWindowStart = 0;
int vibHits = 0;

#define VIB_THRESHOLD     0.12
#define VIB_HIT_LIMIT     4
#define VIB_WINDOW_MS     1500
#define STAMPEDE_HOLD_MS  5000

/* ---- BMP PRESSURE ---- */
#define PRESSURE_DELTA_TH  0.6   // hPa sudden change

/* ---- BUZZER (OVER) ---- */
#define OVER_BEEP_ON   120
#define OVER_BEEP_OFF  800

/* ===== FUNCTION DECLARATIONS ===== */
void handleGesture();
void handleStampede();
void handleBuzzer();
void updateOLED();

/* ================= WEB DASHBOARD (UNCHANGED) ================= */
const char DASHBOARD[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
<meta charset="UTF-8">
<title>SMARTPASS Dashboard</title>
<meta name="viewport" content="width=device-width, initial-scale=1">
<script src="https://cdn.jsdelivr.net/npm/chart.js"></script>
<style>
body{margin:0;font-family:Segoe UI,Arial;background:#0b1e27;color:#fff}
.nav{height:55px;background:#102f3a;display:flex;align-items:center;padding:0 20px}
.wrapper{display:flex;height:calc(100vh - 55px)}
.sidebar{width:210px;background:#0e2731;padding:15px}
.tab{padding:12px;margin-bottom:10px;border-radius:8px;cursor:pointer}
.tab:hover,.tab.active{background:#183e4c}
.main{flex:1;padding:20px;overflow:auto}
.section{display:none}
.section.active{display:block}
.cards{display:flex;gap:20px;flex-wrap:wrap}
.card{background:#122f3b;padding:20px;border-radius:14px;min-width:220px}
.big{font-size:36px;font-weight:bold}
.safe{color:#4caf50}
.over{color:#ff9800}
.danger{color:#f44336}
canvas{background:transparent}
#popup{
display:none;position:fixed;top:20px;left:50%;transform:translateX(-50%);
padding:14px 28px;border-radius:10px;font-size:18px;font-weight:bold;z-index:999
}
</style>
</head>

<body>
<div class="nav">SMARTPASS – Crowd Safety System</div>

<div class="wrapper">
<div class="sidebar">
<div class="tab active" onclick="openTab('dash',this)">📊 Dashboard</div>
<div class="tab" onclick="openTab('analytics',this)">📈 Analytics</div>
<div class="tab" onclick="openTab('settings',this)">⚙ Settings</div>
</div>

<div class="main">

<div id="dash" class="section active">
<div class="cards">
<div class="card"><div>People Count</div><div class="big" id="cnt">0</div></div>
<div class="card"><div>Status</div><div class="big" id="status">SAFE</div></div>
</div><br>
<canvas id="lineChart"></canvas>
</div>

<div id="analytics" class="section">
<h3>Risk Distribution</h3>
<div style="width:260px;margin:auto">
<canvas id="donutChart" width="260" height="260"></canvas>
</div>
</div>

<div id="settings" class="section">
<h3>System Settings</h3>
<p>Max Allowed People</p>
<input type="number" id="cap" value="30">
<button onclick="setCap()">Save</button>
</div>

</div>
</div>

<div id="popup"></div>

<script>
function openTab(id,el){
document.querySelectorAll('.section').forEach(s=>s.classList.remove('active'));
document.querySelectorAll('.tab').forEach(t=>t.classList.remove('active'));
document.getElementById(id).classList.add('active');
el.classList.add('active');
}

let labels=[], data=[];
let safe=0, over=0, danger=0;

const lineChart=new Chart(document.getElementById("lineChart"),{
type:"line",
data:{labels:labels,datasets:[{label:"People Count",data:data,borderColor:"#4caf50",tension:0.3}]},
options:{scales:{x:{title:{display:true,text:"Time"}},y:{title:{display:true,text:"People"},beginAtZero:true}}}
});

const donutChart=new Chart(document.getElementById("donutChart"),{
type:"doughnut",
data:{labels:["SAFE","OVER","DANGER"],datasets:[{data:[safe,over,danger],backgroundColor:["#4caf50","#ff9800","#f44336"]}]},
options:{cutout:"70%",plugins:{legend:{position:"bottom"}}}
});

function setCap(){ fetch("/setcap?c="+cap.value); }

setInterval(()=>{
fetch("/data").then(r=>r.json()).then(d=>{
cnt.innerText=d.count;
status.innerText=d.state;
status.className="big "+(d.state=="SAFE"?"safe":d.state=="OVER"?"over":"danger");

labels.push(""); data.push(d.count);
if(labels.length>25){labels.shift();data.shift();}
lineChart.update();

if(d.state=="SAFE") safe++;
if(d.state=="OVER") over++;
if(d.state=="DANGER") danger++;
donutChart.data.datasets[0].data=[safe,over,danger];
donutChart.update();

let p=document.getElementById("popup");
if(d.state=="DANGER"){
p.style.display="block";p.style.background="#f44336";
p.innerHTML="🚨 STAMPEDE ALERT";
}
else if(d.state=="OVER"){
p.style.display="block";p.style.background="#ff9800";p.style.color="#000";
p.innerHTML="⚠ OVERCROWDED";
}
else p.style.display="none";
});
},1000);
</script>
</body>
</html>
)rawliteral";

/* ================= SETUP ================= */
void setup() {
  Serial.begin(115200);
  Wire.begin(21,22);

  pinMode(BUZZER_PIN,OUTPUT);
  digitalWrite(BUZZER_PIN,LOW);

  display.begin(SSD1306_SWITCHCAPVCC,0x3C);
  display.setTextColor(SSD1306_WHITE);

  if (mpu.begin()) mpuOK = true;

  if (bmp.begin()) {
    bmpOK = true;
    basePressure = bmp.readPressure() / 100.0;
  }

  writeReg(ENABLE,0x00); delay(10);
  writeReg(ENABLE,0b01000101);
  writeReg(0xA2,0x40);
  writeReg(0xA3,0x40);
  writeReg(0xA4,0x02);
  writeReg(0xAB,0x01);
  writeReg(0x8E,0x01);

  WiFi.begin(ssid,password);
  while(WiFi.status()!=WL_CONNECTED) delay(500);
  Serial.println(WiFi.localIP());

  server.on("/",[](){ server.send_P(200,"text/html",DASHBOARD); });
  server.on("/setcap",[](){ maxCapacity = server.arg("c").toInt(); server.send(200,"text/plain","OK"); });
  server.on("/data",[](){
    String state="SAFE";
    if(stampede) state="DANGER";
    else if(peopleCount>=maxCapacity) state="OVER";
    server.send(200,"application/json",
      "{\"count\":"+String(peopleCount)+",\"state\":\""+state+"\"}");
  });
  server.begin();
}

/* ================= LOOP ================= */
void loop() {
  server.handleClient();
  handleGesture();
  handleStampede();
  handleBuzzer();
  updateOLED();
  delay(80);
}

/* ================= GESTURE ================= */
void handleGesture(){
  if (millis()-lastEvent<1200) return;
  if (millis()-lastDirectionTime<DIR_COOLDOWN) return;
  if (!(readReg(GSTATUS)&0x01)) return;

  int d = readReg(GFIFO_R)-readReg(GFIFO_L);
  if(d>ENTRY_TH){ peopleCount++; lastEvent=millis(); lastDirectionTime=millis(); }
  else if(d<EXIT_TH && peopleCount>0){ peopleCount--; lastEvent=millis(); lastDirectionTime=millis(); }
}

/* ================= STAMPEDE ================= */
void handleStampede(){
  bool vibRisk=false, pressureRisk=false;

  if(mpuOK){
    sensors_event_t a,g,t;
    mpu.getEvent(&a,&g,&t);
    float vib = abs(sqrt(a.acceleration.x*a.acceleration.x +
                         a.acceleration.y*a.acceleration.y +
                         a.acceleration.z*a.acceleration.z) - 9.8);
    if(vib>VIB_THRESHOLD){
      if(vibWindowStart==0){ vibWindowStart=millis(); vibHits=0; }
      vibHits++;
      if(vibHits>=VIB_HIT_LIMIT) vibRisk=true;
    }
  }

  if(bmpOK){
    float p=bmp.readPressure()/100.0;
    if(abs(p-basePressure)>PRESSURE_DELTA_TH) pressureRisk=true;
  }

  if((vibRisk||pressureRisk)&&!stampede){
    stampede=true;
    stampedeTime=millis();
    vibWindowStart=0;
    vibHits=0;
  }

  if(stampede && millis()-stampedeTime>STAMPEDE_HOLD_MS)
    stampede=false;
}

/* ================= BUZZER ================= */
void handleBuzzer(){
  static unsigned long lastToggle=0;
  static bool state=false;

  if(stampede){ digitalWrite(BUZZER_PIN,HIGH); return; }

  if(peopleCount>=maxCapacity){
    unsigned long now=millis();
    if(state && now-lastToggle>OVER_BEEP_ON){
      state=false; lastToggle=now; digitalWrite(BUZZER_PIN,LOW);
    }else if(!state && now-lastToggle>OVER_BEEP_OFF){
      state=true; lastToggle=now; digitalWrite(BUZZER_PIN,HIGH);
    }
    return;
  }

  digitalWrite(BUZZER_PIN,LOW);
  state=false;
}

/* ================= OLED ================= */
void updateOLED(){
  display.clearDisplay();
  display.setCursor(0,0);
  display.setTextSize(1);
  display.println("SMARTPASS");

  display.setTextSize(2);
  if(stampede) display.println("DANGER");
  else if(peopleCount>=maxCapacity) display.println("OVER");
  else display.println("SAFE");

  display.setTextSize(1);
  display.print("COUNT: ");
  display.println(peopleCount);
  display.display();
}
