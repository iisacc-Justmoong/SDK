#include <LVUI>
#include <iiGUI>
#include "ModuleImportView.generated.h"

// Compile the generated view in the same translation unit as SDK API users.
static_assert(sizeof(ii::gui::WidgetBuilder) > 0);
static_assert(sizeof(lvui::StyleColor) > 0);
