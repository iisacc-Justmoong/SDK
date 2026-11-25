# 2026-10-25 SDK 커밋·푸시 기록

2026-10-24 Windows 검증을 통과한 SDK 소스를 저장소별로 커밋하고 기존 원격 브랜치로 푸시하였다.
활성 SDK 37개와 공통 SDK 저장소를 대상으로 한다. 변경이 없는 SDK도 로컬·원격 커밋 일치를 확인한다.

검증 결과는 CTest 1,783개 통과·가속 장치 조건 제외 4개와 Node 6개 통과이다. Rust CLI 25개, 공통 검증 회귀 15개와 추가 소비자 33개도 별도 통과하였다.
커밋 직전 기존 실행 기록의 소스 지문을 다시 검사하였다. 커밋 전후 수정 파일 바이트와 Git tree 일치를 확인하여 검증한 코드가 그대로 게시되었음을 연결한다. 이전 검증 원본은 보존한다.

- SDK: LVRS; 브랜치: master; 게시 커밋: `8360f4dcafe2e4a72605299d5dfeb010bba6e818`; 신규 커밋: 있음
- SDK: LVRS-Svelte; 브랜치: master; 게시 커밋: `4a0259010e2c74509a452c9903cc612107907792`; 신규 커밋: 없음
- SDK: LVUI; 브랜치: main; 게시 커밋: `dd9204d564b5cdd501b30804389af050008fe445`; 신규 커밋: 있음
- SDK: iiAccountManager; 브랜치: master; 게시 커밋: `cd7c1a93a0bd174cb09d7b76d285c5c1aaa0c270`; 신규 커밋: 없음
- SDK: iiActorObject; 브랜치: master; 게시 커밋: `21e4dc163ef1505ebdffd2ebf8104f12ed48faf1`; 신규 커밋: 없음
- SDK: iiAudioGen; 브랜치: main; 게시 커밋: `fe4933cdf8395c5429768d383ef49c4c8ebd5221`; 신규 커밋: 없음
- SDK: iiCSMIDI; 브랜치: master; 게시 커밋: `5b3b3c688114d5dace35b604bfbe3ef55a3c2246`; 신규 커밋: 없음
- SDK: iiCalendar; 브랜치: main; 게시 커밋: `c8d431dbbe8a378a520044f4055c25a6bcdc277c`; 신규 커밋: 없음
- SDK: iiCameraIntegration; 브랜치: main; 게시 커밋: `4ca7749f4e0a497c2c374b0c9a8a79e71e26f1ac`; 신규 커밋: 없음
- SDK: iiColorManager; 브랜치: main; 게시 커밋: `7098b7a45b70475726d87f3f0ace893ebb8be039`; 신규 커밋: 없음
- SDK: iiDecision; 브랜치: main; 게시 커밋: `18011dd7ff1a2b8532cbfe4d9175b40bc2e42e37`; 신규 커밋: 없음
- SDK: iiDeviceMultimedia; 브랜치: main; 게시 커밋: `71b6d0ac6bb7cec445a560c5eb7d39c9f3057221`; 신규 커밋: 없음
- SDK: iiFileProvider; 브랜치: master; 게시 커밋: `8569d20059973a923b9c24e3b51c2e03d07d1be2`; 신규 커밋: 있음
- SDK: iiGUI; 브랜치: main; 게시 커밋: `f0201a9deb499f4e69e23a801ad4415a1644a1bb`; 신규 커밋: 있음
- SDK: iiGeneralDocument; 브랜치: master; 게시 커밋: `74d174f0d0ff3a37eb6b7bce50b05d3cd61eb052`; 신규 커밋: 없음
- SDK: iiGraphicAdjust; 브랜치: main; 게시 커밋: `0d31845160610cb2a4613c248d4469a9c3e1e671`; 신규 커밋: 없음
- SDK: iiHtmlBlock; 브랜치: master; 게시 커밋: `a28282f0f95db8968ca40fd2ab52b6ceb92fbb33`; 신규 커밋: 없음
- SDK: iiImageCodec; 브랜치: main; 게시 커밋: `9a8392ea1ef5eac750945862632f1941b4ed5be9`; 신규 커밋: 없음
- SDK: iiJson; 브랜치: main; 게시 커밋: `1a428a4135bf1f3d55173dd0cb075a4b9850dc3a`; 신규 커밋: 없음
- SDK: iiLicenseManager; 브랜치: master; 게시 커밋: `cdf9ae86a521c55dfcd2e2e6ccbad3607a10eb83`; 신규 커밋: 없음
- SDK: iiLocalDiffusion; 브랜치: master; 게시 커밋: `a4b9c655c348da9fe3f342726c5287f594b726ce`; 신규 커밋: 있음
- SDK: iiLocalLLM; 브랜치: master; 게시 커밋: `904d4f98d6d03c951a36458d764b64b69a87cd3d`; 신규 커밋: 없음
- SDK: iiMultimodalAgent; 브랜치: main; 게시 커밋: `13ee3d0a61222ae708dffb3e6d431a44d09c29e0`; 신규 커밋: 있음
- SDK: iiNetwork; 브랜치: main; 게시 커밋: `b5adcf61bce3970171eaaaa768dbcc0b3970c802`; 신규 커밋: 있음
- SDK: iiPaintEngine; 브랜치: master; 게시 커밋: `d0d8f2e2ff832bbdaa7935abcd4323166a47a54c`; 신규 커밋: 없음
- SDK: iiPhotoLibrary; 브랜치: main; 게시 커밋: `dc7ba2fdbe7e9914797735068d021532c620043b`; 신규 커밋: 없음
- SDK: iiSDKBuilder; 브랜치: main; 게시 커밋: `2155ddb0a80f9c4e645831420403805feda32fa1`; 신규 커밋: 없음
- SDK: iiSDKCore; 브랜치: master; 게시 커밋: `bb2bb9a50c2863421e5a1b9f2007c5c6dde14176`; 신규 커밋: 있음
- SDK: iiSharedCanvas; 브랜치: master; 게시 커밋: `ed75de96d70c98b517bf88d4e51da756df3f92d2`; 신규 커밋: 있음
- SDK: iiSocialPublish; 브랜치: main; 게시 커밋: `a8fe23b4896745d317d9237b2c1eb1115bc64fd9`; 신규 커밋: 없음
- SDK: iiSocietyContainer; 브랜치: master; 게시 커밋: `47060af2923add5d162cc2a445b9a35aca8d3ae0`; 신규 커밋: 있음
- SDK: iiSocietyHelper; 브랜치: master; 게시 커밋: `9dce4147e3ee918b4000074f99e1c0f390e1a1d0`; 신규 커밋: 있음
- SDK: iiUpdateManager; 브랜치: master; 게시 커밋: `e44439c63e44e5aa8dc9de98fbaae02453ae1a2a`; 신규 커밋: 있음
- SDK: iiVoiceOver; 브랜치: master; 게시 커밋: `e9ab69a46a061a798673b2a70d64f09e18199abf`; 신규 커밋: 없음
- SDK: iiWebServer; 브랜치: master; 게시 커밋: `32fbb5c63298b54b65c1b9a41b6248135576d531`; 신규 커밋: 있음
- SDK: iiWhatsNew; 브랜치: master; 게시 커밋: `738eadca9cbd92657b2fb12e12d6c83ed2bfbe1c`; 신규 커밋: 있음
- SDK: iiXml; 브랜치: master; 게시 커밋: `7ca265429458bb6029fa6ceea35d639acef83e22`; 신규 커밋: 없음

공통 SDK 저장소의 최종 커밋과 38개 원격의 최종 대조는 아래 외부 감사 파일에 기록한다.
`C:\Users\andud\Workspace\build\maintenance\20261025-sdk-publication\publication-audit.json`

Product/Dreamscapes의 로컬 변경은 SDK 게시 범위에 포함하지 않았다. 소스가 없는 9개 예약 경로에는 별도 Git 저장소를 생성하지 않았다.
