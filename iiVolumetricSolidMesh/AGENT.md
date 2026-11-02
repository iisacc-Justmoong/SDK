# iiVolumetricSolidMesh 에이전트 지침

- CAD 원본, 사면체 메시, 입자 가중치, 시뮬레이션 상태를 분리한다. API와 단위 및 검증 범위는 `docs/architecture.md`를 따른다.
- 내장 CAD 경로는 파라미터 상자뿐이다. 임의 CAD 테트라화와 FEM/MPM 해석기는 앱이 인터페이스로 공급하며 실제 해석이 구현된 것으로 간주하지 않는다.
- 외부 테트라 메시의 전역적인 중첩·체적 커버리지는 검사하지 않는다. 입력 제공자가 정합 비중첩 체적을 보장해야 한다.
- 다른 iiSDK 모듈을 직접 링크하지 않고 앱이 별도로 결합한다. 파일 생성·저장은 앱의 iiFileProvider 경계를 따른다.

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
python3 ../../Engineering/WorkspaceDocumentation/App/workspace_docs.py --workspace ../.. sync --project SDK/iiVolumetricSolidMesh
```
<!-- workspace-docs:agent:end -->
