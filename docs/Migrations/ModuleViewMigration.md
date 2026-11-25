# SDK module view migration boundary

An SDK module's maintained user interface is declared in LVUI and mounted by
an application through the module's C++ presentation model. The application
owns the iiGUI host and connects any services through interfaces. The model
may use the iiSDKCore runtime; it does not gain an iiGUI or LVUI dependency.
Legacy Qt Quick views and their QML-only hosts belong under a `Legacy` directory.
The LVRS and LVRS-Svelte projects are themselves legacy systems and are not
rewritten by this migration.

The current scope is replacement of module-owned `.qml` view declarations
with `.lvui` declarations and isolation of their old QML. Existing C++ Qt Quick
items and further platform or rendering-framework development are outside
this change. Source replacement alone does not establish full behavior or
ensure every existing application has selected its native view path.

iiAccountManager's legacy Quick component is excluded from new builds by
default. Existing Qt applications that still require its fallback must build
that SDK explicitly with `IIACCOUNTMANAGER_BUILD_QUICK=ON`; existing CMake
cache choices remain intact. A package built without Quick also ignores stale
Quick exports in an upgraded install prefix; it does not delete existing files.
iiPhotoLibrary's default service and installed-consumer tests no longer load
or link the old Qt Quick/LVRS view. Its historical QML view checks remain available
with `IIPHOTOLIBRARY_TEST_LEGACY_QML=ON`.

The source inventory covers maintained directories throughout SDK, including
projects whose names do not begin with `ii`, LVUI, resources, examples, tests
and untracked files. The legacy LVRS and LVRS-Svelte projects are excluded.
Hidden directories, `Legacy`, generated builds, third-party trees (including
Python `site-packages` and `dist-packages`) and symbolic links are also excluded.
The staged upstream Dawn distribution at `iiGPU/resources/dawn` is an explicit
external-tree exclusion; similarly named directories elsewhere remain in scope.
QML compatibility fixtures belong under a `Legacy` directory. The inventory
also reports literal `QtQuick`, `QtQml`, and `LVRS` import declarations embedded in C, C++,
Objective-C, Python and JavaScript source files; dynamically assembled source
and other embedding formats still require review. The check does not inspect
generated builds, installed packages or application repositories, and a clean
inventory does not prove that an app mounts its LVUI replacement. Run it with:

```sh
python3 -B tools/audit_module_views.py
python3 -B tests/test_module_views.py
```

On 2026-11-15 the expanded inventory reported zero unisolated QML files and
zero literal `QtQuick`, `QtQml`, or `LVRS` imports in the scanned source files; six audit
regressions passed. Regression fixtures cover project names without
the `ii` prefix, third-party Python package directories, similarly named
maintained directories, the path-specific Dawn exclusion, embedded raw and escaped declarations, and the
legacy-system and user-data exclusions. The account and agent question QML
tests now live in `tests/Legacy`; mixed photo and canvas tests keep their QML
declaration fixtures there while retaining their non-QML checks. LVUI's QML
observation tests also load their unchanged input examples from `tests/Legacy`.
With the same staged LVUI compiler and iiSDKCore runtime, the four generated
LVUI view consumers passed headless macOS tests. Those consumers use the
static iiGUI test package to access its internal test host. A Society macOS
bundle was separately rebuilt against the shared iiGUI package, and its native
account presenter and view passed offscreen tests. The application was not
launched or installed. One first photo-consumer attempt selected an older
photo presentation staging prefix and failed; the test passed after selecting
the current `lvui-photo-info-stage` package. These staging checks do not
replace the installed SDK or establish equivalence on other platforms.
The account, photo-information, and agent-question sheets explicitly select
automatic platform presentation: desktop consumers are centered, while mobile
targets use the native mobile sheet mode. Headless macOS checks cover the
desktop placement; the question view also covers scrolling and submitting a
long request in a small viewport. These are not device-display checks.

The System design's account, photo, question and license frames were checked
against the published LVUI library. Their TextField, LabelButton, Radio and
ApplicationWindow instance keys match the LVUI assets. Some inherited asset
descriptions still use the older LVRS name; that wording does not identify a
different library connection. No Figma components were replaced in this check.

The maintained module view families and their isolated predecessors are:

- Module: iiLicenseManager; Isolated QML: `SocietyLicenseGate.qml`; LVUI declaration: `SocietyLicenseGate.lvui`
- Module: iiAccountManager; Isolated QML: `AccountDialog.qml`, `LoginView.qml`, `SignUpView.qml`; LVUI declaration: `AccountDialog.lvui`
- Module: iiPhotoLibrary; Isolated QML: `PhotosView.qml`, `GalleryInfo.qml`, `GalleryTile.qml`, `GalleryZoom.qml`; LVUI declaration: `PhotosView.lvui`
- Module: iiMultimodalAgent; Isolated QML: `UserQuestionsSheet.qml`; LVUI declaration: `UserQuestionsSheet.lvui`

The photo module's `tests/Legacy/PhotosHarness.qml` is an isolated test
fixture, rather than a separate application view. The table identifies source
replacements; it is not proof of visual or runtime equivalence. Each module's
paired view documentation describes its service adapter, tests, and remaining
feature and platform boundaries. Society and Dreamscapes still
contain consumers of isolated QML views; their native host migration is not
implied by the module source inventory. Dreamscapes has a macOS native question
integration and a macOS/Windows source path for the native license gate; Society has native question, account and
photo integrations in source, all with legacy fallbacks. Society's account
and photo presenters are also selected by their Windows desktop source configuration. Other platforms and
actual window display remain separate work.

All four generated views also have headless consumers using an application-owned
`iiGUI::PaintView` and a fake frame device. These exercise state changes, input,
explicit painting and view retirement without a native window. The test device
does not demonstrate GPU output, OS input integration or product adoption; an
application still supplies its surface, input and scheduling.

iiGUI now also implements a borrowed Windows HWND surface in source, using a
transparent Direct3D 11 and DirectComposition swapchain, owner-thread event
observation, deferred close and change-driven resizing. The host, renderers
and hidden-parent integration test were cross-compiled to x86-64 objects; the
composition surface was separately linked against Windows system libraries.
The complete Windows iiGUI was not linked and Windows tests were not run.
Dreamscapes now selects the Windows license presenter in source; its Windows
product build, account transport and actual window/input behavior are not yet
verified. Society's account and photo presenters have not been linked or exercised
as Windows products; its question presenter and Dreamscapes' question presenter
remain macOS-gated. Society's photo-region Win32 driver and hidden-window test
were cross-linked, while macOS mock-window tests cover geometry, DPI and lifetime;
those checks do not establish actual Windows GPU output or input. A parent with an
occupied topmost DirectComposition target cannot accept this surface, and
input sent to another child HWND requires integration by the owning app.
The iOS iiGUI source now provides a borrowed UIView attachment, event-driven
resize and lifecycle handling, and deferred cleanup. Its policy tests and
iPhoneOS 16/18 arm64 object compilation pass. The full iPhoneOS 18 iiGUI static
library and an installed-stage host consumer also link. Society now selects its
account and photo LVUI presenters on iOS; their actual product presenters,
overlays, iOS photo-region source and generated views compile as iPhoneOS 18
arm64 objects. The iiGUI borrowed view can request a temporary copied document
from its owning UIKit controller; the admission and photo-picker lifecycle
policies have headless tests. This does not link the complete iOS product or
verify app, document picker, device, display or input behavior.
Wayland, Android and Web external attachment remain unsupported. The legacy
fallbacks therefore remain necessary until those integrations are completed.

Using clean iPhoneOS 18 arm64 Core, LVUI and iiGUI staging packages, the
license, account and photo LVUI declarations were AOT compiled and linked
against their staged presentation packages. The question declaration was
compiled and linked with the question model and C boundary directly from
source; this is not a build of the complete agent library. The binaries have no
direct Qt dynamic linkage. None of the consumers was executed, and these builds
do not establish app mounting, device input or GPU display behavior. An initial
account build selected older headers from an aggregate SDK prefix; it passed
after Core and Vulkan headers were isolated from those other packages.
The same four AOT consumers were also built and linked with Android NDK 30,
arm64 and API 24; the first three use staged presentation packages and the
question consumer uses its model and C boundary from source. Their ELF
dependency tables contain no direct Qt dynamic linkage. This is a cross-build,
not an Android package, app launch, emulator run or real-device test.
All eight mobile binaries also contain the generated view's host-mount code
and iiGUI's `Host::mount_widgets` symbols; neither call was executed as part of
the mobile checks.

An additional iOS build of the Qt account service without Quick succeeded.
Enabling its isolated Quick fallback against the existing
`SDK/build/congregation-ipados/prefix` failed package configuration because
that prefix's LVRS and iiGUI native ABI identifiers differ. This describes
that particular staging combination, not every installed SDK. LVRS and its
installation were not changed. Society's account and photo selectors now include
iOS, but the object-only product fixtures do not link that legacy package and do
not prove complete product integration.

The license gate has an iiSDKCore-only `iiLicenseManager::Presentation` package
and can be built without the legacy Qt or native license clients. Those clients
retain their existing gate-model symbols for binary compatibility, so updated
CMake exports reject linking either client together with the presentation
provider. An older or manually linked binary cannot be checked this way.

The photo presentation model now rejects foreign-thread queries and direct
commands before reading or mutating owner-thread state, including no-op
requests. Its query ports close before model state is destroyed. Focused
concurrent, cancellation, sanitizer and staged LVUI-consumer checks cover this
boundary; they do not establish device-level rendering or input equivalence.
The legacy photo-service adapter now exposes preview URLs only when their
revision is ready. A new or changed preview source is invalidated before view
observers are notified, including removal and reinsertion; repeated sources in
one update are coalesced. Service-bridge, staged LVUI-consumer and sanitizer
tests cover this path without opening an application window.

The Web text backend now uses one grapheme-safe, width-dependent line split for
both measuring and painting the question view's long text. A WebAssembly test
with a fake Canvas covers that shared path; browser rendering and application
mounting have not been verified, and full Unicode line-breaking rules are not
claimed. The current Core was independently staged as Web side modules, and
the iiGUI Web static library compiled against that staging. This is not an
application link or browser execution result.

The generated LVUI headless consumer source and the C++ presentation model
source for all four view families also compiled independently to MinGW x86-64
objects on 2026-11-15. The checks used staged headers and did not link Windows
libraries, run the Windows tests or prove visual and input equivalence.
The generated-view-to-host mount adapters for all four also compile to x86-64
COFF objects. This additional check does not link a Windows executable.

The license, account, photo and question presentation models also expose C
boundaries and installed Python bindings. They send owner-thread
changes through the same queries and publish notifications without polling.
Photo row-property observation is limited to IDs explicitly requested by the
consumer. The account binding has an opt-in Core cooldown loop and emits
deadline-change events for a host-managed one-shot wakeup. The question binding
rejects edits to stale row generations. Staged C and Python consumers for all
four passed on macOS; Windows C ABI sources were cross-compiled to objects but
not linked or run. The question binding is part of the existing shared agent
library and therefore retains that library's existing dependencies.
