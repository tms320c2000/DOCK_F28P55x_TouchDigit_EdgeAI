// test_touch.js
//
// CCS Theia (CCS v20, v21) Scripting Console 판정 시험 스크립트 (9_TouchDigitClassify_NPU용)
//
// 하는 일:
//   1. 부팅 자체 시험(selfTestPass)과 NPU 사용 여부(modelIsNpu)를 확인
//   2. "숫자 N을 쓰세요"를 보여 주고 targetLabel = N으로 둔 뒤, 보드가 글자 하나를 판정할 때(charCount 증가)까지 기다림
//   3. 판정 숫자, 1등-2등 점수 차(margin), 추론 시간을 바로 보여 줌. 숫자가 아니라고 거른 글자는 "판정 안 함"과 이유를 보여 줌
//   4. 끝나면 숫자별 정답률과 혼동 행렬(정답 x 판정)을 보여 주고 results/test_rNN.csv로 저장
//
// 실행: Scripting Console에서  loadJSFile("<이 파일 경로>/test_touch.js")

var session = ds.openSession();
var fs = require('fs');
var path = require('path');
var os = require('os');

// ------------------------------------------------------------------ 설정
var PROJECT_NAME = "9_TouchDigitClassify_NPU";
var DIGITS = [0, 1, 2, 3, 4, 5, 6, 7, 8, 9];
var REPEAT = 3;                        // 숫자마다 3번 (30글자)
var WAIT_SEC = 30;
// ------------------------------------------------------------------

function waitMs(ms) {
    if (typeof sleep === "function") { sleep(ms); }
    else { var t = Date.now(); while (Date.now() - t < ms) {} }
}
function ev(expr) { return Number(session.expression.evaluate(expr)); }
function pad(n, w) { var s = String(n); while (s.length < w) { s = " " + s; } return s; }
function shuffled(a) {
    var b = a.slice();
    for (var i = b.length - 1; i > 0; i--) { var j = Math.floor(Math.random() * (i + 1)); var t = b[i]; b[i] = b[j]; b[j] = t; }
    return b;
}
function findProjectDir() {
    var home = os.homedir(), best = null, bestTime = 0, entries = [];
    try { entries = fs.readdirSync(home); } catch (e) { entries = []; }
    for (var i = 0; i < entries.length; i++) {
        if (entries[i].toLowerCase().indexOf("workspace") !== 0) { continue; }
        var dir = path.join(home, entries[i], PROJECT_NAME);
        try {
            var t = fs.statSync(path.join(dir, ".project")).mtimeMs;
            if (t > bestTime) { best = dir; bestTime = t; }
        } catch (e) { }
    }
    return best !== null ? best : (typeof __dirname !== "undefined" ? __dirname : ".");
}

try { if (session.target.isHalted()) { session.target.run(false); } } catch (e) { }
try { ev("predDigit"); }
catch (e) { throw new Error("predDigit 변수가 없습니다. 9_TouchDigitClassify_NPU 펌웨어를 로드했는지 확인하세요."); }

console.log("=========================================================");
console.log(" selfTestPass " + ev("selfTestPass") + " (1이어야 정상) | modelIsNpu " + ev("modelIsNpu") + " | npuTimeouts " + ev("npuTimeouts"));
if (ev("selfTestPass") !== 1) {
    console.log(" 주의: model/ 4개 파일이 한 벌이 아닐 수 있습니다 (mod.a, tvmgen_default.h, test_vector.c를 같은 학습 결과로).");
}
session.expression.evaluate("triggerMode = 0");
session.expression.evaluate("resetStats = 1");
waitMs(300);

var plan = [];
for (var r = 0; r < REPEAT; r++) { plan = plan.concat(shuffled(DIGITS)); }
var conf = [], rows = ["no,target,pred,correct,reject_reason,raw_digit,margin,char_keys,char_ms,infer_us"];
var REASON = ["", "지나간 키가 적음", "한 번에 눌린 키가 많음", "점수 차가 작음"];
var rejected = [0, 0, 0, 0, 0, 0, 0, 0, 0, 0];
for (var a = 0; a < 10; a++) { conf.push([0, 0, 0, 0, 0, 0, 0, 0, 0, 0]); }
var ok = 0;

for (var q = 0; q < plan.length; q++) {
    var digit = plan[q];
    session.expression.evaluate("targetLabel = " + digit);
    var before = ev("charCount");
    console.log("\n>>> [" + (q + 1) + "/" + plan.length + "]  숫자 " + digit + " 을(를) 쓰세요");
    var waited = 0;
    while (ev("charCount") === before) {
        waitMs(100);
        waited += 100;
        if (waited >= WAIT_SEC * 1000) {
            console.log("    (기다리는 중) dvCount " + ev("dvCount") + " | 짧아서 버린 글자 " + ev("shortRejects"));
            waited = 0;
        }
    }
    var pred = ev("predDigit"), m = ev("margin"), ms = ev("charMs"), us = ev("inferUs");
    var why = ev("rejectReason"), raw = ev("rawDigit"), nk = ev("charKeys");
    var hit = (pred === digit);
    if (hit) { ok++; }
    if (pred >= 0) { conf[digit][pred]++; } else { rejected[digit]++; }
    rows.push((q + 1) + "," + digit + "," + pred + "," + (hit ? 1 : 0) + "," + why + "," + raw + "," + m + "," + nk + "," + ms + "," + us.toFixed(1));
    if (pred < 0) {
        console.log("    판정 안 함 (" + REASON[why] + ") | 모델이 고른 숫자 " + raw + " | 점수 차 " + m + " | 지나간 키 " + nk +
                    "  (누적 " + ok + "/" + (q + 1) + ")");
        continue;
    }
    console.log("    판정 " + pred + (hit ? "  ✔ 맞음" : "  ✘ 틀림") + " | 점수 차 " + m + " | 글자 " + ms + "ms | 추론 " + us.toFixed(1) + "us" +
                "  (누적 " + ok + "/" + (q + 1) + ")");
}
session.expression.evaluate("targetLabel = -1");

console.log("\n=========================================================");
console.log(" [완료] 정답률 " + (100 * ok / plan.length).toFixed(1) + "% (" + ok + "/" + plan.length + ")");
console.log(" 혼동 행렬 (행 = 쓴 숫자, 열 = 판정)");
console.log("       0  1  2  3  4  5  6  7  8  9  안함");
for (var t = 0; t < 10; t++) {
    var line = "   " + t + " ";
    for (var p = 0; p < 10; p++) { line += pad(conf[t][p] === 0 ? "." : conf[t][p], 3); }
    line += pad(rejected[t] === 0 ? "." : rejected[t], 5);
    console.log(line);
}
var outDir = path.join(findProjectDir(), "results");
fs.mkdirSync(outDir, { recursive: true });
var n = 1;
while (fs.existsSync(path.join(outDir, "test_r" + (n < 10 ? "0" : "") + n + ".csv"))) { n++; }
var outFile = path.join(outDir, "test_r" + (n < 10 ? "0" : "") + n + ".csv");
fs.writeFileSync(outFile, rows.join("\n") + "\n");
console.log(" 기록: " + outFile);
console.log("=========================================================");
