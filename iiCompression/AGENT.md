# iiCompression 작업 지침

C++23 표준 값으로 메모리 압축 경계를 유지한다. Qt와 다른 iiSDK를 직접 찾거나
링크하지 않는다. zlib는 gzip 구현에만 사용하고 앱이 파일 I/O와 SDK 조합을 맡는다.
헤더와 소스는 함께 `src/iiCompression/`에 둔다. 빌드·테스트·설치 검증은 `build/`
아래에서 수행하고, 코드 변경 시 테스트와 사람용·에이전트용 문서를 함께 갱신한다.
기존 파일과 다른 작업을 보존한다. README는 영어, docs 문서는 한국어로 유지한다.

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
python3 ../../Engineering/WorkspaceDocumentation/App/workspace_docs.py --workspace ../.. sync --project SDK/iiCompression
```
<!-- workspace-docs:agent:end -->
