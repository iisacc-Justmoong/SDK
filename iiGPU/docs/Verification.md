# 플랫폼과 검증

## 장치 세션과 반복 검색 제거 (2026-11-06)

macOS arm64 전체 개발 빌드와 실제 GPU를 사용하지 않는 관련 검사 14건이 통과했다.
가짜 장치에서 동일한 준비 구성의 반복 기록, 다른 상수, 장벽 누락, 다른 장치와
세션 토큰, 슬롯 재사용, 완료되지 않은 명령의 자원 소유, 실패 후 재시도, 순서가
다른 완료와 동시 수집을 검사했다. WebGPU 범위 색인은 10,000개의 결정적 질의를
개별 슬라이스 기준 판정과 대조하고 겹침·전체 범위·정수 상한을 확인했다.

세션과 공통 제출 코드를 계측한 ThreadSanitizer 및 ASan·UBSan 검사와 WebGPU 범위
색인의 ASan·UBSan 검사가 통과했다. 임시 설치본의 기본 예제 컴파일, 독립 Runtime
소비자, iiSDKCore 조합 소비자 및 WebGPU 메모리 소비자도 확인했다. 이 검사는 가짜
장치와 메모리 계약을 사용한다. iOS arm64 공통 소스는 구문 검사했으며 실제 플랫폼
GPU 실행, 표시 결과와 성능 향상은 측정하지 않았다. 시스템 설치본은 변경하지 않았다.

## GPU 명령 전용 스레드 (2026-11-06)

macOS arm64에서 iiGPU 전체 개발 빌드와 장치 없는 관련 검사 12건이 통과했다.
전용 스레드 검사는 고정 스레드 식별, 큐 포화와 FIFO, 다중 생산자, 예외,
종료 중 재진입, 접수된 작업의 드레인과 캡처 반환을 확인한다. 임시 설치한
iiGPU와 현재 iiSDKCore를 앱 소비자가 독립적으로 링크해 CPU 워커 두 개가 바쁠 때도
별도 GPU 명령 스레드가 실행되고 Runtime 종료 때 함께 정리되는지 확인했다.
전용 스레드 단위 검사는 macOS ThreadSanitizer에서도 통과했다.
전용 스레드 소스는 iOS arm64 대상으로 구문 검사했다. 현재 설치된 Android NDK 26은
기존 `GPU.h`가 요구하는 표준 `<stop_token>` 헤더가 없어 구문 검사를 완료하지 못했다.
해당 소비자의 실제 GPU 제출 경로도 전용 스레드를 사용하도록 컴파일했지만,
이번 검증에서는 가짜 완료만 실행했다. 실제 GPU 제출·화면·성능은 검증하지 않았다.

## 대상과 증거의 구분

| 플랫폼 | 구현 경로 | 현재 검증 범위 |
| --- | --- | --- |
| macOS | Metal, 선택적 Vulkan/MoltenVK | Apple GPU의 네이티브 전송·정수 커널·동기화 실행 |
| iOS | Metal | Apple SDK 교차 컴파일 대상이며 실기기 실행은 별도 검증 |
| Windows | Vulkan 1.1 | 같은 C++23 소스·공개 API, 실기기 실행 미검증 |
| Linux | Vulkan 1.1 | 같은 C++23 소스·공개 API, 실기기 실행 미검증 |
| Android | Vulkan 1.1 | NDK 교차 컴파일 대상, 이번 모듈의 실기기 실행 미검증 |
| Web/WASM | 없음 | 장치 생성은 unsupported, WebGPU 지원을 선언하지 않음 |

CPU·소프트웨어 Vulkan은 `require_hardware=false`로 명시한 경우에만 선택할 수
있으며 하드웨어 검증으로 집계하지 않는다. 이번 모듈의 검증을 이전 Android
Hello World 실행 검증과 합치지 않는다.

## 재현

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel 4
ctest --test-dir build --output-on-failure
build/iiGPUAcceleration metal
build/iiGPUAcceleration vulkan
cmake --install build --prefix "$PWD/build/prefix"
cmake -S tests/consumer -B build/consumer \
  -DiiGPU_DIR="$PWD/build/prefix/lib/cmake/iiGPU"
cmake --build build/consumer --parallel 4
ctest --test-dir build/consumer --output-on-failure
```

Vulkan을 찾지 못하면 실제 설치된 헤더·로더를 명시한다. macOS에서 MoltenVK를
사용하면 소비자 구성에도 같은 Vulkan 패키지가 필요하다. 동적 로더의 의존성은
`DYLD_LIBRARY_PATH`로 숨기지 않고 설치된 CMake 패키지에서 전달한다.

검사는 창을 생성하지 않는다. 유닛 검사는 장치 소유권·범위·상태·예산·취소·
실행 중 메모리 접근, 전송 왕복, 정수 커널 결과, 장벽 누락, 자원 수명,
다중 생산자 제출과 네이티브 핸들 수명을 검사한다. 완료 관측 단위 검사는
가짜 호출자 스케줄러를 사용해 단일 완료 전달·관측 취소·재진입·동시 취소와
루프 용량 제한을 검사한다. Core와의 조합 검사는 별도 소비자 애플리케이션에서
두 패키지를 명시적으로 찾고 링크해 수행한다.
장치 부재는 77로 skip하고 네이티브 실행 오류를 skip으로 처리하지 않는다.

셰이더 원문은 `example/Acceleration/transform.comp`이고 생성된 `Shader.h`에
원문 SHA-256을 기록한다. 갱신은 `tools/compile_shader.py --compiler <glslang>`을
사용한다. 생성 중간 파일은 build/shaders/에 둔다.

## 수행 기록

최종 실행 기록·CTest XML·설치 소비자 결과·소스 해시는 build/verification.json과
해당 로그에 기록한다. GPU 발견 목록만으로 커널 실행이나 성능 개선을 증명하지
않는다. 이 예제의 검증은 입력마다 `value * 7 + 13`의 정확한 결과가 나오는지
비교하는 기능 검증이며 CPU 대비 속도 개선이나 모든 GPU 제조사 호환성의
증거로 사용하지 않는다.

## 2026-10-28 의존성 경계 변경 검증

Core 패키지 검색을 끈 구성에서 완료 관측·경계·오프라인 WebGPU 부트스트랩
검사를 확인했다. 최초의 3개 검사 실행에서 완료 관측이 기존 30초 제한으로
시간 초과되었고, 같은 제한으로 재실행한 검사와 이후 5회 반복은 통과했다.
최초 지연의 원인은 확인되지 않았으므로 재실행 성공으로 지연을 해결했다고
간주하지 않는다. GPU 작업은 제출하지 않았다.

처음에는 캐시가 `iiDecision/build/dependencies/MoltenVK` 안의 제3자 Vulkan
헤더와 로더를 사용했다. 이후 로더·헤더와 Apache 라이선스를 iiGPU의
`build/sdk-vulkan-isolated-20261028`에 복사해 해당 경로를 명시하고 Metal,
Vulkan, WebGPU를 활성화한 채 다시 빌드했다. 복사한 로더와 대표 헤더는 원본과
바이트 단위로 비교했다. 이 복사본은 빌드 검증용이며 설치된 Vulkan SDK나 실제
장치 실행을 증명하지 않는다. 설치 소비자는 독립된 Vulkan 헤더와 로더를
별도로 제공해야 한다.

같은 분리 경로에서 Core 검색을 끈 설치 소비자, Core를 명시적으로 조합하는
소비자, iiDecision GPU 어댑터와 그 검사 실행 파일을 다시 컴파일했다. 이
소비자와 장치 검사 실행 파일은 실행하지 않았다.

## 2026-10-27 macOS ARM64 실행 결과

Apple M1 Max에서 Metal과 Vulkan/MoltenVK가 모두 hardware=true로 표시되었고
각각 257개 정수의 `value * 7 + 13` 결과를 비교했다. 첫 값 13, 마지막 값
1805가 일치했다. 테스트·소비자 실행은 창을 생성하지 않았다.

| 구성 | 통과 | skip | 실패 |
| --- | --- | --- | --- |
| iiGPU 정적 + Core 연결 | 15 | 0 | 0 |
| iiGPU 공유 | 14 | 0 | 0 |
| iiGPU ASan + UBSan | 14 | 0 | 0 |
| 백엔드 비활성 구성 | 3 | 12 | 0 |
| iiDecision Metal·Vulkan 구성 | 32 | 12 | 0 |
| iiDecision CPU 전용 구성 | 28 | 16 | 0 |
| iiGPU 설치 소비자 | 3 | 0 | 0 |
| Core 연결 설치 소비자 | 1 | 0 | 0 |
| 다른 경로로 옮긴 공유 iiGPU 소비자 | 3 | 0 | 0 |
| 정적 iiDecision 설치 소비자 | 3 | 0 | 0 |
| 공유 iiDecision 설치 소비자 | 3 | 0 | 0 |

구성별로 겹치는 검사가 있으므로 위 숫자를 합쳐 고유 검사 수로 해석하지
않는다. iiDecision의 Laya·MLX·OpenCL·ONNX는 이 구성에서 명시적으로 OFF로
두었고 해당 skip을 GPU 실행 성공으로 포함하지 않았다. Metal·Vulkan과
버퍼 재사용 검사는 모두 실행했다. 기존 Decimal oracle과 확률 검사도 통과했다.

실제 설치 헤더와 라이브러리로 구성한 소비자는 원본 iiGPU/src/를 참조하지
않았다. 공유 패키지는 다른 prefix로 복사해 재구성했고 동적 라이브러리 검색
우회 환경 변수를 제거한 채 커널을 실행했다. 기본 iiGPU 소비자가 Core
런타임을 import하지 않는 것도 확인했다.

NDK 30, Android API 26, arm64-v8a의 Vulkan 정적 라이브러리와 iOS 18 이상,
arm64의 Metal 정적 라이브러리가 교차 빌드되었다. Core 완료 어댑터는 이 두
교차 빌드에 포함하지 않았다. Android 런타임에는 Vulkan 1.1 드라이버가 별도로
필요하다. Windows·Linux 및 모바일의 실제 GPU 실행은 이번 검증 범위 밖이다.

추가한 retirement 검사는 성공·실패 제출 핸들 128개를 보관한 상태에서도
네이티브 작업 자원과 장치 추적 항목이 0개로 반환되는지 검사한다.

## 기본 빌드 대상

새 `build/` 구성과 `install.sh`는 모듈과 테스트를 빌드한다. 예제는
`-DIIGPU_BUILD_EXAMPLES=ON`으로 별도 활성화하고, 라이브러리만 필요한
경우 `-DBUILD_TESTING=OFF`를 지정한다. 기존 검증 기록의 경로는 당시의
이력이며 현재 산출물 보관 위치를 보장하지 않는다.

검증에 사용한 로컬 MoltenVK 입력은 `resources/vulkan/macos-isolated/`에
보관한다. 명시적으로 사용할 때 `Vulkan_INCLUDE_DIR`과 `Vulkan_LIBRARY`를
각각 이 디렉터리의 `include/`와 `lib/libMoltenVK.dylib`로 지정한다.
기존 검증 스크립트와 요약은 `resources/.recovery/verification-20261030/`에
보존했으며 과거 빌드 경로를 참조하므로 그대로 재실행할 수는 없다.

현재 설치 소비자 검사는 임시 빌드를 검증 후 삭제하고 별도 JSON 로그를
`build/`에 남기지 않는다. 실패 원인은 테스트 출력에서 확인한다.

## 비동기 GPU 완료와 CPU 조합 (2026-11-06)

macOS arm64 Release에서 iiGPU의 전체 대상을 빌드하고 하드웨어를 사용하지 않는
11개 CTest가 통과했다. 추가한 완료 서비스와 제출 자원 반환 시험은 각각 20회 반복해
통과했다. 독립된 대기 워커의 동시 진행, 큐 포화, 콜백만 취소하는 계약, 캡처 파괴
장벽, 종료 중 재진입, 오류 기록, 동시 poll과 native handle 보유를 가짜 완료로 검사했다.

Core와 GPU 패키지를 임시 설치 후 다른 경로로 옮겨 앱 소비자에서 함께 링크했다.
기본 소비자 시험은 CPU Runtime과 EventLoop 사이에서 가짜 완료를 전달한다. 동일한
소비자의 명시적 `--hardware` 경로는 기존 Metal·Vulkan 셰이더를 제출하도록 컴파일·
링크되었지만 이번에는 실행하지 않았다. 모듈 상호 의존성 검사는 통과했다.

CompletionService.cpp와 테스트, GPU.cpp와 반환 테스트를 각각 계측한 TSan 및
ASan·UBSan 검사가 통과했다. 나머지 네이티브 백엔드와 라이브러리는 계측하지 않은
부분 검사이다. iOS arm64의 공통 소스와 Metal 백엔드, WASM pthread 공통 소스는
문법만 검사했다. Android NDK 26은 기존 `<stop_token>`을 찾지 못해 교차 검사를
완료하지 못했고 Windows·Linux 및 물리 GPU의 이번 변경 실행은 검증하지 않았다.
시스템 설치본은 바꾸지 않았으며 가속률을 측정하지 않았다.

## 명시적 폴링과 이벤트 대기 (2026-11-06)

macOS arm64에서 모듈 전체 빌드와 장치 없이 실행하는 13개 검사가 통과했다.
가짜 WebGPU 완료는 세 네이티브 Future의 역순 신호, 기한 만료 후 재대기,
동일 Future의 동시 대기, 명시적 `poll`을 검사한다. 완료 관측 시험은 기존
API가 타이머를 등록하지 않고 오류를 반환하는지와 명시적 반복 요청만 동작하는지
검사한다. 실제 GPU 작업, GUI 앱 실행, 시스템 설치 및 다른 운영체제 실행은
이번 검증에 포함하지 않았다.
