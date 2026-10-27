# 플랫폼과 검증

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
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release \
  -DiiSDKCore_DIR="$HOME/.local/SDK/iiSDKCore/lib/cmake/iiSDKCore"
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
다중 생산자 제출과 네이티브 핸들 수명을 검사한다. Core 연결 검사는 같은 루프
실행 영역의 단일 완료 전달·관측 취소·루프 용량 제한을 검사한다.
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
