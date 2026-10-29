# iiGPU 작업 지침

- C++23의 공개 API를 사용한다. 헤더와 구현은 src/ 및 책임별 하위 디렉터리에 함께 둔다.
- iiGPU는 GPU 장치·버퍼·컴퓨트 파이프라인·명령·동기화를 소유한다. 위젯, 윈도, 모델, 확률 계산은 상위 모듈이 소유한다.
- WebGPU는 선택적 iiGPU::WebGPU 구성요소이며, 기존 iiWebGPU 공개 이름은 호환 대상으로 유지한다. Dawn은 resources/dawn 또는 명시된 대상별 패키지에서 가져온다.
- iiGPU의 모든 구성요소는 다른 SDK 모듈을 참조하지 않는다. 선택적 iiGPU::Runtime은 호출자가 제공한 스케줄러로 완료를 관측한다. 애플리케이션이 Core EventLoop 등과 조합하고 필요한 라이브러리를 직접 링크한다.
- Dawn 준비 코드는 저장 계약을 인자로 받는다. Workspace 소비자 유틸리티가 iiFileProvider를 공급하며 리소스를 생성한다.
- 공개 API에는 Qt 및 Metal·Vulkan의 헤더나 타입을 노출하지 않는다. 네이티브 접근 콜백은 빌린 핸들의 수명과 동기화를 호출자에게 명시한다.
- CPU·소프트웨어 GPU를 하드웨어 실행으로 표시하지 않는다. 미지원 기능, 장치 손실, 예산 초과는 오류로 돌려준다.
- 실제 GPU 제출 이후의 취소나 선점을 보장하지 않는다. 마지막 제출 핸들의 파괴는 GPU 자원 안전성을 위해 완료를 기다린다.
- 빌드와 검증 산출물은 build/ 안에 둔다. 사용자 앱의 창을 테스트 도중 반복 실행하지 않는다.
- 소스 변경 시 tests/와 docs/를 함께 갱신하고 빌드·테스트한다. README.md는 영어, docs/는 한국어로 작성한다.
- docs/agent-index.json과 Markdown에서 파생한 JSON을 함께 유지한다.

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
python3 ../../Engineering/WorkspaceDocumentation/App/workspace_docs.py --workspace ../.. sync --project SDK/iiGPU
```
<!-- workspace-docs:agent:end -->
