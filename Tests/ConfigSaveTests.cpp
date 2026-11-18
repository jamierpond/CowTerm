#include <Multiplexer/Config.h>

#include <Miro/Json.h>
#include <NanoTest/NanoTest.h>

using namespace nano;
using namespace term;

auto tSaveKeepsSettingsItDoesNotKnow = test("Config/savingKeepsSettingsThisBuildDoesNotKnow") = []
{
    constexpr auto existing = R"({"fontSize": 13, "settingOfANewerBuild": "kept", "anotherNewerSetting": "big-cow"})";
    constexpr auto newFontSize = 15.0f;
    auto config = AppConfig {};
    config.fontSize = newFontSize;

    const auto saved = Miro::Json::parse(writeConfigKeepingUnknownSettings(config, existing));

    check(saved["settingOfANewerBuild"].asString() == "kept");
    check(saved["anotherNewerSetting"].asString() == "big-cow");
    check(saved["fontSize"].asNumber() == newFontSize, "the known fields are this build's");
};

auto tSaveOverAnUnreadableFile = test("Config/savingOverAnUnreadableFileWritesTheKnownFields") = []
{
    const auto saved = Miro::Json::parse(writeConfigKeepingUnknownSettings(AppConfig {}, "not json {"));

    check(saved.isObject());
    check(saved["fontSize"].isNumber());
};
