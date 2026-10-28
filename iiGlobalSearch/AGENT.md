# iiGlobalSearch 에이전트 지침

파일 색인과 FTS 질의는 iiFileProvider의 `FileSearchIndex`를 사용한다. Society 검증이나 웹 검색을 하위 제공자에 역참조시키지 않는다.

- C++23과 iiSDKCore Runtime, iiFileProvider, iiSocietyContainer의 공개 API를 사용한다. 공개 헤더와 구현은 `src/`에 함께 둔다.
- 작업 범위는 호출자가 선택한 정상 `.societycontainer` 하나이며, 숨김·제어 경로와 링크를 따라 외부로 이동하지 않는다. 원본이나 Provider 카탈로그를 수정하지 않는다.
- 색인은 메모리 스냅샷이다. 비동기 부분 결과, 취소, 명시적 새로고침, 건너뛴 항목과 검색 직전 경로 재검사의 계약은 `docs/Indexing.md`를 따른다.
- 웹 작업은 각 요청마다 독립된 큐와 방문 집합을 만들고 외부 검색 인덱스나 영속 검색 DB를 사용하지 않는다. iiNetwork, robots와 격리된 브라우저 렌더링 경계는 `docs/WebSearch.md`를 따른다.
- 구조화 검색 답변과 비동기 미디어 적재는 `docs/Answers.md`의 소유권·범위·한도를 따른다. 결과 파일을 자동 생성하지 않는다.
- 소스 변경에는 관련 테스트와 한국어 문서·JSON 쌍을 갱신한다. 구성·빌드·테스트·설치 검증 산출물은 `build/` 아래에 둔다.

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
python3 ../../Engineering/WorkspaceDocumentation/App/workspace_docs.py --workspace ../.. sync --project SDK/iiGlobalSearch
```
<!-- workspace-docs:agent:end -->
