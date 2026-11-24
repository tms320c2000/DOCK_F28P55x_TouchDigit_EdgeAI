# make_synthetic_dataset.py
#
# 실제 데이터를 모으기 전에 9_TouchDigitClassify_NPU를 빌드·시험하기 위한 "임시 모델"용 합성 데이터를 만듭니다.
# 손가락이 4x4 키패드 위에서 숫자 0~9를 쓰는 모습을 흉내 내 TTP229가 20ms마다 내보낼 키 프레임을 만들고,
# touch_features.py(펌웨어와 같은 계산)로 32줄 x 3열 CSV를 씁니다. 형식은 collect_touch.js가 만드는 실제 데이터와 같습니다.
#
# 합성 데이터는 사람 손글씨와 다릅니다. 이 데이터로 만든 모델은 연결 확인용이고, 실제 정확도는 직접 모은 데이터로 다시 학습해야 나옵니다.
#
# 사용: python make_synthetic_dataset.py --out <폴더> [--per-class 120] [--seed 1]
#       결과: <폴더>/classes/D0 ... D9/*.csv

import argparse
import math
import os
import random

from touch_features import NUM_KEYS, KEY_COL, KEY_ROW, make_features, write_csv, touched_frames

POLL_MS = 20.0       # 펌웨어가 TTP229를 읽는 주기
TRESP_MS = 32.0      # TTP229 16키 모드 응답 시간 (데이터시트 T_resp)

# 숫자 획 (x = 열 0~3, y = 행 0~3, 위에서 아래로). 한 글자에 획이 여럿이면 사이에 손을 뗌
DIGITS = {
    0: [[(1.5, 0.0), (0.3, 0.6), (0.2, 2.4), (1.5, 3.0), (2.8, 2.4), (2.7, 0.6), (1.5, 0.0)]],
    1: [[(1.0, 0.6), (1.6, 0.0), (1.6, 3.0)]],
    2: [[(0.3, 0.6), (1.5, 0.0), (2.7, 0.6), (2.4, 1.5), (0.3, 3.0), (2.8, 3.0)]],
    3: [[(0.3, 0.2), (2.6, 0.2), (1.3, 1.4), (2.7, 2.1), (2.1, 3.0), (0.3, 2.8)]],
    4: [[(1.8, 0.0), (0.2, 2.0), (2.9, 2.0)], [(2.1, 0.8), (2.1, 3.0)]],
    5: [[(2.7, 0.0), (0.5, 0.0), (0.4, 1.3), (2.2, 1.3), (2.7, 2.3), (1.8, 3.0), (0.3, 2.8)]],
    6: [[(2.5, 0.0), (0.8, 0.9), (0.3, 2.4), (1.4, 3.0), (2.6, 2.5), (2.3, 1.6), (0.5, 1.9)]],
    7: [[(0.2, 0.2), (2.8, 0.2), (1.2, 3.0)]],
    8: [[(2.5, 0.4), (1.5, 0.0), (0.5, 0.5), (1.5, 1.5), (2.6, 2.4), (1.5, 3.0), (0.4, 2.4), (1.5, 1.5), (2.5, 0.5)]],
    9: [[(2.6, 0.9), (1.5, 0.0), (0.4, 0.8), (1.4, 1.6), (2.6, 1.0), (2.5, 3.0)]],
}


def transform(strokes, rnd):
    s = rnd.uniform(0.80, 1.05)
    sx = s * rnd.uniform(0.9, 1.1)
    a = math.radians(rnd.uniform(-12, 12))
    dx, dy = rnd.uniform(-0.3, 0.3), rnd.uniform(-0.3, 0.3)
    out = []
    for st in strokes:
        pts = []
        for x, y in st:
            x, y = x - 1.5, y - 1.5
            x, y = x * sx, y * s
            x, y = x * math.cos(a) - y * math.sin(a), x * math.sin(a) + y * math.cos(a)
            x += 1.5 + dx + rnd.uniform(-0.15, 0.15)
            y += 1.5 + dy + rnd.uniform(-0.15, 0.15)
            pts.append((min(max(x, -0.2), 3.2), min(max(y, -0.2), 3.2)))
        out.append(pts)
    return out


def path_points(pts, t0, speed, dt=2.0):
    """한 획을 일정 속도로 따라가며 (시각 ms, x, y) 목록."""
    out = []
    t = t0
    for (x0, y0), (x1, y1) in zip(pts[:-1], pts[1:]):
        d = math.hypot(x1 - x0, y1 - y0)
        n = max(1, int(d / speed / dt))
        for i in range(n):
            u = i / n
            out.append((t, x0 + (x1 - x0) * u, y0 + (y1 - y0) * u))
            t += dt
    out.append((t, pts[-1][0], pts[-1][1]))
    return out, t


def keys_at(x, y, r):
    m = 0
    best, bd = None, 9.0
    for i in range(NUM_KEYS):
        d = math.hypot(KEY_COL[i] - x, KEY_ROW[i] - y)
        if d < r:
            m |= 1 << i
        if d < bd:
            best, bd = i, d
    if m == 0 and bd < 0.75:
        m = 1 << best
    return m


def simulate(digit, rnd):
    strokes = transform(DIGITS[digit], rnd)
    total_len = sum(math.hypot(b[0] - a[0], b[1] - a[1]) for st in strokes for a, b in zip(st[:-1], st[1:]))
    dur = rnd.uniform(500, 1400)                 # 획을 긋는 데 걸리는 시간(ms)
    speed = total_len / dur                       # 칸/ms
    r = rnd.uniform(0.45, 0.70)                   # 손가락 접촉 반경(칸)
    t = rnd.uniform(0, TRESP_MS)
    timeline = []                                 # (시작, 끝, 점 목록)
    for k, st in enumerate(strokes):
        pts, t_end = path_points(st, t, speed)
        timeline.append(pts)
        t = t_end + (rnd.uniform(150, 350) if k < len(strokes) - 1 else 0)
    t_end_all = t

    def touch_state(tq):
        for pts in timeline:
            if pts[0][0] <= tq <= pts[-1][0]:
                # 가장 가까운 시각의 점
                j = min(range(len(pts)), key=lambda i: abs(pts[i][0] - tq))
                return keys_at(pts[j][1], pts[j][2], r)
        return 0

    frames = []
    tq = 0.0
    while tq < t_end_all + 600:
        # TTP229는 TRESP_MS마다 키 상태를 갱신 -> 펌웨어는 마지막 갱신 값을 읽음
        latched = math.floor(tq / TRESP_MS) * TRESP_MS
        m = touch_state(latched)
        if m and rnd.random() < 0.03:
            m = 0                                 # 잠깐 놓침
        if m and rnd.random() < 0.02:
            m |= 1 << rnd.randrange(NUM_KEYS)     # 옆 키가 함께 눌리는 잡음
        frames.append(m)
        tq += POLL_MS
    return frames


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--out", required=True)
    ap.add_argument("--per-class", type=int, default=120)
    ap.add_argument("--seed", type=int, default=1)
    a = ap.parse_args()
    rnd = random.Random(a.seed)
    for d in range(10):
        cdir = os.path.join(a.out, "classes", "D%d" % d)
        os.makedirs(cdir, exist_ok=True)
        n = 0
        while n < a.per_class:
            frames = simulate(d, rnd)
            if touched_frames(frames) < 3:
                continue
            write_csv(os.path.join(cdir, "D%d_syn_%03d.csv" % (d, n)), make_features(frames))
            n += 1
    print("완료:", os.path.join(a.out, "classes"), "(클래스 10개 x %d개)" % a.per_class)


if __name__ == "__main__":
    main()
