# 9_TouchDigitClassify_NPU — TTP229 터치 키패드에 쓴 숫자를 NPU로 판정

`8_TouchDigitLogger`와 같은 방법으로 글자 하나를 잘라 특징 32칸 x 3열(x, y, pen)을 만들고, Edge AI Studio로 학습한 분류 모델을 **하드웨어 NPU**로 실행해 숫자 0~9를 판정합니다.

## 개요
- 배선, 모듈 점퍼, 글자 자르기, 특징 계산은 8_와 같습니다. 자세한 내용은 [`8_TouchDigitLogger/README.md`](../8_TouchDigitLogger/README.md)를 보세요.
  - 배선: 모듈 SCL ← 23번 핀(GPIO8), SDO → 25번 핀(GPIO4), VCC 3.3V, GND. DOCK 보드는 우측 **CN4200** 실크 23·25번, EVM V2는 B Side 23·25번입니다(두 보드 번호가 같음). 그림은 8_ README의 "배선"을 봅니다.
  - 점퍼: TP2, TP3, TP4, TP5. 위치(추정)와 확인 방법은 8_ README의 "터치패드 점퍼 설정"을 봅니다. 키 실크 번호는 왼쪽 위부터 가로로 1~16입니다.
- 모델 입력은 `(1, 3, 32, 1) int8`이고, 배열 순서는 `[x 32개][y 32개][pen 32개]`입니다(Studio `test_vector.c`와 같음).
- 출력은 숫자별 점수 10개이며, 순서는 학습 폴더 `D0`~`D9`의 알파벳 순서(= 0~9)입니다.

## 들어 있는 모델 (2026-11-23, 실제 손글씨 300글자)
- 8_로 DOCK 보드에서 직접 모은 **숫자당 30글자, 총 300글자**로 Edge AI Studio 1.8.0에서 학습한 모델입니다.
- 모델: CLS_2k_NPU, Simple Window, Variables 3, Frame 32, Epochs 60, LR 0.04, **Batch 16**, High Speed(int8 QAT)
- 결과
  - `TVMGEN_DEFAULT_TI_NPU`(하드웨어 NPU)
  - Studio 시험 데이터(30글자) 정확도 100%
  - 보드 채점(`test_touch.js`, 30글자): 30/30 정답, 최소 margin 15, `inferUs` 300.2 (`results/test_r01.csv`)
- 배치 크기 비교(같은 300글자): Batch 8·16·32 모두 시험 100%였고 16을 골랐습니다. 50글자처럼 데이터가 적을 때 Batch 32는 정확도 10%로 실패했습니다(8_ README "Batch Size 주의").
- 처음 들어 있던 합성 데이터 모델(2026-11-19, `8_TouchDigitLogger/tools/make_synthetic_dataset.py`, 합성 시험 99.17%)은 연결 확인용이었습니다.
- 이 모델을 학습한 300글자 데이터셋 자체는 저장소에 넣지 않았습니다. 같은 방법으로 직접 수집해 학습하세요(8_ README 8·9절).

## 보는 변수 (`watch_var.json`)
| 변수 | 정상 / 뜻 |
|---|---|
| `selfTestPass` · `modelIsNpu` | 1 · 1 |
| `npuTimeouts` | 0 |
| `keysNow` / `keyNoNow` | 지금 눌린 키 (`triggerMode = 1`일 때 늘 갱신) |
| `dvCount` | 키를 만질 때마다 늘어야 함 (DV 신호) |
| `predDigit` | **판정 숫자**. 숫자가 아니라고 보면 **-1** (아래 "판정 안 함") |
| `rejectReason` / `rawDigit` | 판정 안 한 이유(0 판정함, 1 지나간 키가 적음, 2 한 번에 눌린 키가 많음, 3 점수 차가 작음) / 모델이 고른 숫자 |
| `scores` / `margin` | 숫자별 점수 / 1등과 2등의 점수 차. 작으면 헷갈린 글자. 이유 1·2면 모델을 실행하지 않아 0 |
| `charKeys` / `charMaxKeys` | 글자가 지나간 키 수 / 한 번에 눌린 키 수의 최댓값 |
| `minKeys` / `maxKeysFrame` / `minMargin` | 거르는 기준 4 / 6 / 10. Expressions 창에서 바꿀 수 있음 |
| `charMs` / `inferUs` | 글자 쓰는 데 걸린 시간 / 추론 시간(us) |
| `targetLabel` | 지금 쓸 숫자(0~9)를 넣으면 채점. -1이면 채점 안 함 |
| `accPct` / `tested` / `confusion[정답][판정]` | 채점 결과. `resetStats = 1`로 지움 |

## 판정 안 함 (버전 1.20, 2026-11-23)
- 분류 모델은 숫자 10개 중 하나를 **반드시** 고릅니다. 그래서 키 하나만 눌러도 어떤 숫자의 점수가 크게 나옵니다.
- 점수를 softmax로 바꾼 "확률"도 기준이 되지 못합니다. 같은 모델로 PC에서 시험하니 키 하나 누름도 98~100%가 나왔습니다.
- 그래서 세 가지를 보고, 하나라도 걸리면 `predDigit = -1`로 둡니다.

| 순서 | 검사 | 기준 | 걸리는 입력 |
|---|---|---|---|
| 1 | 지나간 키 수 `charKeys` | 4개 미만 | 점 찍기, 두세 칸 긋기 (모델 실행 안 함) |
| 2 | 한 번에 눌린 키 수 `charMaxKeys` | 6개 초과 | 손바닥, 여러 손가락 (모델 실행 안 함) |
| 3 | 1등-2등 점수 차 `margin` | 10 미만 | 모델이 헷갈린 글자 (`rawDigit`에 모델이 고른 숫자는 남김) |

- **기준을 정한 근거**
  - 수집한 300글자: 지나간 키 7~16개, 한 번에 4~6개, margin 18 이상 → 모두 통과
  - 보드 시험 30글자(`results/test_r01.csv`): margin 15 이상 → 모두 통과
  - PC 모의 입력: 키 1~2개 누름, 두 칸 긋기, 세 칸 무작위 이동은 모두 거름. 여섯 칸 무작위 이동은 약 60%가 통과합니다(숫자처럼 보이는 모양도 있음).
- 채점 중(`targetLabel` 0~9)에 판정 안 한 글자는 틀린 것으로 셉니다(`testedRejects`). 시험 스크립트에서는 혼동 행렬 "안함" 열에 표시합니다.
- 숫자를 작게 쓰는 사람이 자주 거부되면 `minKeys`를 3으로 낮춥니다.

## 사용 순서
1. CCS v21에서 **File > Import > CCS Projects**로 이 폴더의 `CCS` 폴더를 Import합니다. DOCK 보드는 `targetConfigs/TMS320F28P559SJ9-Q1_DOCK_XDS100v2.ccxml`을 Active로 정합니다. `CPU1_RAM`으로 빌드합니다.
2. Debug(F11) → Resume(F8). Expressions 창에서 `watch_var.json`을 Import합니다. `selfTestPass = 1`, `modelIsNpu = 1`을 확인합니다.
3. 키패드에 숫자를 쓰고 손을 뗀 뒤 0.6초 기다리면 `predDigit`이 바뀝니다.
4. 시험 스크립트(**View > Scripting Console**). 경로는 가져온 프로젝트 폴더에 맞게 바꿉니다.
   ```javascript
   loadJSFile("<프로젝트 폴더>/test_touch.js")
   ```
   - 숫자 0~9를 섞어 3바퀴(30글자) 안내합니다. 글자마다 판정과 점수 차를 보여 줍니다.
   - 끝나면 정답률과 혼동 행렬을 보여 주고, `results/test_rNN.csv`로 저장합니다.

## 모델 바꾸기
- Studio(설정은 8_ README 9절)로 학습한 결과 4개 파일을 `model/`에 덮어씁니다.
  - 파일: `mod.a`, `tvmgen_default.h`, `test_vector.c`, `user_input_config.h`
  - Studio 결과의 `artifacts/`와 `golden_vectors/`에 있습니다.
- 조건
  - Variables 3, Frame Size 32, Quantization High Speed. 다르면 빌드 오류로 알려 줍니다(`FE_VARIABLES`, `FE_FRAME_SIZE`, `OUTPUT_INT` 검사).
  - 데이터 열 순서는 `x, y, pen`이어야 합니다(8_의 `collect_touch.js`가 그렇게 저장).
  - 클래스 폴더 이름은 `D0`~`D9`여야 `predDigit` = 숫자가 됩니다.
- 헤더에 `TVMGEN_DEFAULT_TI_NPU_SOFT`가 있으면 CPU로 실행되는 모델입니다(`modelIsNpu = 0`). 분류는 보통 하드웨어로 나옵니다.

## 문제 해결
| 증상 | 확인 |
|---|---|
| `selfTestPass` = 0 | `model/` 4개 파일이 한 학습 결과에서 나온 것인지 |
| 빌드 오류 `FE_VARIABLES` 등 | Studio 설정이 Variables 3, Frame 32, High Speed인지 |
| `keyNoNow`가 늘 0, `dvCount`가 안 늘어남 | 배선·점퍼 문제입니다. 8_ README 12절을 봅니다 |

## 확인 상태 (2026-11-24)
- **확인함**
  - CGT 25.11.1 명령줄로 `CPU1_RAM`, `CPU1_FLASH` 빌드·링크 경고 없음.
  - NPU 함수(`tvmgen_default_ti_npu_*`) 링크됨. 모델 버퍼 `.bss.noinit.tvm` 0x460워드(RAMGS3).
  - **보드 (2026-11-23, DOCK 보드 + F28P559SJ9-Q1 + TTP229):** 실제 모듈 연결, 300글자 모델로 `test_touch.js` 30/30, `inferUs` 300.2.
  - v1.20 "판정 안 함": gmake 빌드 통과, 판정 기준을 PC 모의 입력과 수집 데이터 300글자로 확인.
- **확인 전**
  - v1.20 "판정 안 함"을 보드에서 실행(키 하나 누름 → `predDigit = -1`). 위 30/30 채점은 v1.20 이전 펌웨어 결과입니다.
