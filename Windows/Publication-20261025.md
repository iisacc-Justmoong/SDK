# 2026-10-25 SDK 커밋·푸시 기록

2026-10-24 Windows 검증을 통과한 SDK 소스를 저장소별로 커밋하고 기존 원격 브랜치로 푸시하였다.
활성 SDK 37개와 공통 SDK 저장소를 대상으로 한다. 변경이 없는 SDK도 로컬·원격 커밋 일치를 확인한다.

검증 결과는 CTest 1,783개 통과·가속 장치 조건 제외 4개와 Node 6개 통과이다. Rust CLI 25개, 공통 검증 회귀 15개와 추가 소비자 33개도 별도 통과하였다.
커밋 직전 기존 실행 기록의 소스 지문을 다시 검사하였다. 커밋 전후 수정 파일 바이트와 Git tree 일치를 확인하여 검증한 코드가 그대로 게시되었음을 연결한다. 이전 검증 원본은 보존한다.

| SDK | 브랜치 | 게시 커밋 | 신규 커밋 |
| --- | --- | --- | --- |
| LVRS | master | `8360f4dcafe2e4a72605299d5dfeb010bba6e818` | 있음 |
| LVRS-Svelte | master | `4a0259010e2c74509a452c9903cc612107907792` | 없음 |
| LVUI | main | `dd9204d564b5cdd501b30804389af050008fe445` | 있음 |
| iiAcountManager | master | `cd7c1a93a0bd174cb09d7b76d285c5c1aaa0c270` | 없음 |
| iiActorObject | master | `21e4dc163ef1505ebdffd2ebf8104f12ed48faf1` | 없음 |
| iiAudioGen | main | `fe4933cdf8395c5429768d383ef49c4c8ebd5221` | 없음 |
| iiCSMIDI | master | `5b3b3c688114d5dace35b604bfbe3ef55a3c2246` | 없음 |
| iiCalendar | main | `c8d431dbbe8a378a520044f4055c25a6bcdc277c` | 없음 |
| iiCameraIntergration | main | `4ca7749f4e0a497c2c374b0c9a8a79e71e26f1ac` | 없음 |
| iiColorManager | main | `7098b7a45b70475726d87f3f0ace893ebb8be039` | 없음 |
| iiDecision | main | `18011dd7ff1a2b8532cbfe4d9175b40bc2e42e37` | 없음 |
| iiDeviceMultimedia | main | `71b6d0ac6bb7cec445a560c5eb7d39c9f3057221` | 없음 |
| iiFileProvider | master | `8569d20059973a923b9c24e3b51c2e03d07d1be2` | 있음 |
| iiGUI | main | `f0201a9deb499f4e69e23a801ad4415a1644a1bb` | 있음 |
| iiGeneralDocument | master | `74d174f0d0ff3a37eb6b7bce50b05d3cd61eb052` | 없음 |
| iiGraphicAdjust | main | `0d31845160610cb2a4613c248d4469a9c3e1e671` | 없음 |
| iiHtmlBlock | master | `a28282f0f95db8968ca40fd2ab52b6ceb92fbb33` | 없음 |
| iiImageCodec | main | `9a8392ea1ef5eac750945862632f1941b4ed5be9` | 없음 |
| iiJson | main | `1a428a4135bf1f3d55173dd0cb075a4b9850dc3a` | 없음 |
| iiLicenseManager | master | `cdf9ae86a521c55dfcd2e2e6ccbad3607a10eb83` | 없음 |
| iiLocalDiffusion | master | `a4b9c655c348da9fe3f342726c5287f594b726ce` | 있음 |
| iiLocalLLM | master | `904d4f98d6d03c951a36458d764b64b69a87cd3d` | 없음 |
| iiMultimodalAgent | main | `13ee3d0a61222ae708dffb3e6d431a44d09c29e0` | 있음 |
| iiNetwork | main | `b5adcf61bce3970171eaaaa768dbcc0b3970c802` | 있음 |
| iiPaintEngine | master | `d0d8f2e2ff832bbdaa7935abcd4323166a47a54c` | 없음 |
| iiPhotoLibrary | main | `dc7ba2fdbe7e9914797735068d021532c620043b` | 없음 |
| iiSDKBuilder | main | `2155ddb0a80f9c4e645831420403805feda32fa1` | 없음 |
| iiSDKCore | master | `bb2bb9a50c2863421e5a1b9f2007c5c6dde14176` | 있음 |
| iiSharedCanvas | master | `ed75de96d70c98b517bf88d4e51da756df3f92d2` | 있음 |
| iiSocialPublish | main | `a8fe23b4896745d317d9237b2c1eb1115bc64fd9` | 없음 |
| iiSocietyContainer | master | `47060af2923add5d162cc2a445b9a35aca8d3ae0` | 있음 |
| iiSocietyHelper | master | `9dce4147e3ee918b4000074f99e1c0f390e1a1d0` | 있음 |
| iiUpdateManager | master | `e44439c63e44e5aa8dc9de98fbaae02453ae1a2a` | 있음 |
| iiVoiceOver | master | `e9ab69a46a061a798673b2a70d64f09e18199abf` | 없음 |
| iiWebServer | master | `32fbb5c63298b54b65c1b9a41b6248135576d531` | 있음 |
| iiWhatsNew | master | `738eadca9cbd92657b2fb12e12d6c83ed2bfbe1c` | 있음 |
| iiXml | master | `7ca265429458bb6029fa6ceea35d639acef83e22` | 없음 |

공통 SDK 저장소의 최종 커밋과 38개 원격의 최종 대조는 아래 외부 감사 파일에 기록한다.
`C:\Users\andud\Workspace\build\maintenance\20261025-sdk-publication\publication-audit.json`

Product/Dreamscapes의 로컬 변경은 SDK 게시 범위에 포함하지 않았다. 소스가 없는 9개 예약 경로에는 별도 Git 저장소를 생성하지 않았다.
