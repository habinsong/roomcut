# 2026-09-08 C 볼륨 쓰기의 부분 실패

기존 UI/UX와 C 함수 서명은 유지했습니다. `roomcutClientVolumeSet`이 실패한 단계를 버리고
다른 장치 쓰기만 성공하면 전체 성공을 반환하는 문제를 재현하고 수정했습니다.

## 재현과 변경

실제 `ClientShim.cpp`, `EngineConnection.cpp`, `Control.cpp`를 검사 실행 파일에 연결했습니다.
서비스 조회는 검사 내부의 Mach 포트로만 연결하고 모든 HAL 함수는 모의 구현입니다.
CoreAudio 프레임워크를 링크하지 않았음을 [연결 목록](raw/2026-09-08-client-volume/linked-frameworks.txt)으로 확인했습니다.
운영 엔진이나 실제 장치에 볼륨·기본 출력 명령을 보내지 않았습니다.

수정 전 [13개 검증 항목이 실패](raw/2026-09-08-client-volume/before.txt)했습니다.
증폭 명령에 오류 7을 반환했는데 C 함수는 성공 0을 반환하고 두 장치를 0.4에서 1.0으로 올렸습니다.
수정 후에는 오류 7이 그대로 반환되고 두 장치는 0.4를 유지합니다.
[수정 후 출력](raw/2026-09-08-client-volume/outcomes.txt).

| 변경 파일 | 변경 내용 |
|---|---|
| `engine/client/ClientShim.cpp` | 비정상 숫자·상태 조회 실패·증폭 비지원/거절을 쓰기 전에 반환. 실장치 오류 뒤 후속 쓰기 중단. 가상장치와 음소거 해제 실패를 전파 |
| `engine/client/include/roomcut_client.h` | 0~2 유효 볼륨, 엔진 상태 필요, 단계별 실패·부분 적용·비동기 HAL 반영 계약 명시 |
| `engine/tests/test_client_volume.cpp` | 실제 C API와 Mach를 통한 실패·정상·비지원 경로 회귀 검사 |
| `core/tests/CMakeLists.txt` | 호스트 검사 연결. CoreAudio 대신 모의 HAL 사용 |

속성 질의 실패와 볼륨 기능 비지원은 구분합니다. 속성이 없는 장치까지 오류로 취급한 첫 수정은
[추가한 회귀 검사에서 실패](raw/2026-09-08-client-volume/fallback-before.txt)했습니다.
속성 유무를 먼저 확인하도록 고쳐 기존 소프트웨어 볼륨 경로를 유지했습니다.

검증한 경계:

- 증폭 거절과 응답 소실은 장치 쓰기로 이어지지 않으며 전송 재시도는 기존 최대 두 번입니다.
- 실장치 볼륨·가상장치 볼륨·음소거 해제·속성 질의의 실패를 성공으로 숨기지 않습니다.
- 음소거가 이미 꺼진 읽기 전용 제어에는 불필요한 해제 명령을 보내지 않습니다.
- 하드웨어 마스터 볼륨이 없거나 변경 불가능하면 가상장치의 소프트웨어 볼륨 경로를 유지합니다.
- 가상장치가 없는 기존 실장치 단독 경로와 미선택 상태의 가상장치 단독 경로를 유지합니다.
- 선택한 장치가 열거에서 사라졌다면 성공으로 반환하지 않습니다.
- 구버전 엔진의 일반 볼륨은 유지하고 지원하지 않는 100% 초과 요청은 거절합니다.
- 0·0.6·1·1.5·2와 유한 범위 밖 입력의 제한 및 C API 읽기 왕복을 검사합니다.
- NaN·양/음 무한대는 -3을 반환하며 장치 볼륨을 올리지 않습니다.
- 상태를 읽지 못해 기존 증폭을 알 수 없으면 장치 쓰기를 시작하지 않습니다.

## 실행 검사

| 검사 | 결과 | 자료 |
|---|---|---|
| `cmake -S . -B build`, `cmake --build build -j 4` | 성공 | [네이티브 빌드](raw/2026-09-08-client-volume/native-build.txt) |
| `ctest --test-dir build --output-on-failure` | 36/36 통과 | [전체 호스트 검사](raw/2026-09-08-client-volume/ctest.txt) |
| `DEVELOPER_DIR=/Applications/Xcode.app/Contents/Developer swift test -c release --jobs 4` | 172/172 통과 | [전체 Swift 검사](raw/2026-09-08-client-volume/swift-tests.txt) |
| 별도 Debug 경로의 ASan·UBSan `client_volume` | 통과 | [계측 검사](raw/2026-09-08-client-volume/asan-after.txt) |
| `DEVELOPER_DIR=/Applications/Xcode.app/Contents/Developer bash scripts/build-app.sh release` | 최신 앱 조립 성공 | [앱 빌드](raw/2026-09-08-client-volume/app-build.txt) |
| `codesign --verify --deep --strict build/Roomcut.app` | 종료값 0 | 로컬 서명 무결성 검사이며 공증·배포 검증 아님 |

ASan·UBSan 구성:

```sh
cmake -S . -B build/client-volume-audit/asan -DROOMCUT_BUILD_TESTS=ON -DCMAKE_BUILD_TYPE=Debug \
  '-DCMAKE_CXX_FLAGS=-fsanitize=address,undefined -fno-omit-frame-pointer' \
  '-DCMAKE_C_FLAGS=-fsanitize=address,undefined -fno-omit-frame-pointer' \
  '-DCMAKE_EXE_LINKER_FLAGS=-fsanitize=address,undefined'
cmake --build build/client-volume-audit/asan --target test_client_volume -j 4
ASAN_OPTIONS=detect_leaks=0 UBSAN_OPTIONS=halt_on_error=1 \
  ctest --test-dir build/client-volume-audit/asan -R '^client_volume$' -V
```

첫 계측 실행은 `detect_leaks=1`을 지정해
`AddressSanitizer: detect_leaks is not supported on this platform.` 오류로 시작 단계에서 중단됐습니다.
[중단 로그](raw/2026-09-08-client-volume/asan-test.txt)를 보존했습니다. 이 환경에서 누수 검증까지 통과했다고 주장하지 않습니다.
최초 새 대상 빌드에서 CMake 재구성 전 `No rule to make target`이 발생했으며 재구성 후 정상 빌드했습니다.

## 반환값과 한계

증폭이 이미 적용된 다음 하드웨어 쓰기가 실패하거나 실장치 쓰기 다음 가상장치 쓰기가 실패하면
앞 단계의 변경이 남습니다. 검사도 그 실제 부분 상태를 확인하며 자동 복구했다고 주장하지 않습니다.
호출자는 오류를 표시하고 권위 있는 상태를 다시 읽어야 합니다. 기존 앱의 쓰기 완료·실패 후 조회 경로를 유지했습니다.

[Apple의 쓰기 API 문서](https://developer.apple.com/documentation/coreaudio/audioobjectsetpropertydata(_:_:_:_:_:_:))와
[속성 쓰기 가능 여부 문서](https://developer.apple.com/documentation/coreaudio/audioobjectispropertysettable(_:_:_:))를 2026-09-08 확인했습니다.
반환값 0은 필요한 쓰기 요청이 수락됐다는 의미이며 HAL 반영 완료·여러 장치의 원자 적용을 뜻하지 않습니다.
실기기 비동기 적용, 엔진 볼륨 미러와 외부 장치 변경의 경쟁, 청취·장시간 재생은 이번 검사 범위 밖입니다.
UI 파일은 이번 C 수정에서 변경하지 않았습니다. 앞선 [미터 작업의 평가용 UI 검사](2026-09-08-meter-polling.md)와 구분합니다.

다음은 IO-02의 기본 출력 반환값·형식·밸런스 부분 실패 검사입니다. 전체 개발 목표는 활성 상태입니다.
