# touch_features.py
#
# TTP229 4x4 터치 키패드로 읽은 "글자 하나"의 키 프레임(16비트 마스크 목록)을
# Edge AI Studio 학습용 특징 32줄 x 3열(x, y, pen)로 바꿉니다.
#
# 펌웨어(8_TouchDigitLogger.c, 9_TouchDigitClassify_NPU.c)의 makeFeatures()와 같은 계산입니다.
# 펌웨어를 고치면 이 파일도 같이 고쳐야 학습 데이터와 보드 추론이 같은 값을 씁니다.
#
#   frames : 20ms마다 읽은 16비트 값 목록. 비트 i = 키 (i+1), 1 = 눌림
#   1) 앞뒤의 빈 프레임(아무 키도 안 눌림)을 잘라 냄
#   2) 프레임마다 눌린 키들의 무게중심 x(열 0~3), y(행 0~3), pen(눌림 1 / 뗌 0)
#      뗀 프레임은 x, y를 바로 앞 값으로 유지 (예: 4, 5, 7처럼 두 번에 나눠 쓰는 글자)
#   3) 글자 전체 길이를 N_STEPS(32)칸으로 고르게 다시 뽑음 (빨리 쓰든 천천히 쓰든 같은 길이)

N_STEPS = 32          # 모델 입력 길이 = Studio Frame Size
NUM_KEYS = 16

# 키 번호(1~16) -> 위치. 모듈 실크 인쇄는 "1 2 3 4 / 5 6 7 8 / ..." (왼쪽 위부터 가로, ref/ttp229/1327405_1.jpg로 확인)
# 펌웨어의 keyCol[], keyRow[]와 같아야 합니다.
KEY_COL = [i % 4 for i in range(NUM_KEYS)]
KEY_ROW = [i // 4 for i in range(NUM_KEYS)]


def trim(frames):
    """앞뒤 빈 프레임을 잘라 냅니다. 모두 비었으면 빈 목록."""
    a, b = 0, len(frames)
    while a < b and frames[a] == 0:
        a += 1
    while b > a and frames[b - 1] == 0:
        b -= 1
    return frames[a:b]


def frame_xy(mask):
    """눌린 키들의 무게중심 (x, y, count)."""
    sx = sy = n = 0
    for i in range(NUM_KEYS):
        if mask & (1 << i):
            sx += KEY_COL[i]
            sy += KEY_ROW[i]
            n += 1
    if n == 0:
        return 0.0, 0.0, 0
    return sx / n, sy / n, n


def make_features(frames, n_steps=N_STEPS):
    """frames(잘라 내기 전) -> [(x, y, pen)] n_steps개. 쓸 수 없으면 None."""
    f = trim(frames)
    L = len(f)
    if L == 0:
        return None
    xs, ys, pens = [], [], []
    lx, ly = 0.0, 0.0
    for m in f:
        x, y, n = frame_xy(m)
        if n > 0:
            lx, ly = x, y
            pens.append(1.0)
        else:
            pens.append(0.0)
        xs.append(lx)
        ys.append(ly)
    out = []
    for k in range(n_steps):
        # 정수 반올림 인덱스 (펌웨어와 같은 식): idx = (k*(L-1) + (n_steps-1)/2) / (n_steps-1)
        idx = (k * (L - 1) + (n_steps - 1) // 2) // (n_steps - 1) if n_steps > 1 else 0
        out.append((xs[idx], ys[idx], pens[idx]))
    return out


def touched_frames(frames):
    return sum(1 for m in trim(frames) if m != 0)


def write_csv(path, feats):
    with open(path, "w", encoding="utf-8", newline="\n") as fp:
        fp.write("x,y,pen\n")
        for x, y, p in feats:
            fp.write("%.4f,%.4f,%d\n" % (x, y, int(p)))


def ascii_path(frames):
    """4x4 격자에 처음 눌린 순서를 1-9, a-z로 표시 (확인용)."""
    order = {}
    step = 0
    for m in trim(frames):
        for i in range(NUM_KEYS):
            if (m & (1 << i)) and i not in order:
                order[i] = step
        if m:
            step += 1
    sym = "123456789abcdefghijklmnopqrstuvwxyz"
    rank = {k: r for r, (k, _) in enumerate(sorted(order.items(), key=lambda kv: kv[1]))}
    lines = []
    for r in range(4):
        row = ""
        for c in range(4):
            i = [j for j in range(NUM_KEYS) if KEY_ROW[j] == r and KEY_COL[j] == c][0]
            row += (sym[min(rank[i], len(sym) - 1)] if i in rank else ".") + " "
        lines.append(row.rstrip())
    return "\n".join(lines)
