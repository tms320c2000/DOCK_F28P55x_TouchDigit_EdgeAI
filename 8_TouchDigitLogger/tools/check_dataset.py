# check_dataset.py
#
# collect_touch.js로 모은 dataset/classes/ 를 Edge AI Studio에 올리기 전에 확인합니다.
#   - 숫자(클래스)마다 파일 수, 32줄 x 3열(x,y,pen)이 아닌 파일
#   - 같은 숫자 안에서 다른 글자와 많이 다른 파일 (잘못 썼을 가능성) 목록
#   - Studio에 올릴 zip 만들기 (--zip)
#
# 사용: python check_dataset.py <dataset 폴더> [--zip 결과.zip]

import argparse
import glob
import os
import zipfile

N_STEPS = 32


def load(path):
    rows = open(path, encoding="utf-8").read().strip().splitlines()
    if not rows or rows[0].strip() != "x,y,pen":
        return None
    data = []
    for r in rows[1:]:
        v = r.split(",")
        if len(v) != 3:
            return None
        data.append([float(x) for x in v])
    return data if len(data) == N_STEPS else None


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("dataset")
    ap.add_argument("--zip")
    a = ap.parse_args()
    cls_root = os.path.join(a.dataset, "classes")
    if not os.path.isdir(cls_root):
        cls_root = a.dataset
    classes = sorted(d for d in os.listdir(cls_root) if os.path.isdir(os.path.join(cls_root, d)))
    print("클래스 순서(= 모델 출력 순서):", ", ".join(classes))
    bad = []
    for c in classes:
        files = sorted(glob.glob(os.path.join(cls_root, c, "*.csv")))
        good = {}
        for f in files:
            d = load(f)
            if d is None:
                bad.append(f)
            else:
                good[f] = d
        # 평균 궤적과의 거리로 튀는 글자 찾기
        out = []
        if len(good) >= 5:
            mean = [[sum(d[k][j] for d in good.values()) / len(good) for j in range(3)] for k in range(N_STEPS)]
            dist = {f: sum((d[k][0] - mean[k][0]) ** 2 + (d[k][1] - mean[k][1]) ** 2 for k in range(N_STEPS)) / N_STEPS
                    for f, d in good.items()}
            vals = sorted(dist.values())
            med = vals[len(vals) // 2]
            out = [os.path.basename(f) for f, v in sorted(dist.items(), key=lambda kv: -kv[1]) if v > 3 * med + 0.3][:5]
        print("  %-4s 파일 %3d개 (형식 오류 %d)%s" % (c, len(files), len(files) - len(good),
                                               ("  | 다시 볼 것: " + ", ".join(out)) if out else ""))
    if bad:
        print("형식 오류 파일 (32줄 x 'x,y,pen'이 아님):")
        for f in bad:
            print("  ", f)
    if a.zip:
        with zipfile.ZipFile(a.zip, "w", zipfile.ZIP_DEFLATED) as z:
            for c in classes:
                for f in sorted(glob.glob(os.path.join(cls_root, c, "*.csv"))):
                    if f not in bad:
                        z.write(f, os.path.join("classes", c, os.path.basename(f)))
        print("zip:", a.zip, "(형식 오류 파일 제외)")


if __name__ == "__main__":
    main()
