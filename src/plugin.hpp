#include "plugin.hpp"

Plugin* pluginInstance;

float maybeDefaultContrast = 255.0f;
int maybeDefaultTheme = -1;

void maybeLoadSettings() {}
void maybeSaveSettings() {}

void maybeApplyContrastToAll(float contrast) {
    maybeDefaultContrast = contrast;
}

void maybeApplyThemeToAll(int theme) {
    maybeDefaultTheme = theme;
}

void init(Plugin* p) {
    pluginInstance = p;
    p->addModel(modelSkyline);
}
