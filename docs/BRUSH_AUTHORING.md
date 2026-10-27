# 브러시 작성 계층과 공급

전체 획의 비율에 따른 Start·End 크기 테이퍼와 사후 보정은 끝까지 받은 기하 정보가 필요하다. iiPaintEngine의 기본 엔진은 현재 점을 즉시 픽셀화하며 입력 궤적·미래 위치·replay를 유지하지 않는다. iiSharedCanvas는 결과 픽셀과 문서 의미를 소유한다. 이 경계를 보존하면서 상위 작성 동작은 `iiPaintEngine::BrushTools` 선택 구성요소가 소유한다.

의존성은 소비자 → BrushTools 구성요소 → iiPaintEngine 기본 엔진이다. 소비자는 결과 픽셀 타일을 iiSharedCanvas에 하나의 문서 편집으로 커밋한다. 하위 엔진과 문서는 BrushTools를 참조하지 않는다. 원시 표본은 호출자 소유의 임시 작성 값이며 문서·Undo·설정에 직렬화하지 않는다. 입력·시계·미리보기·문서 적용은 소비자가 조합한다.

브러시 작성 소스는 iiPaintEngine 저장소의 `src/iiBrushTools/`에 있다. 기존 네임스페이스와 헤더 및 `iiBrushTools::iiBrushTools` 설치 패키지는 호환된다. 새 소비자는 `find_package(iiPaintEngine COMPONENTS BrushTools)`와 `iiPaintEngine::BrushTools`를 사용한다.

설치기는 iiPaintEngine 하나를 찾는다. `IIPAINTENGINE_BUILD_BRUSH_TOOLS=OFF`는 작성 기능을 제외한다. 라이브러리 헤더·소스는 함께 관리하고 출력은 모듈 `build/` 아래에 둔다.

공개 API·단위·활성 정책·픽셀 출력은 [브러시 작성 계약](../iiPaintEngine/docs/BrushTools.md)에 있다. 엔진 CTest와 별도 설치 소비자 실행은 각각 검증한다.
