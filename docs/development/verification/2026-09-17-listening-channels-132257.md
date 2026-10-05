# 청취: 채널 식별

- 일시: 2026-09-17 13:22:57
- 조건: 레이아웃 7.1 · 헤드폰 베드 내장 · 헤드 트래킹 off · 개인화 HRTF 사용 안 함 · Surround Depth 100 · Center Width 100 · 룸 2
- 버스트: 0.5 s, -25.0 dBFS, 채널당 3회, 시드 142995045
- 출력 장치: 14-85-09-B7-B7-96:output

| 채널 | 목표 방위 | 시도 | 평균 오차 |
|---|---|---|---|
| C | +0° | 3 | 60.0° |
| L | -30° | 3 | 75.0° |
| R | +30° | 3 | 45.0° |
| Ls | -90° | 3 | 30.0° |
| Rs | +90° | 3 | 30.0° |
| Lb | -135° | 3 | 60.0° |
| Rb | +135° | 3 | 60.0° |

- 전체 평균 방위 오차: **51.4°** (P0-FR1 수치 기준 10°는 하네스용, 청취는 참고)
- 앞뒤 혼동: **6/15 = 40%** (P0-FR2 기준: 트래킹 Off ≤ 25%, On ≤ 10%) — **기준 미달**

원자료:

```json
[{"channel": "Rb", "target": 135, "answer": 45}, {"channel": "C", "target": 0, "answer": 0}, {"channel": "Lb", "target": -135, "answer": -45}, {"channel": "R", "target": 30, "answer": 45}, {"channel": "L", "target": -30, "answer": -45}, {"channel": "C", "target": 0, "answer": 0}, {"channel": "R", "target": 30, "answer": 45}, {"channel": "Ls", "target": -90, "answer": -90}, {"channel": "Lb", "target": -135, "answer": -90}, {"channel": "Lb", "target": -135, "answer": -90}, {"channel": "Rs", "target": 90, "answer": 90}, {"channel": "Rs", "target": 90, "answer": 45}, {"channel": "Rb", "target": 135, "answer": 90}, {"channel": "C", "target": 0, "answer": 180}, {"channel": "L", "target": -30, "answer": -135}, {"channel": "Ls", "target": -90, "answer": -135}, {"channel": "Rb", "target": 135, "answer": 90}, {"channel": "Rs", "target": 90, "answer": 135}, {"channel": "Ls", "target": -90, "answer": -135}, {"channel": "L", "target": -30, "answer": -135}, {"channel": "R", "target": 30, "answer": 135}]
```
