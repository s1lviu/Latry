#ifndef APP_LAUNCH_MODE_H
#define APP_LAUNCH_MODE_H

#include <QStringList>

enum class AppLaunchMode {
    UserInterface,
    AndroidService
};

inline constexpr char kAndroidServiceLaunchArgument[] = "--android-service";

inline AppLaunchMode detectAppLaunchMode(const QStringList &arguments)
{
    for (const QString &argument : arguments) {
        if (argument == QLatin1String(kAndroidServiceLaunchArgument)) {
            return AppLaunchMode::AndroidService;
        }
    }

    return AppLaunchMode::UserInterface;
}

inline bool isAndroidServiceLaunchMode(const QStringList &arguments)
{
    return detectAppLaunchMode(arguments) == AppLaunchMode::AndroidService;
}

#endif // APP_LAUNCH_MODE_H
