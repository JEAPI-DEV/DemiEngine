#include "demi/runtime/scene/ProjectBuildSettings.h"
#include "demi/packages/SemanticVersion.h"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <cctype>
#include <regex>
#include <set>
namespace demi::runtime
{
    namespace
    {
        void issue(Diagnostics &diagnostics, std::string code,
                   std::string message, const std::filesystem::path &path)
        {
            diagnostics.push_back({.severity = Severity::Error,
                                   .code = std::move(code),
                                   .message = std::move(message),
                                   .path = path.string(),
                                   .suggestion = {}});
        }

        template <typename Value>
        Value valueOr(const nlohmann::json &object, std::string_view name,
                      Value fallback, Diagnostics &diagnostics,
                      const std::filesystem::path &path)
        {
            const auto field = object.find(name);
            if (field == object.end())
                return fallback;

            try
            {
                return field->get<Value>();
            }
            catch (const nlohmann::json::exception &)
            {
                issue(diagnostics, "PROJECT_BUILD_FIELD_TYPE_INVALID",
                      "build field '" + std::string(name) +
                          "' has the wrong value type.",
                      path);
                return fallback;
            }
        }

        std::string slug(std::string s)
        {
            std::ranges::transform(s, s.begin(), [](unsigned char c)
                                   { return std::isalnum(c) ? char(std::tolower(c)) : '_'; });
            const auto first = s.find_first_not_of('_');
            const auto last = s.find_last_not_of('_');
            if (first == std::string::npos)
                return "demi_game";
            return s.substr(first, last - first + 1);
        }
        void unknown(const nlohmann::json &o, const std::set<std::string, std::less<>> &a, std::string_view owner, Diagnostics &d, const std::filesystem::path &p)
        {
            for (const auto &[k, v] : o.items())
            {
                (void)v;
                if (!a.contains(k))
                    issue(d, "PROJECT_BUILD_FIELD_UNKNOWN", std::string(owner) + " contains unknown field: " + k, p);
            }
        }
    }
    ProjectBuildSettingsResult parseProjectBuildSettings(const nlohmann::json &project, const std::filesystem::path &path)
    {
        ProjectBuildSettingsResult r;
        r.settings.displayName = valueOr(project, "name", std::string{"Demi Game"},
                                         r.diagnostics, path);
        r.settings.executableName = slug(r.settings.displayName);
        auto b = project.find("build");
        if (b == project.end())
            return r;
        r.settings.authored = true;
        if (!b->is_object())
        {
            issue(r.diagnostics, "PROJECT_BUILD_INVALID", "build must be an object.", path);
            return r;
        }
        unknown(*b, {"application_id", "display_name", "executable_name", "version_name", "version_code", "icon", "splash", "window", "android"}, "build", r.diagnostics, path);
        r.settings.applicationId = valueOr(*b, "application_id", std::string{},
                                           r.diagnostics, path);
        r.settings.displayName = valueOr(*b, "display_name", r.settings.displayName,
                                         r.diagnostics, path);
        r.settings.executableName = valueOr(*b, "executable_name", slug(r.settings.displayName),
                                            r.diagnostics, path);
        r.settings.versionName = valueOr(*b, "version_name", std::string{"0.1.0"},
                                         r.diagnostics, path);
        r.settings.versionCode = valueOr(*b, "version_code", 1,
                                         r.diagnostics, path);
        r.settings.icon = valueOr(*b, "icon", std::string{},
                                  r.diagnostics, path);
        r.settings.splash = valueOr(*b, "splash", std::string{},
                                    r.diagnostics, path);
        static const std::regex app(R"(^[a-z][a-z0-9_]*(\.[a-z][a-z0-9_]*)+$)"), exe(R"(^[a-z0-9][a-z0-9_-]*$)");
        if (!std::regex_match(r.settings.applicationId, app))
            issue(r.diagnostics, "PROJECT_BUILD_APPLICATION_ID_INVALID", "application_id must be lowercase reverse-DNS.", path);
        if (r.settings.displayName.empty())
            issue(r.diagnostics, "PROJECT_BUILD_DISPLAY_NAME_INVALID", "display_name must not be empty.", path);
        if (!std::regex_match(r.settings.executableName, exe))
            issue(r.diagnostics, "PROJECT_BUILD_EXECUTABLE_NAME_INVALID", "executable_name is invalid.", path);
        if (!packages::SemanticVersion::parse(r.settings.versionName))
            issue(r.diagnostics, "PROJECT_BUILD_VERSION_NAME_INVALID", "version_name must be semantic, for example 1.0.0.", path);
        if (r.settings.versionCode < 1)
            issue(r.diagnostics, "PROJECT_BUILD_VERSION_CODE_INVALID", "version_code must be positive.", path);
        for (const auto &[n, v] : {std::pair{"icon", r.settings.icon}, std::pair{"splash", r.settings.splash}})
            if (!v.empty() && !v.starts_with("asset://"))
                issue(r.diagnostics, "PROJECT_BUILD_ASSET_REFERENCE_INVALID", std::string(n) + " must use asset://.", path);
        if (auto w = b->find("window"); w != b->end())
        {
            if (!w->is_object())
                issue(r.diagnostics, "PROJECT_BUILD_WINDOW_INVALID", "window must be an object.", path);
            else
            {
                unknown(*w, {"width", "height", "mode"}, "build.window", r.diagnostics, path);
                r.settings.window.width = valueOr(*w, "width", 1280,
                                                  r.diagnostics, path);
                r.settings.window.height = valueOr(*w, "height", 720,
                                                   r.diagnostics, path);
                r.settings.window.mode = valueOr(*w, "mode", std::string{"windowed"},
                                                 r.diagnostics, path);
                if (r.settings.window.width < 320 || r.settings.window.height < 240 || r.settings.window.width > 16384 || r.settings.window.height > 16384)
                    issue(r.diagnostics, "PROJECT_BUILD_WINDOW_SIZE_INVALID", "window size is outside 320x240..16384x16384.", path);
                if (r.settings.window.mode != "windowed" && r.settings.window.mode != "borderless" && r.settings.window.mode != "fullscreen")
                    issue(r.diagnostics, "PROJECT_BUILD_WINDOW_MODE_INVALID", "window mode is invalid.", path);
            }
        }
        if (auto a = b->find("android"); a != b->end())
        {
            if (!a->is_object())
                issue(r.diagnostics, "PROJECT_BUILD_ANDROID_INVALID", "android must be an object.", path);
            else
            {
                unknown(*a, {"orientation", "min_sdk", "abis", "permissions"}, "build.android", r.diagnostics, path);
                r.settings.android.orientation = valueOr(*a, "orientation", std::string{"unspecified"},
                                                         r.diagnostics, path);
                r.settings.android.minimumSdk = valueOr(*a, "min_sdk", 26,
                                                        r.diagnostics, path);
                r.settings.android.abis = valueOr(*a, "abis", std::vector<std::string>{"arm64-v8a"},
                                                  r.diagnostics, path);
                r.settings.android.permissions = valueOr(*a, "permissions", std::vector<std::string>{},
                                                         r.diagnostics, path);
                const std::set<std::string> orientations{"unspecified", "portrait", "landscape", "portrait_sensor", "landscape_sensor"}, abis{"arm64-v8a", "x86_64"};
                if (!orientations.contains(r.settings.android.orientation))
                    issue(r.diagnostics, "PROJECT_BUILD_ORIENTATION_INVALID", "Android orientation is invalid.", path);
                if (r.settings.android.minimumSdk < 26 || r.settings.android.minimumSdk > 36)
                    issue(r.diagnostics, "PROJECT_BUILD_MIN_SDK_INVALID", "min_sdk must be 26..36.", path);
                if (r.settings.android.abis.empty() || std::ranges::any_of(r.settings.android.abis, [&](const auto &v)
                                                                           { return !abis.contains(v); }))
                    issue(r.diagnostics, "PROJECT_BUILD_ABI_INVALID", "abis support arm64-v8a and x86_64.", path);
                std::set<std::string> unique;
                for (const auto &v : r.settings.android.permissions)
                    if (!v.starts_with("android.permission.") || !unique.insert(v).second)
                        issue(r.diagnostics, "PROJECT_BUILD_PERMISSION_INVALID", "permissions must be unique android.permission.* names.", path);
            }
        }
        return r;
    }
    nlohmann::json projectBuildSettingsJson(const ProjectBuildSettings &s)
    {
        nlohmann::json result = {{"application_id", s.applicationId},
                                 {"display_name", s.displayName},
                                 {"executable_name", s.executableName},
                                 {"version_name", s.versionName},
                                 {"version_code", s.versionCode},
                                 {"window", {{"width", s.window.width},
                                             {"height", s.window.height},
                                             {"mode", s.window.mode}}},
                                 {"android", {{"orientation", s.android.orientation},
                                              {"min_sdk", s.android.minimumSdk},
                                              {"abis", s.android.abis},
                                              {"permissions", s.android.permissions}}}};
        if (!s.icon.empty())
            result["icon"] = s.icon;
        if (!s.splash.empty())
            result["splash"] = s.splash;
        return result;
    }
}
