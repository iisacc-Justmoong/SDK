# iiLocalizer 문서 색인

현재 구현은 UTF-8 PO 단수 메시지, 위치 인자 치환과 LVUI 레이블 소비 경로이다.
전체 현지화 계약에서 복수형 등은 아직 목표이다.

- [현재 구현과 검증](Implementation.md): 사용 방법, 제약, 빌드 및 테스트이다.
- [LVUI 레이블 치환](LVUILabelIntegration.md): PO 번역과 위치 인자가 실제
  네이티브 레이블에 전달되는 경로와 검증 범위이다.
- [현지화 계약](LocalizationContract.md): 모듈의 장기 목표, 인터페이스 경계와
  후속 구현의 검증 기준이다.
- [PO 작업 흐름](POWorkflow.md): GNU gettext 및 KDE를 참고한 번역 자료 형식,
  예시, 번역·검증·배포 흐름과 호환 범위이다.
- [에이전트 문서 색인](agent-index.json): 원문과 구조화 JSON의 경로·해시이다.

각 Markdown의 같은 이름인 `.agent.json`은 원문의 구조화된 투영이다. 루트
진입 문서의 투영은 `Entrypoints/`에 있다.
