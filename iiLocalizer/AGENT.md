# iiLocalizer 에이전트 지침

현재 구현은 UTF-8 PO 단수 메시지의 조회, 치환 인자와 레이블 사용 위치 선택,
C ABI, Python API 및 LVUI 레이블 소비 예제를 제공한다. 전체 현지화 계약은 목표이며 복수형 등 미구현
기능을 완료된 것으로 해석하지 않는다. [문서 색인](docs/README.md),
[현재 구현](docs/Implementation.md), [현지화 계약](docs/LocalizationContract.md),
[PO 작업 흐름](docs/POWorkflow.md)을 읽는다. `docs/agent-index.json`은 Markdown
원문과 대응 JSON 및 원문 해시를 가리킨다. Markdown이 원본이며 해시가 다르면
원문을 우선하고 JSON을 갱신한다.

- Workspace와 SDK의 상위 `AGENT.md`를 따른다. 다른 SDK, Qt 및 KDE에 직접
  의존하지 않는다. 호스트가 다른 SDK와 이 모듈을 인터페이스로 결합한다.
- 현재 PO 지원 범위와 거부하는 입력은 구현 문서를 따른다. 복수형을 비롯한
  미구현 계약은 명세·실패하는 테스트를 먼저 갖춘 뒤 구현한다.
- 실제 제품 UI, 언어 선택, 파일 저장소 및 서식화는 호스트의 책임이다.
  `example/LVUILabel`은 앱 소유 어댑터로 두 SDK를 조합하며, 라이브러리의
  CMake 타깃과 공개 헤더에 다른 SDK를 추가하지 않는다.
- 소스 변경 시 `build/`에서 빌드와 관련 자동 테스트를 실행한다. LVUI 소비자
  검사의 빌드 산출물은 `build/lvui-label/`, 설치 검증은 `build/stage/`에 둔다.
  실제 앱 실행 검증은 별도 명시적 요청이 있을 때만 한다.
- `src/`에 구현 및 같은 이름의 헤더를 함께 둔다. 정적 시험 입력과 자원은
  `resources/`에 두며 `build/`를 필수 입력의 원본으로 사용하지 않는다.
- 인간용 Markdown을 수정하면 짝을 이루는 구조화 JSON과 색인을 함께 갱신하고
  원문 해시·줄 범위·링크를 확인한다. 타인의 라이선스와 번역 자료는 별도로
  확인하며, 출처 확인 없이 가져오지 않는다.
