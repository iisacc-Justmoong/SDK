# Congregation 글로벌 메뉴의 iiSDK 모듈 배정

기준일: 2026-11-25. [글로벌 메뉴 정의](../../../../Product/Congregation/App/Application/AppMenu.h)의 현재 작업 트리와 관련 SDK 소스를 정적으로 검토한 배정 제안이다. 앱을 실행하여 메뉴를 조작한 결과는 아니며, 설치된 빌드와 현재 작업 트리는 다를 수 있다.

현재 메뉴 정의의 비활성 명령 162개와 빈 하위 메뉴 49개, 합계 211개를 각각 한 번씩 주 담당 모듈에 배정한다. 기존 모듈에는 137개, 신규 제안 모듈에는 74개를 배정한다. Help의 `Help unavailable`은 기능이 아닌 상태 행이므로 211개에 포함하지 않고 별도로 기록한다. 메뉴 항목 뒤의 **†**는 하위 명령 없이 `No commands available`만 있는 분류이다. 분류의 세부 기능은 아직 정의되지 않았으므로 해당 배정은 잠정적이다.

기존 모듈에 배정했다는 것은 해당 모듈이 존재한다는 뜻이며, 그 명령의 API가 완성되었거나 Congregation에 연결되었다는 뜻은 아니다. 신규 모듈의 이름과 책임은 이번 배정을 위한 제안이며 실제 모듈을 생성하거나 구현한 것은 아니다.

주 담당 외에도 Congregation이 문서·선택·현재 프레임을 전달하고 명령, 변경 이력, LVUI 뷰모델, 파일 접근과 오류 처리를 연결한다. SDK 간 직접 참조를 새로 추가하지 않고 앱이 독립 인터페이스와 콜백을 조합한다. 파일 생성과 게시는 iiFileProvider를 사용한다. 이 원칙은 기존 구현의 모든 의존성 전환이 완료되었다는 주장이 아니다. [SDK 독립성 지침](../../../AGENT.md)

## 1. 기존 모듈에 배정한 기능

### iiSharedCanvas

문서·레이어·마스크와 저장할 선택 정보, 변경 이력 및 영상 편집 타임라인 모델을 담당한다. 필요하면 기존 API를 확장한다.

- **File > Place Linked…**
- **File > File Info…**
- **Edit > Toggle Last State**
- **Select > All Layers**
- **Select > Deselect Layers**
- **Select > Find Layers**
- **Select > Isolate Layers**
- **Select > Load Selection…**
- **Select > Save Selection…**
- **View > Pixel Aspect Ratio** †
- **Layer > Copy CSS**
- **Layer > Copy SVG**
- **Layer > Rename Layer…**
- **Layer > Layer Style** †
- **Layer > New Fill Layer** †
- **Layer > New Adjustment Layer** †
- **Layer > Layer Content Options…**
- **Layer > Layer Mask** †
- **Layer > Vector Mask** †
- **Layer > Video Layers** †
- **Layer > Rasterize** †
- **Layer > New Layer-Based Slice**
- **Layer > Group Layers**
- **Layer > Ungroup Layers**
- **Layer > Arrange** †
- **Layer > Combine Shapes** †
- **Layer > Align** †
- **Layer > Distribute** †
- **Layer > Lock Layers…**
- **Layer > Link Layers**
- **Layer > Select Linked Layers**
- **Layer > Merge Layers**
- **Layer > Merge Visible**
- **Layer > Flatten Image**
- **Image > Image Size…**
- **Image > Canvas Size…**
- **Image > Image Rotation** †
- **Image > Crop**
- **Image > Trim…**
- **Image > Reveal All**
- **Image > Duplicate…**
- **Image > Apply Image…**
- **Image > Calculations…**
- **Sequence > Sequence Settings…**
- **Sequence > Match Frame**
- **Sequence > Reverse Match Frame**
- **Sequence > Add Edit**
- **Sequence > Add Edit to All Tracks**
- **Sequence > Trim Edit**
- **Sequence > Extend Selected Edit to Playhead**
- **Sequence > Apply Video Transition**
- **Sequence > Apply Audio Transition**
- **Sequence > Apply Default Transitions to Selection**
- **Sequence > Lift**
- **Sequence > Extract**
- **Sequence > Close Gap**
- **Sequence > Go to Gap** †
- **Sequence > Linked Selection**
- **Sequence > Make Subsequence**
- **Sequence > Simplify Sequence…**
- **Sequence > Add Tracks…**
- **Sequence > Delete Tracks…**
- **Sequence > Captions** †
- **Sequence > Selection Follows Playhead**
- **Sequence > Show Through Edits**

### iiPaintEngine

비트맵 선택·변형·획·채우기와 브러시 프리셋의 계산을 담당한다.

- **Edit > Fade…**
- **Edit > Cut**
- **Edit > Paste Special** †
- **Edit > Clear**
- **Edit > Fill…**
- **Edit > Stroke…**
- **Edit > Free Transform**
- **Edit > Transform** †
- **Edit > Define Brush Preset…**
- **Select > All**
- **Select > Reselect**
- **Select > Select and Mask…**
- **Select > Modify** †
- **Select > Grow**
- **Select > Similar**
- **Select > Transform Selection**

### iiGraphicAdjust

이미지 조정·효과와 변형 알고리즘을 담당한다. 일부 명령은 새 알고리즘이나 앱 연결이 필요하다.

- **Edit > Content-Aware Fill…**
- **Edit > Content-Aware Scale**
- **Edit > Puppet Warp**
- **Edit > Perspective Warp**
- **Edit > Auto-Align Layers…**
- **Edit > Auto-Blend Layers…**
- **Edit > Sky Replacement…**
- **Select > Color Range…**
- **View > 32-bit Preview Options…**
- **Layer > Matting** †
- **Image > Adjustments** †
- **Image > Auto Tone**
- **Image > Auto Contrast**
- **Image > Auto Color**
- **Image > Analysis** †
- **Filter > Gaussian Blur**
- **Filter > Filter Gallery…**
- **Filter > Adaptive Wide Angle…**
- **Filter > Lens Correction…**
- **Filter > Liquify…**
- **Filter > Vanishing Point…**
- **Filter > Noise** †
- **Filter > Render** †
- **Filter > Video** †
- **Filter > Sharpen** †
- **Filter > Stylize** †
- **Filter > Distort** †
- **Filter > Pixelate** †
- **Filter > Blur** †
- **Filter > Blur Gallery** †
- **Filter > Other** †

### iiColorManager

문서 색상 모드, 프로파일·교정 설정과 색상 관리 계약을 담당한다. 임의의 ICC 프로파일 변환과 교정은 현재 제공 범위에 맞춰 확장해야 한다.

- **Edit > Color Settings…**
- **Edit > Assign Profile…**
- **Edit > Convert to Profile…**
- **View > Proof Setup** †
- **View > Proof Colors**
- **View > Gamut Warning**
- **Image > Mode** †

### iiVisualCodec

이미지·영상·Vision·공간 영상·3D 자산 및 CAD의 해당 구성요소를 담당한다. 기능별 모델, 알고리즘, 플랫폼 지원은 별도 확인이 필요하다.

- **File > Import** †
- **Select > Focus Area…** — `iiVisualCodec::OpenCV`
- **Select > Subject** — `iiVisualCodec::Vision`
- **Select > Sky** — `iiVisualCodec::Vision`
- **Select > New 3D Extrusion** — `iiVisualCodec::CAD`
- **Layer > Quick Export as JPG** — `iiVisualCodec::Image`
- **Layer > Export As…** — `iiVisualCodec::Image`
- **Layer > Mask All Objects** — `iiVisualCodec::Vision`
- **Sequence > Auto Reframe Sequence…** — `iiVisualCodec::Vision`
- **3D > New 3D Layer from File…** — `iiVisualCodec::Asset3D`
- **3D > Export 3D Layer…** — `iiVisualCodec::Asset3D`
- **3D > Spherical Panorama** † — `iiVisualCodec::Spatial`
- **3D > New 3D Extrusion from Selected Layer** — `iiVisualCodec::CAD`
- **3D > New 3D Extrusion from Selected Path** — `iiVisualCodec::CAD`
- **3D > New 3D Extrusion from Current Selection** — `iiVisualCodec::CAD`

### iiResource

정리할 자원의 식별과 계약을 담당한다. 실제 캐시 삭제는 자원을 소유한 모듈이 수행한다.

- **Edit > Purge** †

### iiNetwork

콘텐츠 다운로드의 네트워크 전송을 담당한다. 저장은 앱이 iiFileProvider와 연결한다.

- **3D > Download Content…**

### iiLocalDiffusion

사용자가 선택한 지원 백엔드를 통한 생성형 이미지 처리 인터페이스를 담당한다.

- **Filter > Generative filter**

### 활성화되어 있지만 불완전한 메뉴 경로

아래 항목은 위의 211개 비활성 명령·빈 하위 메뉴와 별도로 관리한다. 기능을 실제 수정할 때에는 주 담당 SDK뿐 아니라 Congregation의 연결도 함께 확인해야 한다.

- **iiSharedCanvas · iiGUI — Edit > Copy / Copy Merged:** 현재는 동일한 액션으로 연결되어 선택 영역과 선택 레이어를 사용하지 않고 프레임 0의 합성 이미지를 복사한다. 현재 프레임과 복사 대상을 구분해 생성하고 클립보드로 전달하도록 배정한다. [명령 연결](../../../../Product/Congregation/App/Application/AppMenu.h) · [복사 처리](../../../../Product/Congregation/App/Application/Application.cpp)
- **iiGUI — Edit > Undo / Redo / Copy / Copy Merged / Paste:** 텍스트 입력 위젯이 포커스를 유지한 채 메뉴 명령이 전달되면 현재 처리기는 명령을 텍스트 입력으로 전달하지 않고 반환한다. 앱이 입력 대상과 캔버스 대상을 구분하고 iiGUI가 해당 입력 대상으로 전달하는 경계를 담당한다. 키보드 입력 경로와 구분한다. [명령 처리](../../../../Product/Congregation/App/Application/Application.cpp)
- **iiSharedCanvas · iiVisualCodec — File > Export > Export…:** 현재 비트맵 내보내기는 프레임 0을 사용한다. Congregation이 현재 프레임을 지정하고 렌더와 인코딩을 연결하도록 배정한다. [내보내기 구현](../../../../Product/Congregation/App/Canvas/Artwork.cpp)
- **iiGUI · iiFileProvider — iPad의 File > Save As… 및 첫 Save:** 현재 명시적 파일명 저장 경로가 중단된다. 파일 선택과 저장 연결을 배정한다. 이미 저장 가능한 경로가 있는 Save는 별도 경로이다. [파일 대화상자 처리](../../../../Product/Congregation/App/Application/Application.cpp)
- **iiGUI · iiVisualCodec — iPad의 File > Export > Export…:** 현재 폴더 선택 뒤 `Canvas.png`라는 고정 이름을 사용한다. 파일명과 형식 선택 및 선택한 형식의 인코딩을 배정한다. [앱의 기본 파일명](../../../../Product/Congregation/App/Application/Application.cpp) · [UIKit 파일 선택기](../../../iiGUI/src/Platform/HostIOS.mm)
- **iiGUI — iPad·Android의 View > Screen Mode > Presentation 및 Window > Presentation:** 현재 앱은 상태를 변경한 뒤 호스트 전체 화면 전환을 요청하나, 해당 호스트는 전환을 지원하지 않는다. 앱이 플랫폼의 지원 범위에 따라 UI 상태와 창 동작을 조합하도록 배정한다. [앱 명령](../../../../Product/Congregation/App/Application/Application.cpp) · [iPad 호스트](../../../iiGUI/src/Platform/HostIOS.mm) · [Android 호스트](../../../iiGUI/src/Platform/HostAndroid.cpp)
- **iiGUI · iiFileProvider — Android의 New document…, Add canvas…, Open…, Save As…, 첫 Save, Export…, Place Embedded…, Image > Place image…, Edit > Preferences > Preferences…:** 현재 Android 호스트의 보조 창과 문서 선택기가 미지원이므로 이 경로를 완료할 수 없다. 플랫폼 화면·선택기와 파일 처리를 배정한다. 앱은 필요에 맞게 화면을 연결한다. [Android 호스트](../../../iiGUI/src/Platform/HostAndroid.cpp) · [앱의 창과 파일 연결](../../../../Product/Congregation/App/Application/Application.cpp)

## 2. 신규 제안 모듈에 배정한 기능

아래는 기존 작업 공간에서 해당 이름의 모듈을 확인하지 못해 새로 제안한 책임이다. 각 모듈이 독립 API를 제공하도록 설계하며, 실제 생성·구현·테스트·설치는 아직 이루어지지 않았다.

### iiDocumentSession

열린 문서 집합, 최근 문서와 저장·닫기 절차의 공통 계약을 담당한다.

- **File > Open As…**
- **File > Open Recent** †
- **File > Close**
- **File > Close All**
- **File > Close Others**
- **File > Save a Copy…**
- **File > Revert**

### iiAutomation

명령 시퀀스, 일괄 실행과 교체 가능한 스크립트 실행 인터페이스를 담당한다.

- **File > Automate** †
- **File > Scripts** †

### iiPrint

2D 인쇄 설정, 출력 작업과 인쇄 준비를 담당한다.

- **File > Print…**
- **File > Print One Copy**
- **Image > Trap…**

### iiCommandRegistry

명령 검색, 단축키와 메뉴·도구 모음 구성을 담당한다.

- **Edit > Search** — 명령 검색으로 잠정 해석; 문서 텍스트 검색은 iiTextServices에 배정
- **Edit > Keyboard Shortcuts…**
- **Edit > Menus…**
- **Edit > Toolbar…**

### iiTextServices

맞춤법 검사와 텍스트 검색·치환을 담당한다.

- **Edit > Check Spelling…**
- **Edit > Find and Replace Text…**

### iiPresetManager

종류에 종속되지 않는 프리셋 식별, 분류와 버전 관리를 담당한다.

- **Edit > Define Pattern…**
- **Edit > Define Custom Shape…**
- **Edit > Presets** †

### iiViewport

캔버스·타임라인의 좌표 변환, 배율, 표시 범위와 스냅 계산을 담당한다.

- **View > Pixel Aspect Ratio Correction**
- **View > Fit Layer on Screen**
- **View > Fit Artboard on Screen**
- **View > 100%**
- **View > 200%**
- **View > Print Size**
- **View > Actual Size**
- **View > Flip Horizontal**
- **View > Pattern Preview**
- **View > Extras**
- **View > Show** †
- **View > Snap**
- **View > Snap To** †
- **View > Guides** †
- **View > Lock Slices**
- **View > Clear Slices**
- **Sequence > Zoom In**
- **Sequence > Zoom Out**
- **Sequence > Snap in Timeline**

### iiDocumentVariables

문서 변수, 데이터 집합과 반복 출력용 값 결합을 담당한다.

- **Image > Variables** †
- **Image > Apply Data Set…**

### iiTimelineRender

시퀀스의 범위별 렌더 작업과 렌더 결과 관리를 담당한다.

- **Sequence > Render Effects in Work Area**
- **Sequence > Render Entire Work Area**
- **Sequence > Render Selection**
- **Sequence > Delete Render Files**
- **Sequence > Delete Work Area Render Files**

### iiAudioProcessing

오디오 믹싱, 전환 처리와 정규화를 담당한다.

- **Sequence > Render Audio**
- **Sequence > Normalize Mix Track…**

### iiSpeechRecognition

교체 가능한 음성 인식 백엔드와 시간 정보가 있는 전사 결과를 담당한다.

- **Sequence > Transcribe Sequence…**

### ii3DScene

장면 객체, 계층, 배치와 표면 페인팅 대상을 담당한다.

- **3D > Merge 3D Layers**
- **3D > Group Objects**
- **3D > Group All Objects in Scene**
- **3D > Move Objects to Ground Plane**
- **3D > Pack Objects on Ground Plane**
- **3D > New Tiled Painting from Layer**
- **3D > Paint Falloff…**
- **3D > Paint System** †
- **3D > Paint on Target Texture** †
- **3D > Select Paintable Areas**
- **3D > Create Painting Overlay** †
- **3D > Sketch with Current Brush**

### iiMeshProcessing

메시 생성·편집, UV, 단면과 단순화를 담당한다.

- **3D > New Mesh from Layer** †
- **3D > Generate UVs…**
- **3D > Split Extrusion**
- **3D > Apply Cross Section to Scene**
- **3D > Unify Scene for 3D Printing**
- **3D > Simplify Mesh…**
- **3D > Make Work Path from 3D Layer**

### ii3DRender

장면 렌더링과 렌더 결과 생성을 담당한다.

- **Filter > 3D** †
- **3D > Render 3D Layer**
- **3D > Render Document to File…**

### ii3DPrinting

3D 인쇄 설정, 출력 데이터 준비와 장치 전달을 담당한다.

- **3D > 3D Print Settings…**
- **3D > 3D Print…**

### iiHelp

문맥별 도움말 문서의 탐색과 연결을 담당한다. 실제 도움말 내용은 Congregation이 제공한다.

- **Help > 도움말** — 현재는 `Help unavailable` 상태 행만 있으며 세부 명령은 없다.

## 관련 근거와 확인 범위

- [Congregation 글로벌 메뉴 표시 계약](../../../../Product/Congregation/docs/GlobalApplicationToolbar.md)
- [iiSharedCanvas의 영상 편집 타임라인](../../../iiSharedCanvas/README.md): 모델은 시퀀스 렌더러나 오디오 믹서가 아니며, 해당 `TimelineProject`의 `.iisc` 영속화도 별도이다.
- [iiGraphicAdjust의 필터·변형 범위](../../../iiGraphicAdjust/README.md): 기존 계산 기반의 재사용 가능 범위를 확인한다.
- [iiVisualCodec의 구성요소](../../../iiVisualCodec/README.md): Image, Video, Vision, OpenCV, Geometry, Asset3D, CAD 등의 분리를 확인한다.
- [iiColorManager의 현재 색상 관리 범위](../../../iiColorManager/README.md): 임의 문서·ICC 프로파일 변환 전체가 이미 지원된다고 전제하지 않는다.
- [iiPaintEngine 브러시 구성요소](../../../iiPaintEngine/README.md): `iiBrushTools`는 별도 신규 모듈 대신 `iiPaintEngine::BrushTools`와 기존 호환 패키지를 기준으로 한다.
- [레이어 마스크 작업 계약](../../../../Product/Congregation/docs/MASK_WORKFLOW.md): Layer > Layer Mask 하위 메뉴는 비어 있지만 타임라인에는 별도 마스크 경로가 있다.
- 생성형 기능은 폐기된 ONNX 및 ONNX Runtime을 다시 도입하지 않고 지원 중인 백엔드와 명시적으로 제공된 모델을 사용하도록 설계한다.
