#include "app_registry.h"

#include "aichats/aichats.h"
#include "analyzer/analyzer.h"
#include "gravity/gravity.h"
#include "music/music.h"
#include "photos/photos.h"
#include "settings/settings.h"
#include "squareline/squareline.h"
#include "video/video.h"
#include "assets/assets.h"

namespace {

void no_update() {}
void no_destroy() {}
void no_deactivate() {}

const AppDefinition k_apps[] = {
    {AppId::photos, "Photos", &img_app_photos, photos_app::create, photos_app::update, photos_app::destroy, no_deactivate},
    {AppId::music, "Music", &img_app_music, music_app::create, music_app::update, music_app::destroy, music_app::deactivate},
    {AppId::aichats, "AIChats", &img_app_aichats, aichats_app::create, no_update, no_destroy, no_deactivate},
    {AppId::video, "Video", &img_app_video, video_app::create, no_update, no_destroy, no_deactivate},
    {AppId::analyzer, "Analyzer", &img_app_analyzer, analyzer_app::create, analyzer_app::update, analyzer_app::destroy, no_deactivate},
    {AppId::gravity, "Gravity", &img_app_gravity, gravity_app::create, gravity_app::update, gravity_app::destroy, no_deactivate},
    {AppId::squareline, "Squareline", &esp_brookesia_app_icon_launcher_squareline_112_112,
     squareline_app::create, squareline_app::update, squareline_app::destroy, no_deactivate},
    {AppId::settings, "Settings", &img_app_settings, settings_app::create, settings_app::update, settings_app::destroy, no_deactivate},
};

} // namespace

const AppDefinition *app_find(AppId id)
{
    for (const AppDefinition &app : k_apps) {
        if (app.id == id) return &app;
    }
    return nullptr;
}

const AppDefinition *app_at(size_t index)
{
    return index < app_count() ? &k_apps[index] : nullptr;
}

size_t app_count()
{
    return sizeof(k_apps) / sizeof(k_apps[0]);
}
