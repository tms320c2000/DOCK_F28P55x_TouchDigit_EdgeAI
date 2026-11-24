// 파일이름	:	8_TouchDigitLogger.c
// 대상장치	:	TMS320F28P55x(F28P550SJ9 / F28P559SJ9-Q1) 모듈 - TMS320F28X EVM V2 또는 초소형 모듈 DOCK 보드(전용 어댑터)
//			+ TTP229 4x4 정전식 터치 키패드 모듈 (TTP229-BSF, 아두이노용 16키 모듈)
// 파일버전	:	1.10
// 갱신이력	:	2026-11-19, 버전 1.00 - 터치 키패드에 손가락으로 쓴 숫자를 글자 단위로 잘라 Edge AI Studio 분류 학습용 특징(32 x 3)으로 기록
//			:	2026-11-22, 버전 1.10 - SDO 입력을 47번 핀(GPIO9)에서 25번 핀(GPIO4)으로 옮김. DOCK 보드에서 SCL·SDO·GND가 한 헤더(CN4200)에 모이도록
// 예제설명	:

//************************************************************************************************************************************************************************
//
// 본 예제는 TTP229 4x4 터치 키패드 위에 손가락으로 쓴 글자 하나를 "키가 눌린 순서와 위치의 시계열"로 기록합니다.
// CCS Scripting Console의 collect_touch.js가 글자마다 값을 읽어 Edge AI Studio 분류용 CSV(글자 하나 = 파일 하나)로 저장합니다.
// 학습한 모델은 9_TouchDigitClassify_NPU에서 NPU로 실행합니다.
//
// a. 배선 (TTP229 모듈 12핀 헤더. 실크 인쇄 왼쪽부터 VCC GND SCL SDO OUT1~OUT8 = 회로도 P3 12번~1번, 사진 ref/ttp229/1327405_1.jpg)
//
//		DOCK 보드 우측 핀-헤더(CN420x/CN430x)와 EVM V2 B Side 핀-헤더는 F28P559SJ9-Q1 어댑터 탑재 시 번호가 같습니다
//		(DOCK 보드 매뉴얼 표 7-4 = EVM V2 F28P559SJ9-Q1 핀 정의표 표 2). 그래서 아래 번호는 두 보드에 그대로 씁니다.
//
//		모듈 VCC (P3 12번) ◀── 보드 3.3V            DOCK : 범용 기판 위 전원 핀-헤더의 '3.3V' 줄 / EVM V2 : 3.3V 단자
//		                                             ※ 5V를 넣으면 SDO가 5V로 나와 MCU 입력 핀이 상합니다
//		모듈 GND (P3 11번) ◀── 보드 GND             DOCK : 우측 CN4200의 3번 핀 (또는 전원 핀-헤더 'GND' 줄)
//		모듈 SCL (P3 10번) ◀── 23번 핀 (GPIO8, 출력)  DOCK : 우측 CN4200 실크 23 / EVM V2 : B Side 23
//		모듈 SDO (P3  9번) ──▶ 25번 핀 (GPIO4, 입력, 외부 인터럽트 XINT1)  DOCK : 우측 CN4200 실크 25 / EVM V2 : B Side 25
//		모듈 OUT1~OUT8 (P3 8~1번) : 연결하지 않습니다 (아래 b 참고)
//
//		※ DOCK 보드 CN4200은 모듈 오른쪽 안쪽 줄(홀수 1~39번), CN4300은 바깥 줄(짝수 2~40번)입니다. 3·23·25번이 모두 CN4200에 있습니다.
//		※ 우측 8번 핀 TVD(VDDIO)는 JTAG 전압 감지용이므로 모듈 전원으로 쓰지 않습니다.
//
// b. 모듈 설정 (점퍼 = 헤더 아래 2x4 핀헤더 P2(위)·P1(아래). 세로 두 핀을 이으면 1M 저항으로 GND에 연결되어 옵션 "0")
//
//		회로도와 사각 패드(1번 핀) 위치로 본 배치 (사진의 작은 TP 글씨는 해상도가 낮아 읽지 못함, 아래 확인 방법 참고)
//		P2(위) 왼쪽부터 : TP7 TP6 TP5 TP4
//		P1(아래) 왼쪽부터 : TP3 TP2 TP1 TP0
//		확인 방법 : triggerMode = 1에서 TP2 점퍼를 이은 뒤 모듈 전원을 다시 켜고 9~16번 키를 눌러 keyNoNow가 9~16으로 나오면 16키 모드가 된 것
//		            TP3+TP4를 이은 뒤 두 키를 함께 눌러 keyCountNow = 2가 나오면 멀티키가 된 것
//
//		TP2 연결 : 16키 모드 (안 하면 8키 모드라 9~16번 키가 동작하지 않음)
//		TP3, TP4 둘 다 연결 : 모든 키 동시 입력(멀티키). 손가락이 두 칸에 걸치면 두 키가 함께 눌린 것으로 나와 위치가 더 정확함
//		TP5 연결(권장) : 잠자기 모드에서 깨어나는 확인 주기 8Hz(125ms) -> 64Hz(15.6ms). 한동안 쉬었다 쓰는 첫 획이 덜 잘림
//		TP0, TP1 : 연결하지 않음 (2선 직렬 출력이 액티브 로우 = SDO 0이 '눌림', 이 코드가 가정하는 기본값)
//		점퍼를 바꾼 뒤에는 모듈 전원을 껐다 켭니다. 전원을 켠 뒤 약 0.5초는 자동 보정 시간이라 키를 만지지 않습니다.
//
//		※ OUT1~OUT8 직접 출력은 8키 모드에서만 나옵니다. 16키 모드에서는 같은 핀이 9~16번 키의 터치 입력으로 바뀝니다
//		  (TTP229 데이터시트 "8 pins direct output only use at 8 keys input mode", 모듈 회로도 P3 1~8번 = K15~K8).
//		  그래서 "터치가 시작됐다"는 신호는 OUT 핀 대신 SDO의 DV(Data Valid) 펄스(약 93us 동안 0)로 받습니다.
//
// c. TTP229 2선 직렬 읽기 (데이터시트 3-4)
//
//		SCL은 평소 1. 16번 반복: SCL 0 -> 잠깐 기다림 -> SDO 읽기 -> SCL 1. 첫 비트가 1번 키(TP0), 마지막이 16번 키(TP15)
//		SCL 주파수 1kHz~512kHz (여기서는 반주기 TTP_HALF_US = 4us, 약 110kHz)
//		SCL에 2ms 넘게 변화가 없으면 TTP229가 직렬 상태를 처음으로 되돌리므로, 20ms마다 읽으면 매번 1번 키부터 나옵니다
//		16키 모드의 키 상태 갱신(응답) 시간은 약 32ms이고, 20ms마다 읽으면 갱신을 놓치지 않습니다
//
// d. 글자 하나 자르기
//
//		대기 : SDO에 DV 펄스(하강 에지)가 오면 XINT1 인터럽트 -> 20ms마다 읽기 시작 (triggerMode = 1이면 DV 없이 늘 읽음)
//		기록 : 키가 처음 눌린 프레임부터 20ms마다 16비트 키 상태를 rawFrames[]에 쌓음 (최대 MAX_FRAMES = 4초)
//		끝   : 아무 키도 안 눌린 상태가 END_GAP_MS(600ms) 이어지면 글자 끝. 4, 5, 7처럼 손을 떼고 다시 쓰는 획은 그 안에만 이어 쓰면 됨
//		버림 : 눌린 프레임이 MIN_TOUCH_FRAMES(3)개보다 적으면 잡음으로 보고 버림 (shortRejects)
//
// e. 특징 (모델 입력, tools/touch_features.py와 같은 계산)
//
//		프레임마다 눌린 키들의 무게중심 x(열 0~3), y(행 0~3), pen(눌림 1 / 뗌 0). 뗀 동안은 x, y를 직전 값으로 유지
//		글자 길이(프레임 수)와 상관없이 N_STEPS(32)칸으로 고르게 다시 뽑음 -> feat[3][32] (배열 순서 [x 32개][y 32개][pen 32개])
//		Edge AI Studio : Variables 3, Frame Size 32 (CSV 한 파일 = 32줄 = 창 하나 = 학습 샘플 하나)
//
// f. 수집 스크립트와 주고받기
//
//		글자가 끝나면 sampleReady = 1, charCount 1 증가. holdUntilRead = 1(기본)이면 스크립트가 sampleReady = 0으로 바꿀 때까지
//		다음 글자를 받지 않습니다(그동안 쓴 글자는 missedChars에 셈). 스크립트 없이 볼 때는 holdUntilRead = 0으로 바꾸세요.
//
// CCS Expressions 창에 등록할 변수 (watch_var.json으로 한 번에 등록):
// --> triggerMode, capState, keysNow, keyNoNow, keyCountNow, dvCount, falseTriggers
// --> charCount, sampleReady, holdUntilRead, rawLen, charMs, touchFrames, shortRejects, missedChars, targetLabel, feat
//
// 본 예제는 TI가 제공하는 Driver API Library(DriverLib)만으로 제작되었습니다 (SysConfig 미사용).
//
//************************************************************************************************************************************************************************


// 헤더 파일들
#include <stdint.h>

#include "driverlib.h"			// TI 제공 Driver API Library 헤더파일 (driverlib)
#include "device.h"

// 전처리 구문 정의
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
#define	N_STEPS				32U			// 모델 입력 길이 (Studio Frame Size)
#define	NUM_FEAT			3U			// x, y, pen (Studio Variables)
#define	END_GAP_TICKS		(END_GAP_MS / SAMPLE_MS)
#define	ARM_TIMEOUT_TICKS	(ARM_TIMEOUT_MS / SAMPLE_MS)

#define	CAP_IDLE			0U			// DV(또는 첫 터치)를 기다림
#define	CAP_ARMED			1U			// DV를 받음, 첫 터치를 기다림
#define	CAP_RUN				2U			// 글자 기록 중

// 함수 원형 선언
static void		initTouchPins(void);
static void		initSampleTimer(void);
static uint16_t	ttpRead16(void);
static void		startCapture(void);
static void		finishCapture(void);
static void		armDvInterrupt(void);
static uint16_t	makeFeatures(const uint16_t *fr, uint16_t len, float *out);
static void		updateLiveView(uint16_t keys);
__interrupt void timer0ISR(void);
__interrupt void xint1ISR(void);


// 전역 변수 선언 (CCS Expressions/Watch 창에서 실시간으로 값을 확인할 수 있는 전역 변수)

// 설정
volatile uint16_t	triggerMode = 0U;		// 0 = SDO DV 인터럽트로 시작(기본), 1 = DV 없이 20ms마다 늘 읽음 (배선·키 배치 확인용)
volatile uint16_t	holdUntilRead = 1U;		// 1 = 스크립트가 읽을 때까지 다음 글자를 받지 않음
volatile int16_t	targetLabel = -1;		// 수집 스크립트가 지금 쓰라고 한 숫자 (표시용, -1 = 없음)

// 실시간 상태
volatile uint16_t	capState = CAP_IDLE;	// 0 대기 / 1 DV 받음 / 2 기록 중
volatile uint16_t	keysNow = 0U;			// 마지막으로 읽은 16비트 키 상태 (비트 i = (i+1)번 키, 1 = 눌림)
volatile uint16_t	keyNoNow = 0U;			// 눌린 키 중 가장 작은 번호 1~16 (0 = 없음). 키 배치 확인용
volatile uint16_t	keyCountNow = 0U;		// 지금 눌린 키 개수
volatile uint32_t	dvCount = 0UL;			// 받은 DV 인터럽트 수
volatile uint32_t	falseTriggers = 0UL;	// DV 뒤 ARM_TIMEOUT_MS 안에 키가 안 보인 횟수
volatile uint32_t	readCount = 0UL;		// TTP229 읽은 횟수

// 글자 결과 (sampleReady = 1일 때 유효)
volatile uint16_t	sampleReady = 0U;		// 1 = 새 글자 있음 (스크립트가 읽고 0으로)
volatile uint32_t	charCount = 0UL;		// 기록한 글자 수
volatile uint16_t	rawLen = 0U;			// rawFrames 길이 (첫 터치 ~ 마지막 터치)
volatile uint16_t	charMs = 0U;			// 글자 쓰는 데 걸린 시간(ms) = rawLen x SAMPLE_MS
volatile uint16_t	touchFrames = 0U;		// 그중 키가 눌린 프레임 수
volatile uint32_t	shortRejects = 0UL;		// 너무 짧아 버린 글자 수
volatile uint32_t	missedChars = 0UL;		// sampleReady = 1(아직 안 읽음)인 동안 들어온 DV 수. 0이 아니면 그동안 쓴 글자는 버려짐
volatile uint16_t	rawFrames[MAX_FRAMES];	// 20ms마다 읽은 키 상태
volatile float		feat[NUM_FEAT * N_STEPS];	// 특징 [x 32개][y 32개][pen 32개] = 모델 입력과 같은 순서

// 키 번호(1~16) -> 위치. 모듈 실크 인쇄는 왼쪽 위부터 가로로 1 2 3 4 / 5 6 7 8 / 9 10 11 12 / 13 14 15 16 (사진 1327405_1.jpg로 확인)
// 실크 번호 n이 칩의 TP(n-1)로 연결된다는 것은 회로도 기준입니다. triggerMode = 1에서 키를 눌러 keyNoNow가 실크 번호와 같은지 한 번 확인하세요.
// tools/touch_features.py, 9_TouchDigitClassify_NPU.c와 같아야 합니다.
static const uint16_t	keyCol[NUM_KEYS] = { 0U, 1U, 2U, 3U, 0U, 1U, 2U, 3U, 0U, 1U, 2U, 3U, 0U, 1U, 2U, 3U };
static const uint16_t	keyRow[NUM_KEYS] = { 0U, 0U, 0U, 0U, 1U, 1U, 1U, 1U, 2U, 2U, 2U, 2U, 3U, 3U, 3U, 3U };

static uint16_t		armTicks = 0U;
static uint16_t		gapTicks = 0U;
static uint16_t		capLen = 0U;


// 메인 함수
void main(void)
{
//	1. 시스템 초기화 - 왓치독 비-활성화, PLL 150MHz, 주변회로 클럭 공급
	Device_init();

//	2. GPIO 핀 락 해제, 내부 풀업 활성화
	Device_initGPIO();

//	3. 주변회로 인터럽트 확장회로 초기화, 벡터 테이블 초기화, 인터럽트 등록
	Interrupt_initModule();
	Interrupt_initVectorTable();
	Interrupt_register(INT_TIMER0, &timer0ISR);
	Interrupt_register(INT_XINT1, &xint1ISR);

//	4. TTP229 전원 안정·자동 보정 시간(약 0.5초) 기다림. 이때 키를 만지지 않습니다
	DEVICE_DELAY_US(600000UL);

//	5. TTP229 연결 핀(SCL 출력, SDO 입력 + XINT1), 20ms 타이머
	initTouchPins();
	initSampleTimer();

//	6. 인터럽트 활성화, 실시간 디버깅 활성화
	Interrupt_enable(INT_TIMER0);
	Interrupt_enable(INT_XINT1);
	armDvInterrupt();
	EINT;
	ERTM;

//	7. 20ms 타이머 시작
	CPUTimer_startTimer(CPUTIMER0_BASE);

//	8. Idle(Background) Loop - 모든 일은 인터럽트에서 합니다
	for(;;)
	{
	}
}

//	9. 함수들

//
// ttpRead16 - TTP229 2선 직렬로 16키 상태를 읽습니다. 반환값 비트 i = (i+1)번 키, 1 = 눌림
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
	readCount++;
	return keys;
}

//
// makeFeatures - 키 프레임 len개 -> 특징 NUM_FEAT x N_STEPS. 앞뒤 빈 프레임은 빼고 계산. 반환값 = 쓸 수 있으면 1
//   (tools/touch_features.py의 make_features와 같은 식)
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
		// 정수 반올림으로 고르게 뽑기: idx = (k*(L-1) + (N_STEPS-1)/2) / (N_STEPS-1)
		idx = (((uint32_t)k * (uint32_t)(L - 1U)) + ((N_STEPS - 1U) / 2U)) / (N_STEPS - 1U);
		j = a + (uint16_t)idx;
		out[(2U * N_STEPS) + k] = (fr[j] != 0U) ? 1.0f : 0.0f;		// pen
		while(fr[j] == 0U)		// 뗀 동안은 직전에 눌린 프레임의 위치 (fr[a]는 눌린 프레임이라 반드시 멈춤)
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

//
// updateLiveView - 지금 키 상태를 Expressions 창용 변수에 반영
//
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

//
// startCapture / finishCapture - 글자 하나 기록 시작과 끝
//
static void startCapture(void)
{
	capLen = 0U;
	gapTicks = 0U;
	capState = CAP_RUN;
}

static void finishCapture(void)
{
	uint16_t	i, n = 0U;
	uint16_t	len = capLen - gapTicks;	// 마지막 터치 뒤의 빈 프레임은 뺌

	for(i = 0U; i < len; i++)
	{
		if(rawFrames[i] != 0U)
		{
			n++;
		}
	}
	if(n < MIN_TOUCH_FRAMES)
	{
		shortRejects++;
	}
	else if(makeFeatures((const uint16_t *)rawFrames, len, (float *)feat) != 0U)
	{
		rawLen = len;
		charMs = len * SAMPLE_MS;
		touchFrames = n;
		charCount++;
		sampleReady = 1U;
	}
	capState = CAP_IDLE;
	armDvInterrupt();
}

//
// armDvInterrupt - 대기 상태에서 SDO 하강 에지(DV)를 기다리도록 XINT1을 켭니다
//
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
// xint1ISR - SDO 하강 에지 = TTP229 DV 펄스(키 상태가 바뀌어 새 데이터가 있음). 대기 중이면 읽기를 시작합니다
//   이후 직렬 읽기 동안에도 SDO가 0으로 바뀌므로, 기록이 끝날 때까지 XINT1을 끕니다
//
__interrupt void xint1ISR(void)
{
	GPIO_disableInterrupt(GPIO_INT_XINT1);
	dvCount++;
	if(capState == CAP_IDLE)
	{
		if((sampleReady != 0U) && (holdUntilRead != 0U))
		{
			missedChars++;
			armDvInterrupt();		// 스크립트가 읽을 때까지 무시
		}
		else
		{
			armTicks = 0U;
			capState = CAP_ARMED;
		}
	}
	Interrupt_clearACKGroup(INTERRUPT_ACK_GROUP1);
}

//
// timer0ISR - 20ms마다: 기록 중이면 TTP229를 읽어 쌓고, 글자 끝을 판단합니다
//
__interrupt void timer0ISR(void)
{
	uint16_t	keys;

	switch(capState)
	{
	case CAP_IDLE:
		if(triggerMode != 0U)						// DV 없이 늘 읽는 방식 (배선·키 배치 확인용)
		{
			GPIO_disableInterrupt(GPIO_INT_XINT1);
			keys = ttpRead16();
			updateLiveView(keys);
			if((keys != 0U) && !((sampleReady != 0U) && (holdUntilRead != 0U)))
			{
				startCapture();
				rawFrames[capLen++] = keys;
			}
		}
		else
		{
			GPIO_enableInterrupt(GPIO_INT_XINT1);		// triggerMode를 1 -> 0으로 바꿨을 때도 DV를 다시 기다리도록
		}
		break;

	case CAP_ARMED:									// DV를 받았음. 첫 터치를 기다림
		keys = ttpRead16();
		updateLiveView(keys);
		if(keys != 0U)
		{
			startCapture();
			rawFrames[capLen++] = keys;
		}
		else if(++armTicks >= ARM_TIMEOUT_TICKS)	// 손을 뗄 때도 DV가 오므로 이 경우가 흔함
		{
			falseTriggers++;
			capState = CAP_IDLE;
			armDvInterrupt();
		}
		break;

	default:										// CAP_RUN: 글자 기록 중
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

//
// initTouchPins - SCL 출력(평소 1), SDO 입력(풀업, SYSCLK 동기) + XINT1 하강 에지
//
static void initTouchPins(void)
{
	GPIO_setPinConfig(PINMUX_TTP_SCL);					// 23번 핀 -> 모듈 SCL
	GPIO_writePin(PIN_TTP_SCL, 1U);
	GPIO_setPadConfig(PIN_TTP_SCL, GPIO_PIN_TYPE_STD);
	GPIO_setDirectionMode(PIN_TTP_SCL, GPIO_DIR_MODE_OUT);

	GPIO_setPinConfig(PINMUX_TTP_SDO);					// 25번 핀 <- 모듈 SDO
	GPIO_setPadConfig(PIN_TTP_SDO, GPIO_PIN_TYPE_PULLUP);	// 모듈이 빠져도 '안 눌림'(1)으로 읽힘
	GPIO_setQualificationMode(PIN_TTP_SDO, GPIO_QUAL_SYNC);
	GPIO_setDirectionMode(PIN_TTP_SDO, GPIO_DIR_MODE_IN);

	GPIO_setInterruptPin(PIN_TTP_SDO, GPIO_INT_XINT1);		// 입력 X-BAR INPUT4 -> XINT1
	GPIO_setInterruptType(GPIO_INT_XINT1, GPIO_INT_TYPE_FALLING_EDGE);
	GPIO_disableInterrupt(GPIO_INT_XINT1);
}

//
// initSampleTimer - CPU 타이머0을 SAMPLE_MS(20ms) 주기 인터럽트로 설정
//
static void initSampleTimer(void)
{
	CPUTimer_setPeriod(CPUTIMER0_BASE, (DEVICE_SYSCLK_FREQ / 1000UL) * SAMPLE_MS);
	CPUTimer_setPreScaler(CPUTIMER0_BASE, 0U);
	CPUTimer_stopTimer(CPUTIMER0_BASE);
	CPUTimer_reloadTimerCounter(CPUTIMER0_BASE);
	CPUTimer_setEmulationMode(CPUTIMER0_BASE, CPUTIMER_EMULATIONMODE_RUNFREE);
	CPUTimer_enableInterrupt(CPUTIMER0_BASE);
}

// 파일 끝.
