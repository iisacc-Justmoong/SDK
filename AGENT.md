# SDK 에이전트 지침

필요한 실행 입력과 정적 리소스는 프로젝트의 `resources/`에 둔다. `build/`는 생성된 빌드·시험·설치 검증 산출물용이며, 별도의 필수 입력 저장소로 사용하지 않는다.

## SDK 간 독립성

SDK 모듈은 다른 SDK 모듈을 찾거나 링크하거나 그 공개 타입에 의존하지 않는다.
앱이 각 SDK를 별도로 소비하고 인터페이스·콜백으로 결합한다. 파일 생성의
책임은 iiFileProvider에 남기고, 다른 SDK가 직접 의존하지 않도록 앱이 공급하는
저장 인터페이스로 연결한다. CTest·GNUInstallDirs 같은 일반 CMake 기능은 허용한다.
전환 중인 실제 참조와 점검의 한계는 [SDKIndependence.md](docs/Architecture/SDKIndependence.md)를 따른다.

## 모듈 구성과 개발 표준

iiSDK의 최우선 과제는 단일 코드베이스로 모든 크로스 플랫폼 애플리케이션을
네이티브로 구축하는 것이며, 장기적으로 Qt 의존성을 제거한다. 전환 현황과 점검
범위는 [QtIndependence.md](docs/Migrations/QtIndependence.md)를 따른다.

각 모듈은 `src/`, `example/`, `docs/`, `tests/`, `cmake/`, `resources/`,
`README.md`, `AGENT.md`, `LICENSE`, `.gitignore`, `install.sh` 또는 `install.py`,
`CMakeLists.txt`를 갖춘다. 헤더 배치는 [HeaderLayout.md](docs/Architecture/HeaderLayout.md)를 따른다.

각 모듈은 앱에서 사용하는 API, 다른 모듈과 병행하기 위한 인터페이스와 Python API를
제공한다. 여기서 병행은 앱이 독립 인터페이스를 조합하는 방식이며 SDK 간 직접 의존을
허용하지 않는다. 일반 의존 방향 규칙보다 SDK 간 독립성이 더 엄격하므로 이를 우선한다.

GUI를 그리는 모듈은 LVUI로 iiGUI 네이티브 위젯을 그린다. `LVRS/`는 레거시이며 새 뷰에
사용하지 않는다. 기술 스택, TDD 순서, 설계 원칙과 디자인 규칙은 상위
[Workspace 지침](../AGENT.md)과 [개발·디자인 표준](../Docs/Company/iisacc_dev_standard_2026-11-18.md)을 따른다.

## 반응형 런타임

iiSDK와 이를 사용하는 모든 앱은 이벤트와 쿼리로 반응한다. 전체 상태 로딩과
뷰 구성은 데이터 범위의 초기화에서 수행한다. 이후에는 변경된 자원·필드와
리비전을 수명에 묶인 이벤트로 전달하고 필요한 범위만 갱신한다. 이벤트가
누락되면 해당 자원의 리비전부터 복구하고, 원본 연결이 교체되면 새 수명으로 초기화한다.
명시적으로 요청한 폴링, 실제 기한·만료·재시도, 프로토콜 heartbeat와
활성 애니메이션·게임의 프레임 주기는 각각의 계약에 따라 허용한다. 모듈 간 직접
의존성을 추가하지 않으며 독립 인터페이스를 앱이 조합한다. 현재 구현과 명세의
점검 근거와 미해결 범위는 [반응형 런타임 감사](docs/Audits/ReactiveRuntimeAudit.md)를 따른다.

Linux GUI의 기본 대상은 직접 Wayland이다. X11은 폐기 예정이며 명시적 레거시
선택으로만 유지한다. Wayland 의존성이 없다는 이유로 X11에 자동 전환하지 않는다.

## 폐기한 추론 런타임

ONNX와 ONNX Runtime은 전량 폐기한 기술이다. 제품과 SDK의 빌드, 모델 실행,
설치 패키지와 배포 번들에 다시 추가하지 않는다. 기존 ONNX 모델을 암묵적으로
실행하거나 자동 다운로드하지 않는다. 폐기된 모델 경로는 명시적 미지원 결과를
반환하며, 새 기능은 iiSDK의 네이티브 인터페이스와 지원 중인 백엔드로 구현한다.

GGUF, safetensors and other supported non-ONNX model formats are permitted.
Format permission does not imply that every backend supports every architecture.

<!-- workspace-docs:agent:start -->
## 문서 구조와 에이전트 탐색

- 프로젝트 루트의 `README.md`는 사람용 영문 진입 문서이며 `AGENT.md`는 에이전트 진입점이다.
- 하위 인간용 Markdown과 구조화된 agent JSON을 함께 유지한다. Markdown이 원본이며 JSON은 원문 해시와 줄 범위를 가진 파생 문서이다.
- 먼저 [docs/agent-index.json](docs/agent-index.json)에서 관련 문서와 JSON 투영을 찾는다. 원문 SHA-256이 다르면 현재 Markdown을 읽고 투영을 갱신한다.
- JSON의 규칙 후보는 검색용 어휘 일치 결과이다. 과거 기록·예제·실패 상태를 현재 지시나 검증 완료로 자동 해석하지 않는다.
- 기존 프로젝트별 규칙, 동결된 증거, 라이선스, 외부 원본, 사용자 데이터와 다른 작업의 변경을 보존한다.
- 이 문서 재편 지침은 과거 작업의 기능 구현·배포를 자동 재개하라는 요청이 아니다. 현재 사용자의 작업 범위를 우선한다.
- 문서를 수정하면 JSON 쌍·색인을 함께 갱신하고 검증한다. 기존 소스 변경에 대한 빌드·테스트 규칙은 계속 따른다.

Workspace에서 이 프로젝트의 문서를 갱신하는 명령은 다음과 같다.

```sh
python3 ../Engineering/WorkspaceDocumentation/App/workspace_docs.py --workspace .. sync --project SDK
```
<!-- workspace-docs:agent:end -->
