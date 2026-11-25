# Congregation 글로벌 메뉴 기능별 iiSDK 모듈 배정

기준일: 2026-11-25. Congregation의 글로벌 메뉴에서 현재 비활성인 명령과 비어 있는 하위 메뉴를 기능별로 나열하고, 각 기능의 주 담당 모듈과 함께 구성할 모듈을 정리한다. [메뉴 정의](../../../../Product/Congregation/App/Application/AppMenu.h)와 [기존 모듈별 배정 문서](../../../../Product/Congregation/docs/GLOBAL_MENU_SDK_ALLOCATION.md)를 기준으로 작성했다.

아래 211행은 비활성 명령 162개와 빈 하위 메뉴 49개이다. **†**는 `No commands available`만 있는 하위 메뉴로, 세부 기능이 정해지지 않았으므로 배정이 잠정적이다. Help의 `Help unavailable`은 상태 행으로 별도 기록한다. 활성 상태이지만 불완전한 경로도 별도 표에 둔다.

**`*`는 신규 제안 모듈이다.** 별표가 없는 모듈은 현재 SDK 작업 공간에 있다. 기존 모듈이 해당 메뉴 기능을 이미 구현했거나 앱에 연결했다는 뜻은 아니다. 신규 이름은 책임 배정 제안이며 실제 모듈을 만든 것은 아니다.

주 담당은 기능의 핵심 계약을 맡는 제안이다. 협력 모듈은 해당 문서 작업·파일 입출력·계산 등의 경계를 함께 구성할 후보이며, 실제 파일 입출력이나 해당 백엔드를 쓰지 않는 구현에서는 생략할 수 있다. 모듈 간 직접 링크나 공개 타입 의존성을 뜻하지 않는다. Congregation이 독립 인터페이스를 조합하고 제품 명령·이력·오류를 연결한다. 파일 생성은 iiFileProvider가 담당한다. [SDK 독립성 지침](../../../AGENT.md)

메뉴 화면과 설정 화면에는 LVUI와 iiGUI를 공통으로 사용한다. 표의 iiGUI는 그 공통 표시 외에 클립보드·파일 선택·플랫폼 기능처럼 별도 경계가 필요한 경우에만 적는다. 생성형 기능에는 폐기된 ONNX 및 ONNX Runtime을 다시 도입하지 않는다.

구현 시 각 SDK는 C++ API와 독립적인 연동 인터페이스, Python API를 제공한다. Congregation은 동일한 제품 명령을 AI 에이전트가 호출할 수 있도록 CLI 또는 이에 준하는 인터페이스와 MCP를 구성한다. `iiAgentApplicationInterface`를 앱의 명령 등록과 연동할 수 있지만, 현재 제품에서 이 통합이 완성되었다고 가정하지 않는다.

## File

| 기능 | 주 담당 | 협력 모듈 |
| --- | --- | --- |
| Open As… | `iiDocumentSession`* | `iiSharedCanvas`, `iiFileProvider`, `iiVisualCodec`, `iiGUI` |
| Open Recent † | `iiDocumentSession`* | `iiSharedCanvas`, `iiFileProvider` |
| Close | `iiDocumentSession`* | `iiSharedCanvas` |
| Close All | `iiDocumentSession`* | `iiSharedCanvas` |
| Close Others | `iiDocumentSession`* | `iiSharedCanvas` |
| Save a Copy… | `iiDocumentSession`* | `iiSharedCanvas`, `iiFileProvider` |
| Revert | `iiDocumentSession`* | `iiSharedCanvas`, `iiFileProvider` |
| Place Linked… | `iiSharedCanvas` | `iiResource`, `iiFileProvider`, `iiVisualCodec::Image` |
| Automate † | `iiAutomation`* | `iiAgentApplicationInterface` |
| Scripts † | `iiAutomation`* | `iiAgentApplicationInterface` |
| Import † | `iiVisualCodec` | `iiSharedCanvas`, `iiFileProvider` |
| File Info… | `iiSharedCanvas` | `iiFileProvider`, `iiVisualCodec::Image` |
| Print… | `iiPrint`* | `iiSharedCanvas`, `iiColorManager` |
| Print One Copy | `iiPrint`* | `iiSharedCanvas`, `iiColorManager` |

## Edit

| 기능 | 주 담당 | 협력 모듈 |
| --- | --- | --- |
| Toggle Last State | `iiSharedCanvas` | — |
| Fade… | `iiPaintEngine` | `iiSharedCanvas` |
| Cut | `iiPaintEngine` | `iiSharedCanvas`, `iiGUI` |
| Paste Special † | `iiPaintEngine` | `iiSharedCanvas`, `iiGUI` |
| Clear | `iiPaintEngine` | `iiSharedCanvas` |
| Search (명령 검색으로 잠정 해석) | `iiCommandRegistry`* | — |
| Check Spelling… | `iiTextServices`* | `iiSharedCanvas` |
| Find and Replace Text… | `iiTextServices`* | `iiSharedCanvas` |
| Fill… | `iiPaintEngine` | `iiSharedCanvas` |
| Stroke… | `iiPaintEngine` | `iiSharedCanvas` |
| Content-Aware Fill… | `iiGraphicAdjust` | `iiSharedCanvas` |
| Content-Aware Scale | `iiGraphicAdjust` | `iiSharedCanvas` |
| Puppet Warp | `iiGraphicAdjust` | `iiSharedCanvas` |
| Perspective Warp | `iiGraphicAdjust` | `iiSharedCanvas` |
| Free Transform | `iiPaintEngine` | `iiSharedCanvas`, `iiGraphicAdjust` |
| Transform † | `iiPaintEngine` | `iiSharedCanvas`, `iiGraphicAdjust` |
| Auto-Align Layers… | `iiGraphicAdjust` | `iiSharedCanvas`, `iiVisualCodec::OpenCV` |
| Auto-Blend Layers… | `iiGraphicAdjust` | `iiSharedCanvas` |
| Sky Replacement… | `iiGraphicAdjust` | `iiSharedCanvas`, `iiVisualCodec::Vision` |
| Define Brush Preset… | `iiPaintEngine` | `iiSharedCanvas`, `iiPresetManager`*, `iiFileProvider` |
| Define Pattern… | `iiPresetManager`* | `iiFileProvider`, `iiSharedCanvas` |
| Define Custom Shape… | `iiPresetManager`* | `iiFileProvider`, `iiSharedCanvas` |
| Purge † | `iiResource` | — |
| Presets † | `iiPresetManager`* | `iiFileProvider`, `iiSharedCanvas` |
| Color Settings… | `iiColorManager` | `iiSharedCanvas` |
| Assign Profile… | `iiColorManager` | `iiSharedCanvas` |
| Convert to Profile… | `iiColorManager` | `iiSharedCanvas` |
| Keyboard Shortcuts… | `iiCommandRegistry`* | `iiGUI`, `iiFileProvider` |
| Menus… | `iiCommandRegistry`* | `iiGUI`, `iiFileProvider` |
| Toolbar… | `iiCommandRegistry`* | `iiGUI`, `iiFileProvider` |

## Select

| 기능 | 주 담당 | 협력 모듈 |
| --- | --- | --- |
| All | `iiPaintEngine` | `iiSharedCanvas` |
| Reselect | `iiPaintEngine` | `iiSharedCanvas` |
| All Layers | `iiSharedCanvas` | — |
| Deselect Layers | `iiSharedCanvas` | — |
| Find Layers | `iiSharedCanvas` | — |
| Isolate Layers | `iiSharedCanvas` | — |
| Color Range… | `iiGraphicAdjust` | `iiSharedCanvas` |
| Focus Area… | `iiVisualCodec::OpenCV` | `iiSharedCanvas`, `iiPaintEngine` |
| Subject | `iiVisualCodec::Vision` | `iiSharedCanvas`, `iiPaintEngine` |
| Sky | `iiVisualCodec::Vision` | `iiSharedCanvas`, `iiPaintEngine` |
| Select and Mask… | `iiPaintEngine` | `iiSharedCanvas` |
| Modify † | `iiPaintEngine` | `iiSharedCanvas` |
| Grow | `iiPaintEngine` | `iiSharedCanvas` |
| Similar | `iiPaintEngine` | `iiSharedCanvas` |
| Transform Selection | `iiPaintEngine` | `iiSharedCanvas` |
| Load Selection… | `iiSharedCanvas` | — |
| Save Selection… | `iiSharedCanvas` | — |
| New 3D Extrusion | `iiVisualCodec::CAD` | `iiSharedCanvas`, `ii3DScene`* |

## View

| 기능 | 주 담당 | 협력 모듈 |
| --- | --- | --- |
| Proof Setup † | `iiColorManager` | `iiSharedCanvas` |
| Proof Colors | `iiColorManager` | `iiSharedCanvas` |
| Gamut Warning | `iiColorManager` | `iiSharedCanvas` |
| Pixel Aspect Ratio † | `iiSharedCanvas` | — |
| Pixel Aspect Ratio Correction | `iiViewport`* | `iiSharedCanvas` |
| 32-bit Preview Options… | `iiGraphicAdjust` | `iiSharedCanvas` |
| Fit Layer on Screen | `iiViewport`* | `iiSharedCanvas` |
| Fit Artboard on Screen | `iiViewport`* | `iiSharedCanvas` |
| 100% | `iiViewport`* | `iiSharedCanvas` |
| 200% | `iiViewport`* | `iiSharedCanvas` |
| Print Size | `iiViewport`* | `iiSharedCanvas`, `iiGUI` |
| Actual Size | `iiViewport`* | `iiSharedCanvas`, `iiGUI` |
| Flip Horizontal | `iiViewport`* | `iiSharedCanvas` |
| Pattern Preview | `iiViewport`* | `iiSharedCanvas` |
| Extras | `iiViewport`* | `iiSharedCanvas` |
| Show † | `iiViewport`* | `iiSharedCanvas` |
| Snap | `iiViewport`* | `iiSharedCanvas` |
| Snap To † | `iiViewport`* | `iiSharedCanvas` |
| Guides † | `iiViewport`* | `iiSharedCanvas` |
| Lock Slices | `iiViewport`* | `iiSharedCanvas` |
| Clear Slices | `iiViewport`* | `iiSharedCanvas` |

## Layer

| 기능 | 주 담당 | 협력 모듈 |
| --- | --- | --- |
| Copy CSS | `iiSharedCanvas` | `iiGUI` |
| Copy SVG | `iiSharedCanvas` | `iiGUI` |
| Quick Export as JPG | `iiVisualCodec::Image` | `iiSharedCanvas`, `iiFileProvider` |
| Export As… | `iiVisualCodec::Image` | `iiSharedCanvas`, `iiFileProvider`, `iiGUI` |
| Rename Layer… | `iiSharedCanvas` | — |
| Layer Style † | `iiSharedCanvas` | `iiGraphicAdjust` |
| New Fill Layer † | `iiSharedCanvas` | `iiGraphicAdjust` |
| New Adjustment Layer † | `iiSharedCanvas` | `iiGraphicAdjust` |
| Layer Content Options… | `iiSharedCanvas` | `iiGraphicAdjust` |
| Layer Mask † | `iiSharedCanvas` | `iiPaintEngine` |
| Vector Mask † | `iiSharedCanvas` | `iiPaintEngine` |
| Mask All Objects | `iiVisualCodec::Vision` | `iiSharedCanvas`, `iiPaintEngine` |
| Video Layers † | `iiSharedCanvas` | `iiVisualCodec::Video` |
| Rasterize † | `iiSharedCanvas` | `iiPaintEngine` |
| New Layer-Based Slice | `iiSharedCanvas` | `iiViewport`* |
| Group Layers | `iiSharedCanvas` | — |
| Ungroup Layers | `iiSharedCanvas` | — |
| Arrange † | `iiSharedCanvas` | — |
| Combine Shapes † | `iiSharedCanvas` | — |
| Align † | `iiSharedCanvas` | — |
| Distribute † | `iiSharedCanvas` | — |
| Lock Layers… | `iiSharedCanvas` | — |
| Link Layers | `iiSharedCanvas` | — |
| Select Linked Layers | `iiSharedCanvas` | — |
| Merge Layers | `iiSharedCanvas` | `iiPaintEngine` |
| Merge Visible | `iiSharedCanvas` | `iiPaintEngine` |
| Flatten Image | `iiSharedCanvas` | `iiPaintEngine` |
| Matting † | `iiGraphicAdjust` | `iiSharedCanvas` |

## Image

| 기능 | 주 담당 | 협력 모듈 |
| --- | --- | --- |
| Mode † | `iiColorManager` | `iiSharedCanvas` |
| Adjustments † | `iiGraphicAdjust` | `iiSharedCanvas` |
| Auto Tone | `iiGraphicAdjust` | `iiSharedCanvas` |
| Auto Contrast | `iiGraphicAdjust` | `iiSharedCanvas` |
| Auto Color | `iiGraphicAdjust` | `iiSharedCanvas` |
| Image Size… | `iiSharedCanvas` | `iiGraphicAdjust` |
| Canvas Size… | `iiSharedCanvas` | — |
| Image Rotation † | `iiSharedCanvas` | `iiGraphicAdjust` |
| Crop | `iiSharedCanvas` | `iiGraphicAdjust` |
| Trim… | `iiSharedCanvas` | `iiGraphicAdjust` |
| Reveal All | `iiSharedCanvas` | — |
| Duplicate… | `iiSharedCanvas` | — |
| Apply Image… | `iiSharedCanvas` | `iiGraphicAdjust` |
| Calculations… | `iiSharedCanvas` | `iiGraphicAdjust` |
| Variables † | `iiDocumentVariables`* | `iiSharedCanvas` |
| Apply Data Set… | `iiDocumentVariables`* | `iiSharedCanvas` |
| Trap… | `iiPrint`* | `iiColorManager`, `iiSharedCanvas` |
| Analysis † | `iiGraphicAdjust` | `iiSharedCanvas` |

## Sequence

| 기능 | 주 담당 | 협력 모듈 |
| --- | --- | --- |
| Sequence Settings… | `iiSharedCanvas` | — |
| Render Effects in Work Area | `iiTimelineRender`* | `iiSharedCanvas`, `iiGraphicAdjust`, `iiFileProvider` |
| Render Entire Work Area | `iiTimelineRender`* | `iiSharedCanvas`, `iiFileProvider` |
| Render Selection | `iiTimelineRender`* | `iiSharedCanvas`, `iiFileProvider` |
| Render Audio | `iiAudioProcessing`* | `iiSharedCanvas` |
| Delete Render Files | `iiTimelineRender`* | `iiFileProvider` |
| Delete Work Area Render Files | `iiTimelineRender`* | `iiFileProvider` |
| Match Frame | `iiSharedCanvas` | — |
| Reverse Match Frame | `iiSharedCanvas` | — |
| Add Edit | `iiSharedCanvas` | — |
| Add Edit to All Tracks | `iiSharedCanvas` | — |
| Trim Edit | `iiSharedCanvas` | — |
| Extend Selected Edit to Playhead | `iiSharedCanvas` | — |
| Apply Video Transition | `iiSharedCanvas` | `iiTimelineRender`* |
| Apply Audio Transition | `iiSharedCanvas` | `iiAudioProcessing`* |
| Apply Default Transitions to Selection | `iiSharedCanvas` | `iiTimelineRender`*, `iiAudioProcessing`* |
| Lift | `iiSharedCanvas` | — |
| Extract | `iiSharedCanvas` | — |
| Zoom In | `iiViewport`* | `iiSharedCanvas` |
| Zoom Out | `iiViewport`* | `iiSharedCanvas` |
| Close Gap | `iiSharedCanvas` | — |
| Go to Gap † | `iiSharedCanvas` | — |
| Snap in Timeline | `iiViewport`* | `iiSharedCanvas` |
| Linked Selection | `iiSharedCanvas` | — |
| Selection Follows Playhead | `iiSharedCanvas` | — |
| Show Through Edits | `iiSharedCanvas` | — |
| Normalize Mix Track… | `iiAudioProcessing`* | `iiSharedCanvas` |
| Make Subsequence | `iiSharedCanvas` | — |
| Auto Reframe Sequence… | `iiVisualCodec::Vision` | `iiSharedCanvas` |
| Transcribe Sequence… | `iiSpeechRecognition`* | `iiSharedCanvas` |
| Simplify Sequence… | `iiSharedCanvas` | — |
| Add Tracks… | `iiSharedCanvas` | — |
| Delete Tracks… | `iiSharedCanvas` | — |
| Captions † | `iiSharedCanvas` | — |

## Filter

| 기능 | 주 담당 | 협력 모듈 |
| --- | --- | --- |
| Gaussian Blur | `iiGraphicAdjust` | `iiSharedCanvas` |
| Generative filter | `iiLocalDiffusion` | `iiSharedCanvas` |
| Filter Gallery… | `iiGraphicAdjust` | `iiSharedCanvas` |
| Adaptive Wide Angle… | `iiGraphicAdjust` | `iiSharedCanvas` |
| Lens Correction… | `iiGraphicAdjust` | `iiSharedCanvas` |
| Liquify… | `iiGraphicAdjust` | `iiSharedCanvas` |
| Vanishing Point… | `iiGraphicAdjust` | `iiSharedCanvas` |
| 3D † | `ii3DRender`* | — |
| Noise † | `iiGraphicAdjust` | `iiSharedCanvas` |
| Render † | `iiGraphicAdjust` | `iiSharedCanvas` |
| Video † | `iiGraphicAdjust` | `iiSharedCanvas` |
| Sharpen † | `iiGraphicAdjust` | `iiSharedCanvas` |
| Stylize † | `iiGraphicAdjust` | `iiSharedCanvas` |
| Distort † | `iiGraphicAdjust` | `iiSharedCanvas` |
| Pixelate † | `iiGraphicAdjust` | `iiSharedCanvas` |
| Blur † | `iiGraphicAdjust` | `iiSharedCanvas` |
| Blur Gallery † | `iiGraphicAdjust` | `iiSharedCanvas` |
| Other † | `iiGraphicAdjust` | `iiSharedCanvas` |

## 3D

| 기능 | 주 담당 | 협력 모듈 |
| --- | --- | --- |
| New 3D Layer from File… | `iiVisualCodec::Asset3D` | `ii3DScene`*, `iiSharedCanvas`, `iiFileProvider` |
| Merge 3D Layers | `ii3DScene`* | `iiSharedCanvas` |
| Export 3D Layer… | `iiVisualCodec::Asset3D` | `ii3DScene`*, `iiSharedCanvas`, `iiFileProvider` |
| Spherical Panorama † | `iiVisualCodec::Spatial` | — |
| Download Content… | `iiNetwork` | `iiFileProvider` |
| New 3D Extrusion from Selected Layer | `iiVisualCodec::CAD` | `ii3DScene`*, `iiSharedCanvas` |
| New 3D Extrusion from Selected Path | `iiVisualCodec::CAD` | `ii3DScene`*, `iiSharedCanvas` |
| New 3D Extrusion from Current Selection | `iiVisualCodec::CAD` | `ii3DScene`*, `iiSharedCanvas` |
| New Mesh from Layer † | `iiMeshProcessing`* | `ii3DScene`*, `iiSharedCanvas` |
| Group Objects | `ii3DScene`* | `iiSharedCanvas` |
| Group All Objects in Scene | `ii3DScene`* | `iiSharedCanvas` |
| Move Objects to Ground Plane | `ii3DScene`* | `iiSharedCanvas` |
| Pack Objects on Ground Plane | `ii3DScene`* | `iiSharedCanvas` |
| New Tiled Painting from Layer | `ii3DScene`* | `iiSharedCanvas`, `iiPaintEngine` |
| Generate UVs… | `iiMeshProcessing`* | `ii3DScene`*, `iiSharedCanvas` |
| Paint Falloff… | `ii3DScene`* | `iiSharedCanvas`, `iiPaintEngine` |
| Paint System † | `ii3DScene`* | `iiSharedCanvas`, `iiPaintEngine` |
| Paint on Target Texture † | `ii3DScene`* | `iiSharedCanvas`, `iiPaintEngine` |
| Select Paintable Areas | `ii3DScene`* | `iiSharedCanvas` |
| Create Painting Overlay † | `ii3DScene`* | `iiSharedCanvas`, `iiPaintEngine` |
| Split Extrusion | `iiMeshProcessing`* | `ii3DScene`*, `iiSharedCanvas` |
| Apply Cross Section to Scene | `iiMeshProcessing`* | `ii3DScene`*, `iiSharedCanvas` |
| Unify Scene for 3D Printing | `iiMeshProcessing`* | `ii3DScene`*, `iiSharedCanvas` |
| Simplify Mesh… | `iiMeshProcessing`* | `ii3DScene`*, `iiSharedCanvas` |
| Make Work Path from 3D Layer | `iiMeshProcessing`* | `ii3DScene`*, `iiSharedCanvas` |
| Render 3D Layer | `ii3DRender`* | `ii3DScene`*, `iiSharedCanvas` |
| Render Document to File… | `ii3DRender`* | `ii3DScene`*, `iiSharedCanvas`, `iiVisualCodec::Image`, `iiFileProvider` |
| Sketch with Current Brush | `ii3DScene`* | `iiSharedCanvas`, `iiPaintEngine` |
| 3D Print Settings… | `ii3DPrinting`* | `ii3DScene`*, `iiMeshProcessing`* |
| 3D Print… | `ii3DPrinting`* | `ii3DScene`*, `iiMeshProcessing`* |

## Help

| 기능 | 주 담당 | 협력 모듈 |
| --- | --- | --- |
| 도움말: 현재 `Help unavailable` 상태만 표시 | `iiHelp`* | — |

## 활성화되어 있지만 불완전한 기능

| 기능과 현재 제한 | 주 담당 | 협력 모듈 |
| --- | --- | --- |
| Edit > Copy / Copy Merged: 둘 다 선택 영역·선택 레이어가 아닌 프레임 0의 합성 이미지를 복사 | `iiSharedCanvas` | `iiGUI` |
| 텍스트 입력이 포커스를 유지할 때 Edit > Undo / Redo / Copy / Copy Merged / Paste 메뉴 명령이 입력 대상으로 전달되지 않음 | `iiGUI` | `iiSharedCanvas` (캔버스 명령 대상과의 구분) |
| File > Export > Export…: 비트맵 내보내기가 현재 프레임 대신 프레임 0 사용 | `iiSharedCanvas` | `iiVisualCodec::Image`, `iiFileProvider` |
| iPad의 File > Save As… 및 첫 Save: 명시적 파일명 저장 경로 미지원 | `iiGUI` | `iiFileProvider`, `iiSharedCanvas` |
| iPad의 File > Export > Export…: 폴더 선택 후 파일명이 `Canvas.png`로 고정 | `iiGUI` | `iiVisualCodec::Image`, `iiFileProvider` |
| iPad·Android의 View > Screen Mode > Presentation 및 Window > Presentation: 호스트 전체 화면 전환 미지원 | `iiGUI` | — |
| Android의 New document…, Add canvas…, Open…, Save As…, 첫 Save, Export…, Place Embedded…, Image > Place image…, Edit > Preferences > Preferences…: 보조 창 또는 파일 선택기 미지원 | `iiGUI` | `iiFileProvider`, `iiSharedCanvas` (파일·문서 처리 시) |

## 경계와 현재 제공 범위

- [iiSharedCanvas](../../../iiSharedCanvas/README.md)의 `TimelineProject`는 영상 편집 모델이며, 그 자체는 시퀀스 렌더러나 오디오 믹서가 아니다. 해당 모델의 `.iisc` 영속화도 별도이다.
- [iiGraphicAdjust](../../../iiGraphicAdjust/README.md)는 다수의 이미지 필터와 변형 계산을 제공한다. 각 메뉴에 필요한 옵션, 결과 적용과 이력 연결은 추가 확인이 필요하다.
- [iiVisualCodec](../../../iiVisualCodec/README.md)은 Image·Video·Vision·OpenCV·Geometry·Asset3D·CAD 등의 구성요소를 가진다. Vision 객체의 추적 ID는 호출자가 제공하며 자동 추적기가 있는 것으로 전제하지 않는다. 모델과 백엔드도 별도로 필요하다. [Vision 인터페이스](../../../iiVisualCodec/src/Vision/Vision.h)
- [iiColorManager](../../../iiColorManager/README.md)는 임의 문서의 ICC 프로파일 변환 전체를 지원한다고 주장하지 않는다. 프로파일 변환·교정과 플랫폼별 지원은 확장이 필요하다.
- 브러시 작성은 [iiPaintEngine::BrushTools](../../../iiPaintEngine/README.md)를 기준으로 한다. `iiBrushTools`는 기존 호환 패키지이며 별도 신규 모듈로 배정하지 않는다.
- [Congregation의 Layer Mask 계약](../../../../Product/Congregation/docs/MASK_WORKFLOW.md)처럼 글로벌 메뉴가 비어 있어도 앱의 다른 경로에 기능이 있을 수 있다.
- 이 문서는 소스 기반의 배정 제안이다. 앱을 실행하여 메뉴를 조작하거나 각 플랫폼에서 실제 동작을 검증한 결과는 아니다.
