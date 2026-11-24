// 파일이름	:	9_TouchDigitClassify_NPU.c
// 대상장치	:	TMS320F28P55x(F28P550SJ9 / F28P559SJ9-Q1) 모듈 - NPU 내장, TMS320F28X EVM V2 또는 초소형 모듈 DOCK 보드(전용 어댑터)
//			+ TTP229 4x4 정전식 터치 키패드 모듈 (TTP229-BSF, 아두이노용 16키 모듈)
// 파일버전	:	1.20
// 갱신이력	:	2026-11-19, 버전 1.00 - 8_TouchDigitLogger로 모은 데이터로 학습한 분류 모델(Simple Window, Variables 3, Frame 32)을
//			NPU로 실행해 터치 키패드에 손가락으로 쓴 숫자(0~9)를 글자마다 판정
//			:	2026-11-22, 버전 1.10 - SDO 입력을 47번 핀(GPIO9)에서 25번 핀(GPIO4)으로 옮김 (8_TouchDigitLogger 1.10과 같은 배선)
//			:	2026-11-23, 버전 1.20 - 숫자가 아닌 입력(키 하나 누름, 손바닥, 헷갈린 글자)은 판정하지 않음 (predDigit = -1, rejectReason)
//			minKeys(지나간 키 수), maxKeysFrame(한 번에 눌린 키 수), minMargin(1등-2등 점수 차)로 거름. 수집 데이터 300글자 기준으로 정함
// 예제설명	:

//************************************************************************************************************************************************************************
//
// 본 예제는 8_TouchDigitLogger와 같은 방법으로 TTP229 터치 키패드에 쓴 글자 하나를 잘라 특징 32 x 3(x, y, pen)으로 만들고,
// Edge AI Studio(Generic Timeseries Classification)로 학습한 모델을 NPU로 실행해 어떤 숫자인지 판정합니다.
// 배선, 모듈 점퍼 설정, 글자 자르기, 특징 계산은 8_TouchDigitLogger.c와 같습니다 (자세한 설명은 그 파일 머리말).
//
// a. 배선 (TTP229 모듈 12핀 헤더. 실크 인쇄 왼쪽부터 VCC GND SCL SDO OUT1~OUT8, 사진 ref/ttp229/1327405_1.jpg)
//
//		DOCK 보드 우측 핀-헤더(CN420x/CN430x) 번호 = EVM V2 B Side 번호 (F28P559SJ9-Q1 어댑터 탑재 시)
//		모듈 VCC (P3 12번) ◀── 보드 3.3V   (5V 금지)   DOCK : 범용 기판 위 전원 핀-헤더의 '3.3V' 줄
//		모듈 GND (P3 11번) ◀── 보드 GND               DOCK : 우측 CN4200 3번 핀 (또는 전원 핀-헤더 'GND' 줄)
//		모듈 SCL (P3 10번) ◀── 23번 핀 (GPIO8, 출력)  DOCK : 우측 CN4200 실크 23 / EVM V2 : B Side 23
//		모듈 SDO (P3  9번) ──▶ 25번 핀 (GPIO4, 입력, 외부 인터럽트 XINT1)  DOCK : 우측 CN4200 실크 25 / EVM V2 : B Side 25
//		모듈 점퍼: TP2(16키), TP3 + TP4(멀티키), TP5(빠른 깨어남, 권장) 연결. TP0, TP1은 열어 둠
//		           (P2(위) 왼쪽부터 TP7 TP6 TP5 TP4, P1(아래) 왼쪽부터 TP3 TP2 TP1 TP0 - 회로도 기준 추정, 8_ 머리말의 확인 방법 참고)
//
// b. 모델이 하는 일
//
//		입력 : 글자 하나를 32칸으로 고르게 뽑은 값 3가지 -> Tensor (1, 3, 32, 1) int8
//		         0번 채널 x   = 눌린 키들의 무게중심 열(0~3)
//		         1번 채널 y   = 눌린 키들의 무게중심 행(0~3)
//		         2번 채널 pen = 눌림 1 / 뗌 0
//		       배열 순서는 [x 32개][y 32개][pen 32개] 입니다 (Studio test_vector.c와 같음).
//		       float 값을 헤더의 채널별 bias/scale/shift로 int8로 바꿔 넣습니다.
//		출력 : 숫자별 점수 10개 (int8) -> 가장 큰 것이 판정 숫자 (학습 폴더 D0~D9의 알파벳 순서 = 0~9)
//
// b-1. 숫자가 아닌 입력 거르기 (판정 안 함 = predDigit -1)
//
//		분류 모델은 숫자 10개 중 하나를 '반드시' 고르도록 학습되어, 키 하나만 눌러도 어느 숫자의 점수가 크게 나옵니다.
//		점수를 softmax로 바꾼 '확률'도 키 하나 누름에서 98~100%가 나와 기준이 되지 못합니다 (PC에서 같은 모델로 확인).
//		그래서 모델 앞뒤에서 다음 세 가지를 봅니다. 기준값은 Expressions 창에서 바꿀 수 있습니다.
//		  1) 지나간 키 수(charKeys) < minKeys(4)          -> rejectReason 1 (점 찍기, 짧은 긋기. 모델 실행 안 함)
//		  2) 한 번에 눌린 키 수(charMaxKeys) > maxKeysFrame(6) -> rejectReason 2 (손바닥, 여러 손가락. 모델 실행 안 함)
//		  3) 1등-2등 점수 차(margin) < minMargin(10)       -> rejectReason 3 (모델이 헷갈림. rawDigit에 모델이 고른 숫자는 남김)
//		수집 데이터 300글자(PC): 지나간 키 7~16개, 한 번에 4~6개, margin 18 이상. 보드 시험 30글자(test_r01): margin 15 이상 -> 모두 통과
//		키 1~2개 누름, 두 칸 긋기, 세 칸 무작위 이동 -> 모두 거름. 여섯 칸 무작위 이동은 약 60%가 통과 (숫자와 비슷한 모양도 있음)
//
// c. 채점 (선택)
//
//		Expressions 창에서 targetLabel에 지금 쓸 숫자(0~9)를 넣고 쓰면, 판정이 맞았는지 세어 accPct와 confusion[정답][판정]에 쌓습니다.
//		targetLabel = -1이면 채점하지 않습니다. resetStats = 1로 바꾸면 통계를 지웁니다.
//
// d. 부팅 자체 시험(selfTestPass): model/test_vector.c의 입력을 모델에 넣어 Studio 정답(golden_output)과 ±1 안에서 같은지 확인합니다.
//    1이면 mod.a, tvmgen_default.h, test_vector.c가 한 벌로 맞게 연결된 것입니다.
//
// e. 들어 있는 model/은 합성 데이터(tools/make_synthetic_dataset.py)로 학습한 임시 모델입니다. 연결 확인용이며,
//    실제 손글씨 정확도는 8_TouchDigitLogger로 직접 모은 데이터로 다시 학습해 model/ 4개 파일을 바꿔야 나옵니다.
//
// CCS Expressions 창에 등록할 변수 (watch_var.json으로 한 번에 등록):
// --> selfTestPass(1), modelIsNpu(1), npuTimeouts(0), triggerMode, capState, keysNow, keyNoNow
// --> predDigit(판정 숫자, -1 = 판정 안 함), rejectReason, rawDigit, scores, margin, charKeys, charMaxKeys, charMs, inferUs, charCount, rejectCount
// --> minKeys, maxKeysFrame, minMargin (거르는 기준)
// --> targetLabel, predCorrect, accPct, tested, testedRejects, confusion, resetStats
//
// 본 예제는 TI가 제공하는 Driver API Library(DriverLib)만으로 제작되었습니다 (SysConfig, 특징 추출 라이브러리 미사용).
//
//************************************************************************************************************************************************************************


// 헤더 파일들
#include <stdint.h>
#include <math.h>

#include "driverlib.h"			// TI 제공 Driver API Library 헤더파일 (driverlib)
#include "device.h"
#include "tvmgen_default.h"		// 모델 입출력 구조체, 실행 함수, 입력 정규화 값 (model/, Studio 결과 artifacts/)
#include "user_input_config.h"	// FE_FRAME_SIZE(32), FE_VARIABLES(3), FE_NN_OUT_SIZE(10) (model/, Studio 결과 golden_vectors/)

// 전처리 구문 정의 (터치 입력: 8_TouchDigitLogger와 같음)
#define	PIN_TTP_SCL			8U			// SCL 출력 : 23번 핀 (GPIO8, DOCK CN4200 / EVM V2 B Side)  -> 모듈 SCL
#define	PIN_TTP_SDO			4U			// SDO 입력 : 25번 핀 (GPIO4, DOCK CN4200 / EVM V2 B Side)  <- 모듈 SDO
#define	PINMUX_TTP_SCL		GPIO_8_GPIO8
#define	PINMUX_TTP_SDO		GPIO_4_GPIO4
#define	TTP_ACTIVE_LOW		1U			// 1 = TP1 개방(기본): SDO 0이 '눌림'
#define	TTP_HALF_US			4U			// SCL 반주기(us)
#define	NUM_KEYS			16U

#define	SAMPLE_MS			20U			// 키 읽기 주기(ms)
#define	END_GAP_MS			600U		// 이만큼 안 눌리면 글자 끝
#define	ARM_TIMEOUT_MS		300U		// DV 뒤 이 시간 안에 키가 안 보이면 잘못된 트리거로 보고 대기로
#define	MAX_FRAMES			200U		// 글자 하나 최대 프레임 (200 x 20ms = 4초)
#define	MIN_TOUCH_FRAMES	3U			// 눌린 프레임이 이보다 적으면 버림
#define	END_GAP_TICKS		(END_GAP_MS / SAMPLE_MS)
#define	ARM_TIMEOUT_TICKS	(ARM_TIMEOUT_MS / SAMPLE_MS)

#define	CAP_IDLE			0U
#define	CAP_ARMED			1U
#define	CAP_RUN				2U

// 모델
#define	N_STEPS				FE_FRAME_SIZE	// 모델 입력 길이 (Studio Frame Size 32)
#define	NUM_FEAT			3U				// x, y, pen
#define	NUM_CLASSES			FE_NN_OUT_SIZE	// 숫자 수 (10)
#define	CYC_PER_US			(DEVICE_SYSCLK_FREQ / 1000000UL)

#if defined(TVMGEN_DEFAULT_TI_NPU)
#define	USE_NPU_HW			1			// 하드웨어 NPU 모델
extern void NPU_enableInterrupt(void);	// device/f28p55x_npu.c (C2000Ware device_support)
#else
#define	USE_NPU_HW			0			// TVMGEN_DEFAULT_TI_NPU_SOFT(CPU 실행) 모델
#endif

#if (FE_VARIABLES != 3) || (FE_FRAME_SIZE != 32)
#error "이 예제는 입력 3개(x, y, pen) x 32칸 모델용입니다. Studio에서 Variables = 3, Frame Size = 32로 학습한 model/을 넣으세요."
#endif

#if !defined(OUTPUT_INT)
#error "이 예제는 int8 출력 분류 모델(user_input_config.h에 OUTPUT_INT)용입니다. Quantization을 High Speed로 학습하세요."
#endif

// CPU 타이머1: SYSCLK으로 내려 세는 32비트 자유 실행 카운터 (경과 = 시작값 - 현재값)
#define	NOW()	CPUTimer_getTimerCount(CPUTIMER1_BASE)

// 함수 원형 선언
static void		initTouchPins(void);
static void		initSampleTimer(void);
static void		initFreeRunTimer(void);
static uint16_t	ttpRead16(void);
static void		startCapture(void);
static void		finishCapture(void);
static void		armDvInterrupt(void);
static uint16_t	makeFeatures(const uint16_t *fr, uint16_t len, float *out);
static uint16_t	bitCount16(uint16_t v);
static void		rejectChar(uint16_t reason);
static void		updateLiveView(uint16_t keys);
static void		quantizeInput(const float *src);
static void		runModel(void);
static void		selfTest(void);
static void		classifyChar(void);
__interrupt void timer0ISR(void);
__interrupt void xint1ISR(void);


// 전역 변수 선언 (CCS Expressions/Watch 창에서 실시간으로 값을 확인할 수 있는 전역 변수)

// 터치 입력
volatile uint16_t	triggerMode = 0U;		// 0 = SDO DV 인터럽트로 시작(기본), 1 = DV 없이 20ms마다 늘 읽음
volatile uint16_t	capState = CAP_IDLE;	// 0 대기 / 1 DV 받음 / 2 기록 중
volatile uint16_t	keysNow = 0U;			// 마지막으로 읽은 16비트 키 상태 (비트 i = (i+1)번 키)
volatile uint16_t	keyNoNow = 0U;			// 눌린 키 중 가장 작은 번호 1~16 (0 = 없음)
volatile uint16_t	keyCountNow = 0U;
volatile uint32_t	dvCount = 0UL;
volatile uint32_t	falseTriggers = 0UL;
volatile uint32_t	shortRejects = 0UL;		// 너무 짧아 버린 글자 수
volatile uint32_t	busyDrops = 0UL;		// 앞 글자 판정이 안 끝나 버린 글자 수 (0이어야 정상)
volatile uint32_t	charCount = 0UL;		// 판정한 글자 수
volatile uint16_t	charMs = 0U;			// 마지막 글자 쓰는 데 걸린 시간(ms)
volatile uint16_t	rawFrames[MAX_FRAMES];	// 20ms마다 읽은 키 상태
volatile float		feat[NUM_FEAT * N_STEPS];	// 마지막 글자의 특징 [x 32][y 32][pen 32]

// 판정 결과
volatile uint16_t	selfTestPass = 0U;		// 1=Studio 정답(test_vector.c)과 일치
volatile uint16_t	modelIsNpu = USE_NPU_HW;	// 1=NPU 하드웨어가 추론
volatile int16_t	predDigit = -1;			// 판정 숫자 (-1 = 아직 없음 또는 판정 안 함. rejectReason 참고)
volatile int16_t	rawDigit = -1;			// 모델이 고른 숫자 (거름과 상관없이. 모델을 실행하지 않았으면 -1)
volatile uint16_t	rejectReason = 0U;		// 0 판정함 / 1 지나간 키가 적음 / 2 한 번에 눌린 키가 많음 / 3 점수 차가 작음
volatile uint32_t	rejectCount = 0UL;		// 판정하지 않은 글자 수
volatile int16_t	scores[NUM_CLASSES];	// 모델 출력 점수 (int8 값. 모델을 실행하지 않았으면 0)
volatile int16_t	margin = 0;				// 1등 점수 - 2등 점수 (작으면 헷갈린 글자)
volatile uint16_t	charKeys = 0U;			// 마지막 글자가 지나간 키 수 (서로 다른 키, 1~16)
volatile uint16_t	charMaxKeys = 0U;		// 마지막 글자에서 한 번에 눌린 키 수의 최댓값

// 거르는 기준 (Expressions 창에서 바꿀 수 있음. 수집 300글자: 키 7~16개, 한 번에 4~6개, margin 18 이상 / 보드 시험 30글자: margin 15 이상)
volatile uint16_t	minKeys = 4U;			// 지나간 키가 이보다 적으면 판정 안 함 (점 찍기, 짧은 긋기)
volatile uint16_t	maxKeysFrame = 6U;		// 한 번에 눌린 키가 이보다 많으면 판정 안 함 (손바닥)
volatile int16_t	minMargin = 10;			// 1등-2등 점수 차가 이보다 작으면 판정 안 함 (0이면 이 검사 끔)
volatile float		inferUs = 0.0f;			// 추론 시작부터 결과까지(us)
volatile uint16_t	npuTimeouts = 0U;		// NPU 완료 신호를 10ms 안에 못 받은 횟수 (0이어야 정상)

// 채점 (선택)
volatile int16_t	targetLabel = -1;		// 지금 쓰는 숫자 0~9 (Expressions 창에서 넣음, -1 = 채점 안 함)
volatile uint16_t	predCorrect = 0U;		// 1=마지막 판정이 targetLabel과 같음
volatile uint32_t	tested = 0UL, testedCorrect = 0UL;
volatile uint32_t	testedRejects = 0UL;	// 채점 중 판정하지 않은 글자 수 (tested에 포함, 틀린 것으로 셈)
volatile float		accPct = 0.0f;			// 채점한 글자의 정답률(%). 판정하지 않은 글자도 분모에 들어감
volatile uint32_t	confusion[NUM_CLASSES][NUM_CLASSES];	// [정답][판정] 횟수
volatile uint16_t	resetStats = 0U;		// 1로 바꾸면 채점 통계를 지움

// 키 번호(1~16) -> 위치. 실크 인쇄 왼쪽 위부터 가로로 1~16 (사진 1327405_1.jpg). 8_TouchDigitLogger, tools/touch_features.py와 같아야 함
static const uint16_t	keyCol[NUM_KEYS] = { 0U, 1U, 2U, 3U, 0U, 1U, 2U, 3U, 0U, 1U, 2U, 3U, 0U, 1U, 2U, 3U };
static const uint16_t	keyRow[NUM_KEYS] = { 0U, 0U, 0U, 0U, 1U, 1U, 1U, 1U, 2U, 2U, 2U, 2U, 3U, 3U, 3U, 3U };

static uint16_t		armTicks = 0U;
static uint16_t		gapTicks = 0U;
static uint16_t		capLen = 0U;
static volatile uint16_t	newChar = 0U;	// 1 = 특징이 준비되어 메인 루프가 판정할 차례

// 모델 입출력 버퍼 (C28x는 int8_t도 16비트 한 칸에 하나씩 둡니다. TI 예제와 같음)
static int8_t		modelInput[NUM_FEAT * N_STEPS];
static int8_t		modelOutput[NUM_CLASSES];

// Studio 정답 벡터 (model/test_vector.c, SET 0)
extern float		model_test_input[];
extern int8_t		golden_output[];


// 메인 함수
void main(void)
{
	uint16_t	i, j;

//	1. 시스템 초기화 - 왓치독 비-활성화, PLL 150MHz, NPU를 포함한 주변회로 클럭 공급
	Device_init();

//	2. GPIO 핀 락 해제, 내부 풀업 활성화
	Device_initGPIO();

//	3. 주변회로 인터럽트 확장회로 초기화, 벡터 테이블 초기화, 인터럽트 등록
	Interrupt_initModule();
	Interrupt_initVectorTable();
	Interrupt_register(INT_TIMER0, &timer0ISR);
	Interrupt_register(INT_XINT1, &xint1ISR);

//	4. NPU 완료 인터럽트 마스크 해제, 시간 측정용 CPU 타이머1 시작
#if USE_NPU_HW
	NPU_enableInterrupt();
#endif
	initFreeRunTimer();
#ifdef _FLASH
	Flash_enablePrefetch(FLASH0CTRL_BASE);	// Flash 실행 구성에서만
#endif

//	5. 부팅 자체 시험 (NPU는 끝나면 인터럽트로 알리므로 전역 인터럽트를 먼저 켭니다)
	EINT;
	selfTest();

//	6. TTP229 전원 안정·자동 보정 시간(약 0.5초) 기다림. 이때 키를 만지지 않습니다
	DEVICE_DELAY_US(600000UL);

//	7. TTP229 연결 핀(SCL 출력, SDO 입력 + XINT1), 20ms 타이머, 인터럽트 활성화
	initTouchPins();
	initSampleTimer();
	Interrupt_enable(INT_TIMER0);
	Interrupt_enable(INT_XINT1);
	armDvInterrupt();
	ERTM;
	CPUTimer_startTimer(CPUTIMER0_BASE);

//	8. Idle(Background) Loop - 글자가 끝나면 판정 (NPU 실행은 인터럽트가 아니라 여기서 합니다)
	for(;;)
	{
		if(newChar != 0U)
		{
			classifyChar();
			newChar = 0U;
		}
		if(resetStats != 0U)
		{
			resetStats = 0U;
			tested = 0UL;
			testedCorrect = 0UL;
			testedRejects = 0UL;
			accPct = 0.0f;
			for(i = 0U; i < NUM_CLASSES; i++)
			{
				for(j = 0U; j < NUM_CLASSES; j++)
				{
					confusion[i][j] = 0UL;
				}
			}
		}
	}
}

//	9. 함수들

//
// quantizeInput - float 입력 NUM_FEAT x N_STEPS개를 모델 입력 int8로 바꿉니다. 채널마다 bias/scale/shift가 다릅니다.
//   input_int = clip(floor((x + bias[c]) * scale[c]) >> shift[c], -128, 127)  (7_1_TempLoadClassify_NPU와 같은 식)
//
static void quantizeInput(const float *src)
{
	uint16_t	c, i, n;
	int32_t		q;

	for(c = 0U; c < NUM_FEAT; c++)
	{
		for(i = 0U; i < N_STEPS; i++)
		{
			n = (c * N_STEPS) + i;
			q = ((int32_t)floorf((src[n] + tvmgen_default_bias_data[c]) * tvmgen_default_scale_data[c])) >> tvmgen_default_shift_data[c];
			if(q > 127L)
			{
				q = 127L;
			}
			else if(q < -128L)
			{
				q = -128L;
			}
			modelInput[n] = (int8_t)q;
		}
	}
}

//
// runModel - modelInput[]으로 모델을 한 번 실행합니다 (결과는 modelOutput[]). 시간도 측정합니다.
//
static void runModel(void)
{
	struct tvmgen_default_inputs	inputs = { (void *)&modelInput[0] };
	struct tvmgen_default_outputs	outputs = { (void *)&modelOutput[0] };
	uint32_t	t0;

#if USE_NPU_HW
	tvmgen_default_finished = 0;
#endif
	t0 = NOW();
	tvmgen_default_run(&inputs, &outputs);
#if USE_NPU_HW
	while(!tvmgen_default_finished)
	{
		if((t0 - NOW()) > (10000UL * CYC_PER_US))	// 10ms 안에 안 끝나면 포기
		{
			npuTimeouts++;
			break;
		}
	}
#endif
	inferUs = (float)(t0 - NOW()) / (float)CYC_PER_US;
}

//
// selfTest - Studio 정답 벡터(SET 0)로 모델을 실행해 golden_output과 ±1 안에서 같은지 확인합니다.
//
static void selfTest(void)
{
	uint16_t	i;
	int16_t		err;
	uint16_t	errors = 0U;

	quantizeInput(model_test_input);
	runModel();
	for(i = 0U; i < NUM_CLASSES; i++)
	{
		err = (int16_t)modelOutput[i] - (int16_t)golden_output[i];
		if((err > 1) || (err < -1))
		{
			errors++;
		}
	}
	selfTestPass = (errors == 0U) ? 1U : 0U;
}

//
// bitCount16 - 16비트 값에서 1인 비트 수 (눌린 키 수)
//
static uint16_t bitCount16(uint16_t v)
{
	uint16_t	n = 0U;

	while(v != 0U)
	{
		v &= (uint16_t)(v - 1U);
		n++;
	}
	return n;
}

//
// rejectChar - 숫자가 아니라고 보고 판정하지 않습니다 (predDigit = -1). 채점 중이면 틀린 것으로 셉니다.
//
static void rejectChar(uint16_t reason)
{
	int16_t		truth = targetLabel;

	predDigit = -1;
	rejectReason = reason;
	rejectCount++;
	charCount++;
	if((truth >= 0) && (truth < (int16_t)NUM_CLASSES))
	{
		predCorrect = 0U;
		tested++;
		testedRejects++;
		accPct = 100.0f * (float)testedCorrect / (float)tested;
	}
}

//
// classifyChar - 거르기 1·2(키 수)를 통과하면 feat[]로 판정하고, 거르기 3(점수 차)을 봅니다. targetLabel이 있으면 채점합니다.
//
static void classifyChar(void)
{
	uint16_t	k, best = 0U, second;
	int16_t		truth = targetLabel;

	if(charKeys < minKeys)						// 거르기 1: 점 찍기, 짧은 긋기
	{
		for(k = 0U; k < NUM_CLASSES; k++)
		{
			scores[k] = 0;
		}
		rawDigit = -1;
		margin = 0;
		rejectChar(1U);
		return;
	}
	if(charMaxKeys > maxKeysFrame)				// 거르기 2: 손바닥, 여러 손가락
	{
		for(k = 0U; k < NUM_CLASSES; k++)
		{
			scores[k] = 0;
		}
		rawDigit = -1;
		margin = 0;
		rejectChar(2U);
		return;
	}

	quantizeInput((const float *)feat);
	runModel();
	for(k = 0U; k < NUM_CLASSES; k++)
	{
		scores[k] = (int16_t)modelOutput[k];
		if(modelOutput[k] > modelOutput[best])
		{
			best = k;
		}
	}
	second = (best == 0U) ? 1U : 0U;
	for(k = 0U; k < NUM_CLASSES; k++)
	{
		if((k != best) && (modelOutput[k] > modelOutput[second]))
		{
			second = k;
		}
	}
	rawDigit = (int16_t)best;
	margin = (int16_t)modelOutput[best] - (int16_t)modelOutput[second];
	if(margin < minMargin)						// 거르기 3: 모델이 헷갈림 (scores, rawDigit은 남겨 둠)
	{
		rejectChar(3U);
		return;
	}
	predDigit = (int16_t)best;
	rejectReason = 0U;
	charCount++;

	if((truth >= 0) && (truth < (int16_t)NUM_CLASSES))
	{
		predCorrect = (best == (uint16_t)truth) ? 1U : 0U;
		tested++;
		testedCorrect += predCorrect;
		accPct = 100.0f * (float)testedCorrect / (float)tested;
		confusion[truth][best]++;
	}
}

//
// ttpRead16 - TTP229 2선 직렬로 16키 상태를 읽습니다. 반환값 비트 i = (i+1)번 키, 1 = 눌림 (8_TouchDigitLogger와 같음)
//
static uint16_t ttpRead16(void)
{
	uint16_t	i, bitIn;
	uint16_t	keys = 0U;

	for(i = 0U; i < NUM_KEYS; i++)
	{
		GPIO_writePin(PIN_TTP_SCL, 0U);
		DEVICE_DELAY_US(TTP_HALF_US);
		bitIn = (uint16_t)GPIO_readPin(PIN_TTP_SDO);
#if TTP_ACTIVE_LOW
		if(bitIn == 0U)
#else
		if(bitIn != 0U)
#endif
		{
			keys |= (uint16_t)(1U << i);
		}
		GPIO_writePin(PIN_TTP_SCL, 1U);
		DEVICE_DELAY_US(TTP_HALF_US);
	}
	return keys;
}

//
// makeFeatures - 키 프레임 len개 -> 특징 NUM_FEAT x N_STEPS (8_TouchDigitLogger, tools/touch_features.py와 같은 식)
//
static uint16_t makeFeatures(const uint16_t *fr, uint16_t len, float *out)
{
	uint16_t	a = 0U, b = len, L, k, i, j, n;
	uint32_t	idx;
	float		sx, sy;

	while((a < b) && (fr[a] == 0U))
	{
		a++;
	}
	while((b > a) && (fr[b - 1U] == 0U))
	{
		b--;
	}
	L = b - a;
	if(L == 0U)
	{
		return 0U;
	}
	for(k = 0U; k < N_STEPS; k++)
	{
		idx = (((uint32_t)k * (uint32_t)(L - 1U)) + ((N_STEPS - 1U) / 2U)) / (N_STEPS - 1U);
		j = a + (uint16_t)idx;
		out[(2U * N_STEPS) + k] = (fr[j] != 0U) ? 1.0f : 0.0f;		// pen
		while(fr[j] == 0U)
		{
			j--;
		}
		sx = 0.0f;
		sy = 0.0f;
		n = 0U;
		for(i = 0U; i < NUM_KEYS; i++)
		{
			if((fr[j] & (uint16_t)(1U << i)) != 0U)
			{
				sx += (float)keyCol[i];
				sy += (float)keyRow[i];
				n++;
			}
		}
		out[k] = sx / (float)n;					// x
		out[N_STEPS + k] = sy / (float)n;		// y
	}
	return 1U;
}

static void updateLiveView(uint16_t keys)
{
	uint16_t	i, n = 0U, first = 0U;

	for(i = 0U; i < NUM_KEYS; i++)
	{
		if((keys & (uint16_t)(1U << i)) != 0U)
		{
			n++;
			if(first == 0U)
			{
				first = i + 1U;
			}
		}
	}
	keysNow = keys;
	keyCountNow = n;
	keyNoNow = first;
}

static void startCapture(void)
{
	capLen = 0U;
	gapTicks = 0U;
	capState = CAP_RUN;
}

//
// finishCapture - 글자 끝: 특징을 만들고 메인 루프에 판정을 맡깁니다
//
static void finishCapture(void)
{
	uint16_t	i, n = 0U, cnt, maxCnt = 0U;
	uint16_t	keysUsed = 0U;
	uint16_t	len = capLen - gapTicks;

	for(i = 0U; i < len; i++)
	{
		if(rawFrames[i] != 0U)
		{
			n++;
			keysUsed |= rawFrames[i];			// 지나간 키 모음
			cnt = bitCount16(rawFrames[i]);		// 이 프레임에 한 번에 눌린 키 수
			if(cnt > maxCnt)
			{
				maxCnt = cnt;
			}
		}
	}
	if(n < MIN_TOUCH_FRAMES)
	{
		shortRejects++;
	}
	else if(newChar != 0U)
	{
		busyDrops++;			// 앞 글자 판정이 아직 안 끝남 (판정은 1ms도 안 걸리므로 보통 0)
	}
	else if(makeFeatures((const uint16_t *)rawFrames, len, (float *)feat) != 0U)
	{
		charMs = len * SAMPLE_MS;
		charKeys = bitCount16(keysUsed);
		charMaxKeys = maxCnt;
		newChar = 1U;
	}
	capState = CAP_IDLE;
	armDvInterrupt();
}

static void armDvInterrupt(void)
{
	if(triggerMode == 0U)
	{
		GPIO_enableInterrupt(GPIO_INT_XINT1);
	}
	else
	{
		GPIO_disableInterrupt(GPIO_INT_XINT1);
	}
}

//
// xint1ISR - SDO 하강 에지 = TTP229 DV 펄스. 대기 중이면 읽기를 시작합니다 (기록이 끝날 때까지 XINT1을 끔)
//
__interrupt void xint1ISR(void)
{
	GPIO_disableInterrupt(GPIO_INT_XINT1);
	dvCount++;
	if(capState == CAP_IDLE)
	{
		armTicks = 0U;
		capState = CAP_ARMED;
	}
	Interrupt_clearACKGroup(INTERRUPT_ACK_GROUP1);
}

//
// timer0ISR - 20ms마다: 기록 중이면 TTP229를 읽어 쌓고, 글자 끝을 판단합니다 (8_TouchDigitLogger와 같음)
//
__interrupt void timer0ISR(void)
{
	uint16_t	keys;

	switch(capState)
	{
	case CAP_IDLE:
		if(triggerMode != 0U)
		{
			GPIO_disableInterrupt(GPIO_INT_XINT1);
			keys = ttpRead16();
			updateLiveView(keys);
			if(keys != 0U)
			{
				startCapture();
				rawFrames[capLen++] = keys;
			}
		}
		else
		{
			GPIO_enableInterrupt(GPIO_INT_XINT1);
		}
		break;

	case CAP_ARMED:
		keys = ttpRead16();
		updateLiveView(keys);
		if(keys != 0U)
		{
			startCapture();
			rawFrames[capLen++] = keys;
		}
		else if(++armTicks >= ARM_TIMEOUT_TICKS)
		{
			falseTriggers++;
			capState = CAP_IDLE;
			armDvInterrupt();
		}
		break;

	default:
		keys = ttpRead16();
		updateLiveView(keys);
		rawFrames[capLen++] = keys;
		gapTicks = (keys == 0U) ? (uint16_t)(gapTicks + 1U) : 0U;
		if((gapTicks >= END_GAP_TICKS) || (capLen >= MAX_FRAMES))
		{
			finishCapture();
		}
		break;
	}
	Interrupt_clearACKGroup(INTERRUPT_ACK_GROUP1);
}

static void initTouchPins(void)
{
	GPIO_setPinConfig(PINMUX_TTP_SCL);					// 23번 핀 -> 모듈 SCL
	GPIO_writePin(PIN_TTP_SCL, 1U);
	GPIO_setPadConfig(PIN_TTP_SCL, GPIO_PIN_TYPE_STD);
	GPIO_setDirectionMode(PIN_TTP_SCL, GPIO_DIR_MODE_OUT);

	GPIO_setPinConfig(PINMUX_TTP_SDO);					// 25번 핀 <- 모듈 SDO
	GPIO_setPadConfig(PIN_TTP_SDO, GPIO_PIN_TYPE_PULLUP);
	GPIO_setQualificationMode(PIN_TTP_SDO, GPIO_QUAL_SYNC);
	GPIO_setDirectionMode(PIN_TTP_SDO, GPIO_DIR_MODE_IN);

	GPIO_setInterruptPin(PIN_TTP_SDO, GPIO_INT_XINT1);
	GPIO_setInterruptType(GPIO_INT_XINT1, GPIO_INT_TYPE_FALLING_EDGE);
	GPIO_disableInterrupt(GPIO_INT_XINT1);
}

static void initSampleTimer(void)
{
	CPUTimer_setPeriod(CPUTIMER0_BASE, (DEVICE_SYSCLK_FREQ / 1000UL) * SAMPLE_MS);
	CPUTimer_setPreScaler(CPUTIMER0_BASE, 0U);
	CPUTimer_stopTimer(CPUTIMER0_BASE);
	CPUTimer_reloadTimerCounter(CPUTIMER0_BASE);
	CPUTimer_setEmulationMode(CPUTIMER0_BASE, CPUTIMER_EMULATIONMODE_RUNFREE);
	CPUTimer_enableInterrupt(CPUTIMER0_BASE);
}

//
// initFreeRunTimer - CPU 타이머1을 SYSCLK마다 1씩 줄어드는 32비트 자유 실행 카운터로 설정 (추론 시간 측정용)
//
static void initFreeRunTimer(void)
{
	CPUTimer_clearOverflowFlag(CPUTIMER1_BASE);
	CPUTimer_setPeriod(CPUTIMER1_BASE, 0xFFFFFFFFUL);
	CPUTimer_setPreScaler(CPUTIMER1_BASE, 0U);
	CPUTimer_reloadTimerCounter(CPUTIMER1_BASE);
	CPUTimer_stopTimer(CPUTIMER1_BASE);
	CPUTimer_startTimer(CPUTIMER1_BASE);
}

// 파일 끝.
