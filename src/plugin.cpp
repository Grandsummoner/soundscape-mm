#include "plugin.hpp"
#include <fstream>

Plugin* pluginInstance;

float maybeDefaultContrast = 255.0f;
int maybeDefaultTheme = -1;

static std::string getSettingsPath() {
    return asset::user("maybe.json");
}

void maybeLoadSettings() {
    std::string path = getSettingsPath();
    FILE* file = std::fopen(path.c_str(), "r");
    if (!file) return;
    DEFER({ std::fclose(file); });
    json_error_t error;
    json_t* rootJ = json_loadf(file, 0, &error);
    if (!rootJ) return;
    DEFER({ json_decref(rootJ); });
    json_t* contrastJ = json_object_get(rootJ, "defaultContrast");
    if (contrastJ) maybeDefaultContrast = json_number_value(contrastJ);
    json_t* themeJ = json_object_get(rootJ, "defaultTheme");
    if (themeJ) maybeDefaultTheme = json_integer_value(themeJ);
}

void maybeSaveSettings() {
    json_t* rootJ = json_object();
    json_object_set_new(rootJ, "defaultContrast", json_real(maybeDefaultContrast));
    json_object_set_new(rootJ, "defaultTheme", json_integer(maybeDefaultTheme));
    std::string path = getSettingsPath();
    FILE* file = std::fopen(path.c_str(), "w");
    if (file) {
        json_dumpf(rootJ, file, JSON_INDENT(2));
        std::fclose(file);
    }
    json_decref(rootJ);
}

void maybeApplyContrastToAll(float contrast) {
    maybeDefaultContrast = contrast;
    maybeSaveSettings();
    std::vector<int64_t> moduleIds = APP->engine->getModuleIds();
    for (int64_t id : moduleIds) {
        Module* m = APP->engine->getModule(id);
        if (m && m->model && m->model->plugin && m->model->plugin->slug == "maybe") {
            json_t* dataJ = m->dataToJson();
            if (dataJ) {
                json_object_set_new(dataJ, "panelContrast", json_real(contrast));
                m->dataFromJson(dataJ);
                json_decref(dataJ);
            }
        }
    }
}

void maybeApplyThemeToAll(int theme) {
    maybeDefaultTheme = theme;
    maybeSaveSettings();
    std::vector<int64_t> moduleIds = APP->engine->getModuleIds();
    for (int64_t id : moduleIds) {
        Module* m = APP->engine->getModule(id);
        if (m && m->model && m->model->plugin && m->model->plugin->slug == "maybe") {
            json_t* dataJ = m->dataToJson();
            if (dataJ) {
                json_object_set_new(dataJ, "panelTheme", json_integer(theme));
                m->dataFromJson(dataJ);
                json_decref(dataJ);
            }
        }
    }
}

void init(Plugin* p) {
    pluginInstance = p;
    maybeLoadSettings();
    p->addModel(modelSkyline);
}
