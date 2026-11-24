// collect_touch.js
//
// CCS Theia (CCS v20, v21) Scripting Console 자동 수집 스크립트 (8_TouchDigitLogger용)
//
// 하는 일:
//   1. 펌웨어가 동작 중인지 확인 (멈춰 있으면 Resume)
//   2. "숫자 N을 쓰세요"를 콘솔에 보여 주고, 펌웨어가 글자 하나를 끝낼 때(sampleReady = 1)까지 기다림
//   3. 특징 feat[3][32]를 읽어 Edge AI Studio 분류용 CSV로 저장 (글자 하나 = 파일 하나, 32줄, 열 x,y,pen)
//        - dataset/classes/D0 ... D9/D<n>_r<실행>_<번호>.csv
//        - logs/touch_rNN.csv : 전체 기록 (번호, 숫자, 걸린 시간, 프레임 수, 16비트 키 프레임들)
//   4. 4x4 칸에 손가락이 지나간 순서를 콘솔에 그려 보여 줌 (잘못 썼으면 출력된 파일을 지우면 됨)
//
// 순서: 0,1,2,...,9를 한 바퀴로 REPEAT바퀴 (한 숫자만 몰아 쓰지 않도록 섞어서)
// 여러 번 실행하면 dataset/classes/ 안에 파일이 계속 쌓입니다 (실행 번호 r01, r02 ...). 여러 사람이 쓰면 더 좋습니다.
// 실행: Scripting Console에서  loadJSFile("<이 파일 경로>/collect_touch.js")

var session = ds.openSession();
var fs = require('fs');
var path = require('path');
var os = require('os');

// ------------------------------------------------------------------ 설정
var PROJECT_NAME = "8_TouchDigitLogger";
var OUTPUT_DIR_OVERRIDE = "";          // 저장 폴더를 직접 정하려면 적으세요 (비워 두면 Import한 프로젝트 폴더)
var DIGITS = [0, 1, 2, 3, 4, 5, 6, 7, 8, 9];   // 한 바퀴에 쓸 숫자
var REPEAT = 5;                        // 바퀴 수 (기본 5바퀴 = 숫자마다 5개, 50글자, 약 5분)
var SHUFFLE = true;                    // true면 바퀴마다 순서를 섞음
var WAIT_SEC = 30;                     // 한 글자를 이 시간 안에 안 쓰면 다시 안내
var N_STEPS = 32;                      // 펌웨어 N_STEPS와 같아야 함
var SAVE_RAW = true;                   // logs/에 키 프레임 원본도 저장 (특징 계산을 나중에 바꿀 때 필요)
// ------------------------------------------------------------------

function waitMs(ms) {
    if (typeof sleep === "function") { sleep(ms); }
    else { var t = Date.now(); while (Date.now() - t < ms) {} }
}
function ev(expr) { return Number(session.expression.evaluate(expr)); }
function pad(n, w) { var s = String(n); while (s.length < w) { s = "0" + s; } return s; }

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
    if (best !== null) { return best; }
    if (typeof __dirname !== "undefined") { return __dirname; }
    return ".";
}
function nextRunNo(dir) {
    var n = 1;
    while (fs.existsSync(path.join(dir, "touch_r" + pad(n, 2) + ".csv"))) { n++; }
    return n;
}
function shuffled(a) {
    var b = a.slice();
    for (var i = b.length - 1; i > 0; i--) { var j = Math.floor(Math.random() * (i + 1)); var t = b[i]; b[i] = b[j]; b[j] = t; }
    return b;
}

// 4x4 칸에 처음 눌린 순서 (1~9, a~z). 키 번호 i -> 행 floor(i/4), 열 i%4 (펌웨어 keyRow/keyCol과 같은 가정)
function drawPath(frames) {
    var order = {}, step = 0, sym = "123456789abcdefghijklmnopqrstuvwxyz";
    for (var f = 0; f < frames.length; f++) {
        if (frames[f] === 0) { continue; }
        for (var i = 0; i < 16; i++) {
            if ((frames[f] & (1 << i)) && order[i] === undefined) { order[i] = step++; }
        }
    }
    var lines = [];
    for (var r = 0; r < 4; r++) {
        var row = "      ";
        for (var c = 0; c < 4; c++) {
            var k = r * 4 + c;
            row += (order[k] === undefined ? "." : sym.charAt(Math.min(order[k], sym.length - 1))) + " ";
        }
        lines.push(row);
    }
    return lines.join("\n");
}

// 1. 펌웨어 동작 확인
try {
    if (session.target.isHalted()) {
        console.log(" 타깃이 멈춰 있어 실행(Resume)합니다.");
        session.target.run(false);
    }
} catch (e) { console.log(" 타깃 상태를 확인하지 못했습니다: " + e); }
try { ev("sampleReady"); }
catch (e) { throw new Error("sampleReady 변수가 없습니다. 8_TouchDigitLogger 펌웨어를 로드했는지 확인하세요."); }

session.expression.evaluate("holdUntilRead = 1");
session.expression.evaluate("triggerMode = 0");
session.expression.evaluate("sampleReady = 0");

// 2. 저장 위치
var projectDir = (OUTPUT_DIR_OVERRIDE !== "") ? OUTPUT_DIR_OVERRIDE : findProjectDir();
var rawDir = path.join(projectDir, "logs");
var clsDir = path.join(projectDir, "dataset", "classes");
fs.mkdirSync(rawDir, { recursive: true });
for (var d = 0; d < DIGITS.length; d++) { fs.mkdirSync(path.join(clsDir, "D" + DIGITS[d]), { recursive: true }); }
var runNo = nextRunNo(rawDir);
var rawFile = path.join(rawDir, "touch_r" + pad(runNo, 2) + ".csv");
fs.writeFileSync(rawFile, "no,digit,file,char_ms,raw_len,touch_frames,frames_hex\n");

var plan = [];
for (var r = 0; r < REPEAT; r++) { plan = plan.concat(SHUFFLE ? shuffled(DIGITS) : DIGITS); }

console.log("=========================================================");
console.log(" [F28P55x + TTP229] 터치 숫자 수집 시작: " + plan.length + "글자 (숫자마다 " + REPEAT + "개)");
console.log(" 분류 파일: " + clsDir);
console.log(" 쓰는 법: 키패드 전체를 써서 크게, 한 글자를 다 쓰면 손을 떼고 0.6초 기다리면 끝으로 봅니다.");
console.log("          4, 5, 7처럼 손을 떼는 획은 0.6초 안에 이어 쓰세요.");
console.log("=========================================================");

// 3. 글자마다 받기
var saved = 0, totalMs = 0;
for (var q = 0; q < plan.length; q++) {
    var digit = plan[q];
    session.expression.evaluate("targetLabel = " + digit);
    session.expression.evaluate("sampleReady = 0");
    var before = ev("charCount");
    console.log("\n>>> [" + (q + 1) + "/" + plan.length + "]  숫자 " + digit + " 을(를) 쓰세요");

    var waited = 0;
    while (ev("sampleReady") === 0) {
        waitMs(100);
        waited += 100;
        if (waited >= WAIT_SEC * 1000) {
            console.log("    (기다리는 중) 숫자 " + digit + " | dvCount " + ev("dvCount") + " | keysNow 0x" +
                        ev("keysNow").toString(16) + " | 짧아서 버린 글자 " + ev("shortRejects"));
            waited = 0;
        }
    }

    // 특징 읽기
    var lines = ["x,y,pen"];
    for (var k = 0; k < N_STEPS; k++) {
        var x = ev("feat[" + k + "]"), y = ev("feat[" + (N_STEPS + k) + "]"), p = ev("feat[" + (2 * N_STEPS + k) + "]");
        lines.push(x.toFixed(4) + "," + y.toFixed(4) + "," + Math.round(p));
    }
    var rawLen = ev("rawLen"), charMs = ev("charMs"), touchFrames = ev("touchFrames");
    var frames = [];
    if (SAVE_RAW) {
        for (var f = 0; f < rawLen; f++) { frames.push(ev("rawFrames[" + f + "]")); }
    }
    session.expression.evaluate("sampleReady = 0");     // 다음 글자 받기 시작

    var name = "D" + digit + "_r" + pad(runNo, 2) + "_" + pad(q + 1, 3) + ".csv";
    fs.writeFileSync(path.join(clsDir, "D" + digit, name), lines.join("\n") + "\n");
    var hex = [];
    for (var h = 0; h < frames.length; h++) { hex.push(pad(frames[h].toString(16), 4)); }
    fs.appendFileSync(rawFile, (q + 1) + "," + digit + "," + name + "," + charMs + "," + rawLen + "," + touchFrames + "," + hex.join(" ") + "\n");
    saved++;
    totalMs += charMs;

    console.log("    저장: " + name + " | " + charMs + "ms, 프레임 " + rawLen + "(눌림 " + touchFrames + ")" +
                (ev("charCount") - before > 1 ? " | 주의: 글자가 2개 이상 들어옴" : ""));
    if (SAVE_RAW) { console.log(drawPath(frames)); }
}
session.expression.evaluate("targetLabel = -1");

// 4. 요약
console.log("\n=========================================================");
console.log(" [완료] " + saved + "글자 저장, 평균 " + (saved ? Math.round(totalMs / saved) : 0) + "ms/글자");
console.log(" 짧아서 버린 글자 " + ev("shortRejects") + ", 놓친 DV " + ev("missedChars") + ", 잘못된 트리거 " + ev("falseTriggers"));
console.log(" 전체 기록: " + rawFile);
console.log(" dataset/classes/ 를 zip으로 묶어 Edge AI Studio 분류 프로젝트에 올리세요 (Variables 3, Frame Size 32).");
console.log(" 잘못 쓴 글자는 위에 출력된 파일 이름을 찾아 지우면 됩니다.");
console.log("=========================================================");
